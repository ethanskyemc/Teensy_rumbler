#pragma once

#include "protocol.h"

namespace command_handler {

void begin();
void poll(uint32_t now_ms);

// Fills a status snapshot from the audio engine. poll() transmits it.
bool fillStatus(siren::StatusPacket& out);

}  // namespace command_handler
