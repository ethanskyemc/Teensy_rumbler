#pragma once

#include "commands.h"

#include <stdint.h>

// Snapshot passed into the renderer. Drawing code stays in display.cpp.
struct UiState {
    bool link_up;
    bool playing;
    bool rumbler_enabled;
    bool volume_valid;
    bool battery_valid;
    uint8_t volume_master;
    uint8_t volume_siren;
    uint8_t volume_rumbler;
    uint8_t battery_percent;
    uint8_t flags;
    siren::SoundID sound;
    siren::TeensyStatus teensy;
    siren::SdStatus sd;
};

namespace display {

void begin();
void render(const UiState& state);

}  // namespace display
