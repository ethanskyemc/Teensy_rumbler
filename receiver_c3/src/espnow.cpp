#include "espnow.h"

#include "config.h"
#include "debug.h"
#include "protocol.h"
#include "sounds.h"
#include "uart_bridge.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

namespace espnow {
namespace {

constexpr uint8_t kRadioDepth = 8;
constexpr uint32_t kHoldForHeartbeatMs = 500;

struct RadioSlot {
    uint8_t len;
    uint8_t data[siren::kMaxFrameBytes];
    uint8_t src[6];
    bool has_src;
};

bool radio_ready_ = false;
QueueHandle_t radio_queue_ = nullptr;
volatile uint32_t radio_drops_ = 0;
uint32_t logged_drops_ = 0;

// Set from a forwarded PLAY press. Cleared by STOP or a momentary release.
// This is the failsafe hint, not a second audio engine.
bool audio_active_ = false;
uint32_t last_heartbeat_ms_ = 0;
bool failsafe_latched_ = false;
bool failsafe_logged_ = false;
siren::LinkGuard link_ = {};

bool held_ = false;
siren::CommandPacket held_cmd_ = {};
uint8_t held_frame_[siren::kMaxFrameBytes] = {};
uint8_t held_len_ = 0;
uint32_t held_since_ms_ = 0;

uint8_t last_src_[6] = {};
bool have_src_ = false;
uint32_t last_bad_log_ms_ = 0;

void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    if (radio_queue_ == nullptr || data == nullptr || len <= 0 ||
        static_cast<size_t>(len) > siren::kMaxFrameBytes) {
        return;
    }
    RadioSlot slot = {};
    slot.len = static_cast<uint8_t>(len);
    memcpy(slot.data, data, slot.len);
    if (info != nullptr && info->src_addr != nullptr) {
        memcpy(slot.src, info->src_addr, 6);
        slot.has_src = true;
    }
    if (xQueueSend(radio_queue_, &slot, 0) != pdTRUE) {
        radio_drops_ = radio_drops_ + 1u;
    }
}

void logMac(const char* label, const uint8_t mac[6]) {
    SIREN_LOG("%s %02X:%02X:%02X:%02X:%02X:%02X\n", label, mac[0], mac[1], mac[2], mac[3], mac[4],
              mac[5]);
}

void noteForwarded(const siren::CommandPacket& cmd) {
    if (cmd.command == siren::CommandType::STOP) {
        audio_active_ = false;
        return;
    }
    if (cmd.command != siren::CommandType::PLAY) {
        return;
    }
    if (cmd.param == siren::CommandParam::PRESS) {
        audio_active_ = true;
        return;
    }
    if (cmd.param == siren::CommandParam::RELEASE && siren::soundIsMomentary(cmd.sound)) {
        audio_active_ = false;
    }
}

bool forwardFrame(const uint8_t* frame, uint8_t len, const siren::CommandPacket& cmd) {
    if (!uart_bridge::writeFrame(frame, len)) {
        SIREN_LOG("uart: forward failed %s seq=%u\n", siren::commandName(cmd.command), cmd.sequence);
        return false;
    }
    noteForwarded(cmd);
    if (cmd.command == siren::CommandType::HEARTBEAT) {
        SIREN_LOG_VERBOSE("espnow: heartbeat seq=%u epoch=%u\n", cmd.sequence, cmd.epoch);
        return true;
    }
    SIREN_LOG("espnow: forward %s %s %s seq=%u epoch=%u\n", siren::commandName(cmd.command),
              siren::soundName(cmd.sound), siren::paramName(cmd.param), cmd.sequence, cmd.epoch);
    return true;
}

void dropHeld(const char* reason) {
    if (!held_) {
        return;
    }
    SIREN_LOG("espnow: drop held %s (%s)\n", siren::commandName(held_cmd_.command), reason);
    held_ = false;
}

void flushHeld(uint32_t now_ms) {
    if (!held_) {
        return;
    }
    if (siren::elapsedMs(now_ms, held_since_ms_, kHoldForHeartbeatMs)) {
        dropHeld("timeout");
        return;
    }
    if (!link_.heartbeat_seen) {
        return;
    }
    const siren::AdmitResult result = siren::admitCommand(link_, held_cmd_);
    if (result != siren::AdmitResult::ACCEPT) {
        dropHeld(siren::admitName(result));
        return;
    }
    uint8_t frame[siren::kMaxFrameBytes];
    const uint8_t len = held_len_;
    memcpy(frame, held_frame_, len);
    const siren::CommandPacket cmd = held_cmd_;
    held_ = false;
    forwardFrame(frame, len, cmd);
}

void holdForHeartbeat(const uint8_t* frame, uint8_t len, const siren::CommandPacket& cmd,
                      uint32_t now_ms) {
    if (held_) {
        SIREN_LOG("espnow: drop extra %s before heartbeat\n", siren::commandName(cmd.command));
        return;
    }
    memcpy(held_frame_, frame, len);
    held_len_ = len;
    held_cmd_ = cmd;
    held_since_ms_ = now_ms;
    held_ = true;
    SIREN_LOG("espnow: holding %s until heartbeat\n", siren::commandName(cmd.command));
}

void handleCommand(const uint8_t* frame, uint8_t len, uint32_t now_ms) {
    siren::FrameParser parser;
    bool got = false;
    siren::PacketType got_type = siren::PacketType::COMMAND;
    uint8_t payload[siren::kMaxPayload] = {};
    uint8_t payload_len = 0;
    uint8_t good_frame[siren::kMaxFrameBytes] = {};
    uint8_t good_len = 0;
    for (uint8_t i = 0; i < len; ++i) {
        if (!parser.push(frame[i], now_ms)) {
            continue;
        }
        got = true;
        got_type = parser.type();
        good_len = parser.frameLength();
        memcpy(good_frame, parser.frame(), good_len);
        payload_len = parser.payloadLength();
        memcpy(payload, parser.payload(), payload_len);
    }
    if (!got || got_type != siren::PacketType::COMMAND) {
        if (!got) {
            if (last_bad_log_ms_ == 0 || siren::elapsedMs(now_ms, last_bad_log_ms_, 1000)) {
                last_bad_log_ms_ = now_ms;
                SIREN_LOG("espnow: ignored non-frame packet\n");
            }
            return;
        }
        SIREN_LOG("espnow: ignored type %u\n", static_cast<unsigned>(got_type));
        return;
    }

    siren::CommandPacket cmd = {};
    if (!siren::deserializeCommand(payload, payload_len, cmd)) {
        SIREN_LOG("espnow: bad command payload\n");
        return;
    }

    const siren::AdmitResult result = siren::admitCommand(link_, cmd);
    if (result == siren::AdmitResult::REJECT_NEEDS_HEARTBEAT) {
        holdForHeartbeat(good_frame, good_len, cmd, now_ms);
        return;
    }
    if (result != siren::AdmitResult::ACCEPT) {
        if (cmd.command != siren::CommandType::HEARTBEAT) {
            SIREN_LOG("espnow: reject %s %s seq=%u epoch=%u\n", siren::admitName(result),
                      siren::commandName(cmd.command), cmd.sequence, cmd.epoch);
        }
        return;
    }

    if (!forwardFrame(good_frame, good_len, cmd)) {
        return;
    }
    if (cmd.command == siren::CommandType::HEARTBEAT) {
        last_heartbeat_ms_ = now_ms;
        failsafe_latched_ = false;
        failsafe_logged_ = false;
        flushHeld(now_ms);
    }
}

void noteSource(const RadioSlot& slot) {
    if (!slot.has_src) {
        return;
    }
    if (have_src_) {
        bool same = true;
        for (uint8_t i = 0; i < 6; ++i) {
            if (last_src_[i] != slot.src[i]) {
                same = false;
                break;
            }
        }
        if (same) {
            return;
        }
    }
    memcpy(last_src_, slot.src, 6);
    have_src_ = true;
    logMac("espnow: remote mac", last_src_);
}

void pollFailsafe(uint32_t now_ms) {
    if (!audio_active_ || last_heartbeat_ms_ == 0 || failsafe_latched_) {
        return;
    }
    if (!siren::elapsedMs(now_ms, last_heartbeat_ms_, siren::kLinkTimeoutMs)) {
        return;
    }
    if (!uart_bridge::sendFailsafeStop(link_)) {
        if (!failsafe_logged_) {
            failsafe_logged_ = true;
            SIREN_LOG("uart: failsafe STOP not written\n");
        }
        return;
    }
    failsafe_latched_ = true;
    failsafe_logged_ = false;
    audio_active_ = false;
    SIREN_LOG("espnow: heartbeat timeout while audio active\n");
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
    radio_ready_ = false;
    radio_queue_ = nullptr;
    radio_drops_ = 0;
    logged_drops_ = 0;
    audio_active_ = false;
    last_heartbeat_ms_ = 0;
    failsafe_latched_ = false;
    failsafe_logged_ = false;
    link_ = {};
    held_ = false;
    held_len_ = 0;
    have_src_ = false;
    last_bad_log_ms_ = 0;

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    if (esp_wifi_set_channel(siren::kEspNowChannel, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
        SIREN_LOG("espnow: channel set failed\n");
    }
    if (esp_now_init() != ESP_OK) {
        SIREN_LOG("espnow: init failed\n");
        return;
    }
    radio_ready_ = true;

    uint8_t own_mac[6] = {};
    WiFi.macAddress(own_mac);
    logMac("espnow: local mac", own_mac);
    SIREN_LOG("espnow: channel=%u timeout=%u ms\n", siren::kEspNowChannel, siren::kLinkTimeoutMs);

    radio_queue_ = xQueueCreate(kRadioDepth, sizeof(RadioSlot));
    if (radio_queue_ == nullptr) {
        SIREN_LOG("espnow: radio queue failed\n");
        return;
    }
    if (esp_now_register_recv_cb(onRecv) != ESP_OK) {
        SIREN_LOG("espnow: recv callback failed\n");
        return;
    }

    if (siren::macIsUnset(cfg::kPeerMac)) {
        SIREN_LOG("espnow: peer unset, receiving broadcast\n");
        return;
    }
    if (!addPeer(cfg::kPeerMac)) {
        SIREN_LOG("espnow: peer add failed\n");
        return;
    }
    logMac("espnow: peer", cfg::kPeerMac);
}

void poll(uint32_t now_ms) {
    if (!radio_ready_) {
        return;
    }
    const uint32_t drops = radio_drops_;
    if (drops != logged_drops_ &&
        (last_bad_log_ms_ == 0 || siren::elapsedMs(now_ms, last_bad_log_ms_, 1000))) {
        SIREN_LOG("espnow: dropped %u radio frames\n", drops - logged_drops_);
        logged_drops_ = drops;
        last_bad_log_ms_ = now_ms;
    }
    if (held_ && siren::elapsedMs(now_ms, held_since_ms_, kHoldForHeartbeatMs)) {
        dropHeld("timeout");
    }

    RadioSlot slot = {};
    while (radio_queue_ != nullptr && xQueueReceive(radio_queue_, &slot, 0) == pdTRUE) {
        noteSource(slot);
        handleCommand(slot.data, slot.len, now_ms);
    }
    pollFailsafe(now_ms);
}

}  // namespace espnow
