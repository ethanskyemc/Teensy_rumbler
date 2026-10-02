#include "wireless.h"

#include "config.h"
#include "debug.h"
#include "sounds.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <freertos/queue.h>
#include <string.h>

namespace wireless {
namespace {

bool radio_ready_ = false;
bool peer_added_ = false;
bool broadcast_ = false;
uint8_t peer_mac_[6] = {};
uint8_t epoch_ = 1;
uint16_t sequence_ = 0;
uint32_t next_heartbeat_ms_ = 0;
bool heartbeat_fail_logged_ = false;
volatile bool send_failed_ = false;
uint32_t last_delivery_log_ms_ = 0;
bool status_pending_ = false;
siren::StatusPacket pending_status_ = {};
uint32_t last_status_ms_ = 0;
bool have_status_seq_ = false;
uint8_t status_epoch_ = 0;
uint16_t status_sequence_ = 0;
QueueHandle_t status_queue_ = nullptr;

constexpr uint8_t kStatusDepth = 4;

struct RadioSlot {
    uint8_t len;
    uint8_t data[siren::kMaxFrameBytes];
};

void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    (void)info;
    if (status_queue_ == nullptr || data == nullptr || len <= 0 ||
        static_cast<size_t>(len) > siren::kMaxFrameBytes) {
        return;
    }
    RadioSlot slot = {};
    slot.len = static_cast<uint8_t>(len);
    memcpy(slot.data, data, slot.len);
    xQueueSend(status_queue_, &slot, 0);
}

bool acceptStatus(const siren::StatusPacket& status) {
    if (!siren::statusFieldsValid(status)) {
        return false;
    }
    if (!have_status_seq_ || status.epoch != status_epoch_) {
        have_status_seq_ = true;
        status_epoch_ = status.epoch;
        status_sequence_ = status.status_sequence;
        return true;
    }
    if (!siren::sequenceIsNewer(status.status_sequence, status_sequence_)) {
        return false;
    }
    status_sequence_ = status.status_sequence;
    return true;
}

void ingestStatus(const uint8_t* frame, uint8_t len, uint32_t now_ms) {
    siren::FrameParser parser;
    bool got = false;
    siren::PacketType type = siren::PacketType::COMMAND;
    uint8_t payload[siren::kMaxPayload] = {};
    uint8_t payload_len = 0;
    for (uint8_t i = 0; i < len; ++i) {
        if (!parser.push(frame[i], now_ms)) {
            continue;
        }
        got = true;
        type = parser.type();
        payload_len = parser.payloadLength();
        memcpy(payload, parser.payload(), payload_len);
    }
    if (!got || type != siren::PacketType::STATUS) {
        if (got) {
            SIREN_LOG("wireless: ignored type %u\n", static_cast<unsigned>(type));
        }
        return;
    }
    siren::StatusPacket status = {};
    if (!siren::deserializeStatus(payload, payload_len, status) || !acceptStatus(status)) {
        return;
    }
    pending_status_ = status;
    status_pending_ = true;
    last_status_ms_ = now_ms;
    SIREN_LOG_VERBOSE("wireless: status %s playing=%u seq=%u\n", siren::soundName(status.sound),
                      status.playing ? 1u : 0u, status.status_sequence);
}

void drainStatus(uint32_t now_ms) {
    RadioSlot slot = {};
    while (status_queue_ != nullptr && xQueueReceive(status_queue_, &slot, 0) == pdTRUE) {
        ingestStatus(slot.data, slot.len, now_ms);
    }
}

void logMac(const char* label, const uint8_t mac[6]) {
    SIREN_LOG("%s %02X:%02X:%02X:%02X:%02X:%02X\n", label, mac[0], mac[1], mac[2], mac[3], mac[4],
              mac[5]);
}

void onSend(const esp_now_send_info_t* info, esp_now_send_status_t status) {
    (void)info;
    // Broadcast has no ack. A failure flag there would print on every heartbeat.
    if (!broadcast_ && status != ESP_NOW_SEND_SUCCESS) {
        send_failed_ = true;
    }
}

bool addPeer(const uint8_t mac[6]) {
    if (esp_now_is_peer_exist(mac)) {
        return true;
    }
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = siren::kEspNowChannel;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    return esp_now_add_peer(&peer) == ESP_OK;
}

}  // namespace

