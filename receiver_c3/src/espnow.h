#pragma once

#include <stdint.h>

namespace espnow {

void begin();
void poll(uint32_t now_ms);

}  // namespace espnow
