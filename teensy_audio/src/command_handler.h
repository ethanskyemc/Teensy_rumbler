#pragma once

#include "protocol.h"

namespace command_handler {

void begin();
void poll(uint32_t now_ms);

// Fills a status snapshot from the audio engine. Phase 1 does not transmit it.
bool fillStatus(siren::StatusPacket& out);

}  // namespace command_handler
