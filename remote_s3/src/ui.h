#pragma once

#include "display.h"

namespace ui {

void begin();
void update(uint32_t now_ms);
const UiState& state();

}  // namespace ui
