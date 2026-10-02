#pragma once

#include "protocol.h"

#include <stdint.h>

namespace wireless {

void begin();
void update(uint32_t now_ms);
bool linkUp();
uint8_t localEpoch();

// Stamps epoch and the next sequence, then queues one ESP-NOW frame.
// A true result means the radio accepted the buffer. It is not a Teensy ack.
bool sendCommand(const siren::CommandPacket& command);
bool takeStatus(siren::StatusPacket& out);

}  // namespace wireless
