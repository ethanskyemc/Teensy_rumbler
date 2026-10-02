#include "uart_bridge.h"

#include "debug.h"
#include "pins.h"

#include <string.h>

namespace uart_bridge {
namespace {

constexpr uint8_t kRxDepth = 4;

bool port_ready_ = false;
siren::FrameParser parser_;
uint8_t rx_len_[kRxDepth] = {};
uint8_t rx_data_[kRxDepth][siren::kMaxFrameBytes] = {};
uint8_t rx_head_ = 0;
uint8_t rx_count_ = 0;
bool rx_drop_logged_ = false;

void pushRx(const uint8_t* frame, uint8_t len) {
    if (rx_count_ >= kRxDepth) {
        if (!rx_drop_logged_) {
            rx_drop_logged_ = true;
            SIREN_LOG("uart: status queue full\n");
        }
        return;
    }
    const uint8_t slot = static_cast<uint8_t>((rx_head_ + rx_count_) % kRxDepth);
    rx_len_[slot] = len;
    memcpy(rx_data_[slot], frame, len);
    rx_count_ = static_cast<uint8_t>(rx_count_ + 1u);
}

}  // namespace

void begin() {
    port_ready_ = false;
    parser_.reset();
    rx_head_ = 0;
    rx_count_ = 0;
    rx_drop_logged_ = false;
    if (pins::kUartRx < 0 || pins::kUartTx < 0) {
        SIREN_LOG("uart: pins not configured\n");
        return;
    }
    // UART0, already pinned to GPIO20/21 by the C3 variant. USB CDC owns Serial.
    Serial0.begin(siren::kUartBaud, SERIAL_8N1, pins::kUartRx, pins::kUartTx);
    port_ready_ = true;
    SIREN_LOG("uart: open %u baud rx=%d tx=%d\n", siren::kUartBaud, pins::kUartRx, pins::kUartTx);
}

void poll(uint32_t now_ms) {
    if (!port_ready_) {
        return;
    }
    while (Serial0.available() > 0) {
        const int value = Serial0.read();
        if (value < 0) {
            break;
        }
        if (!parser_.push(static_cast<uint8_t>(value), now_ms)) {
            continue;
        }
        if (parser_.type() != siren::PacketType::STATUS) {
            SIREN_LOG("uart: ignored type %u\n", static_cast<unsigned>(parser_.type()));
            continue;
        }
        pushRx(parser_.frame(), parser_.frameLength());
    }
}

bool ready() {
    return port_ready_;
}

bool takeFrame(uint8_t* out, size_t cap, size_t& out_len) {
    if (out == nullptr || rx_count_ == 0 || rx_len_[rx_head_] > cap) {
        return false;
    }
    out_len = rx_len_[rx_head_];
    memcpy(out, rx_data_[rx_head_], out_len);
    rx_head_ = static_cast<uint8_t>((rx_head_ + 1u) % kRxDepth);
    rx_count_ = static_cast<uint8_t>(rx_count_ - 1u);
    if (rx_count_ == 0) {
        rx_drop_logged_ = false;
    }
    return true;
}

bool writeFrame(const uint8_t* frame, size_t len) {
    if (!port_ready_ || frame == nullptr || len == 0 || len > siren::kMaxFrameBytes) {
        return false;
    }
    return Serial0.write(frame, len) == len;
}

bool sendFailsafeStop(siren::LinkGuard& link) {
    if (!port_ready_ || link.epoch == 0 || !link.heartbeat_seen) {
        return false;
    }
    const uint16_t sequence = siren::nextSequence(link.last_sequence);
    const siren::CommandPacket stop = siren::makeFailsafeStop(link.epoch, sequence);
    if (!siren::commandFieldsValid(stop)) {
        SIREN_LOG("uart: failsafe STOP malformed\n");
        return false;
    }
    uint8_t frame[siren::kMaxFrameBytes];
    const size_t n = siren::buildCommandFrame(stop, frame, sizeof(frame));
    if (n == 0 || !writeFrame(frame, n)) {
        return false;
    }
    link.last_sequence = sequence;
    SIREN_LOG("uart: failsafe STOP seq=%u epoch=%u\n", stop.sequence, stop.epoch);
    return true;
}

}  // namespace uart_bridge
