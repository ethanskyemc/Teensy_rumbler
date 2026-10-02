#pragma once

#include <stdint.h>

namespace rumbler {

void begin();
void setEnabled(bool enabled);
void setGain(uint8_t gain);
bool enabled();
uint8_t gain();

}  // namespace rumbler
