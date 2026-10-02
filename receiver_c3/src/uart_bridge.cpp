#include "uart_bridge.h"

#include "debug.h"
#include "pins.h"

namespace uart_bridge {
namespace {

bool port_ready_ = false;
siren::FrameParser parser_;

}  // namespace

void begin() {
    port_ready_ = false;
    parser_.reset();
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
    // Drain bytes the Teensy sends so the FIFO cannot stall. Status forwarding
    // back to the remote is phase 7; a completed frame is only logged here.
    while (Serial0.available() > 0) {
        const int value = Serial0.read();
        if (value < 0) {
            break;
        }
        if (!parser_.push(static_cast<uint8_t>(value), now_ms)) {
            continue;
        }
        SIREN_LOG("uart: rx type=%u len=%u\n", static_cast<unsigned>(parser_.type()),
                  parser_.frameLength());
    }
}

bool ready() {
    return port_ready_;
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
