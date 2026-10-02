#pragma once

#include "protocol.h"

#include <stdint.h>

namespace uart_bridge {

void begin();
void poll(uint32_t now_ms);
bool ready();

// Writes one already-framed buffer. The caller owns admission.
bool writeFrame(const uint8_t* frame, size_t len);

// Writes a failsafe STOP and, on success, records its sequence in link.
// Returns false when the port, epoch, or UART write is not ready.
bool sendFailsafeStop(siren::LinkGuard& link);

}  // namespace uart_bridge