void begin() {
    uint32_t random_bits = esp_random();
    epoch_ = static_cast<uint8_t>(random_bits & 0xFFu);
    if (epoch_ == 0) {
        epoch_ = 1;
    }
    sequence_ = 0;
    radio_ready_ = false;
    peer_added_ = false;
    broadcast_ = false;
    heartbeat_fail_logged_ = false;
    send_failed_ = false;
    status_pending_ = false;
    last_status_ms_ = 0;
    have_status_seq_ = false;
    status_epoch_ = 0;
    status_sequence_ = 0;
    next_heartbeat_ms_ = 0;

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    if (esp_wifi_set_channel(siren::kEspNowChannel, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
        SIREN_LOG("wireless: channel set failed\n");
    }
    if (esp_now_init() != ESP_OK) {
        SIREN_LOG("wireless: esp-now init failed\n");
        return;
    }
    radio_ready_ = true;
    status_queue_ = xQueueCreate(kStatusDepth, sizeof(RadioSlot));
    if (status_queue_ == nullptr) {
        SIREN_LOG("wireless: status queue failed\n");
    } else if (esp_now_register_recv_cb(onRecv) != ESP_OK) {
        SIREN_LOG("wireless: recv callback failed\n");
    }
    if (esp_now_register_send_cb(onSend) != ESP_OK) {
        SIREN_LOG("wireless: send callback failed\n");
    }

    uint8_t own_mac[6] = {};
    if (esp_wifi_get_mac(WIFI_IF_STA, own_mac) != ESP_OK) {
        SIREN_LOG("wireless: sta mac read failed\n");
    } else {
        logMac("wireless: local mac", own_mac);
    }

    broadcast_ = siren::macIsUnset(cfg::kPeerMac);
    const uint8_t* dest = broadcast_ ? siren::kEspNowBroadcastMac : cfg::kPeerMac;
    memcpy(peer_mac_, dest, 6);
    if (!addPeer(peer_mac_)) {
        SIREN_LOG("wireless: peer add failed\n");
        return;
    }
    peer_added_ = true;
    if (broadcast_) {
        SIREN_LOG("wireless: peer unset, sending broadcast\n");
    } else {
        logMac("wireless: peer", peer_mac_);
    }
    SIREN_LOG("wireless: epoch=%u channel=%u heartbeat=%u ms\n", epoch_, siren::kEspNowChannel,
              siren::kHeartbeatIntervalMs);
}

void update(uint32_t now_ms) {
    drainStatus(now_ms);
    if (send_failed_) {
        send_failed_ = false;
        if (last_delivery_log_ms_ == 0 ||
            siren::elapsedMs(now_ms, last_delivery_log_ms_, 2000)) {
            last_delivery_log_ms_ = now_ms;
            SIREN_LOG("wireless: radio did not deliver\n");
        }
    }
    if (!radio_ready_ || !peer_added_) {
        return;
    }
    if (next_heartbeat_ms_ != 0 &&
        !siren::elapsedMs(now_ms, next_heartbeat_ms_, siren::kHeartbeatIntervalMs)) {
        return;
    }
    next_heartbeat_ms_ = now_ms;
    sendCommand(siren::makeHeartbeat(epoch_, 0));
}

bool linkUp() {
    return last_status_ms_ != 0 &&
           !siren::elapsedMs(millis(), last_status_ms_, siren::kLinkTimeoutMs);
}

uint8_t localEpoch() {
    return epoch_;
}

bool sendCommand(const siren::CommandPacket& command) {
    if (!radio_ready_ || !peer_added_) {
        SIREN_LOG("wireless: radio not ready\n");
        return false;
    }
    siren::CommandPacket stamped = command;
    stamped.version = siren::kProtocolVersion;
    stamped.epoch = epoch_;
    sequence_ = siren::nextSequence(sequence_);
    stamped.sequence = sequence_;
    if (!siren::commandFieldsValid(stamped)) {
        SIREN_LOG("wireless: refused %s\n", siren::commandName(stamped.command));
        return false;
    }

    uint8_t frame[siren::kMaxFrameBytes];
    const size_t n = siren::buildCommandFrame(stamped, frame, sizeof(frame));
    if (n == 0) {
        return false;
    }
    const bool heartbeat = stamped.command == siren::CommandType::HEARTBEAT;
    if (esp_now_send(peer_mac_, frame, n) != ESP_OK) {
        if (!heartbeat || !heartbeat_fail_logged_) {
            heartbeat_fail_logged_ = heartbeat;
            SIREN_LOG("wireless: send failed %s seq=%u\n", siren::commandName(stamped.command),
                      stamped.sequence);
        }
        return false;
    }
    if (heartbeat) {
        heartbeat_fail_logged_ = false;
        SIREN_LOG_VERBOSE("wireless: heartbeat seq=%u\n", stamped.sequence);
        return true;
    }
    SIREN_LOG("wireless: queued %s %s %s seq=%u epoch=%u\n", siren::commandName(stamped.command),
              siren::soundName(stamped.sound), siren::paramName(stamped.param), stamped.sequence,
              stamped.epoch);
    return true;
}

bool takeStatus(siren::StatusPacket& out) {
    if (!status_pending_) {
        return false;
    }
    out = pending_status_;
    status_pending_ = false;
    return true;
}

}  // namespace wireless
