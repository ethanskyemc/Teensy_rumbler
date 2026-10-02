#include "wireless.h"

#include "config.h"
#include "debug.h"
#include "sounds.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_random.h>
#include <esp_wifi.h>
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
    if (esp_now_register_send_cb(onSend) != ESP_OK) {
        SIREN_LOG("wireless: send callback failed\n");
    }

    uint8_t own_mac[6] = {};
    WiFi.macAddress(own_mac);
    logMac("wireless: local mac", own_mac);

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
    return false;
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
