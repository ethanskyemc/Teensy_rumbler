#pragma once

#include "config.h"

#include <stdint.h>

namespace keypad {

struct KeyEvent {
    uint8_t button;
    bool pressed;
};

// What the LEDs are allowed to show. Playback fields come from Teensy
// status. air_horn_held is the local finger state for the momentary key.
struct KeypadView {
    uint32_t now_ms;
    uint32_t startup_elapsed_ms;
    bool in_startup;
    bool link_up;
    bool playing;
    bool rumbler_enabled;
    bool air_horn_held;
    siren::SoundID active_sound;
};

void begin();
void poll(uint32_t now_ms);
void setView(const KeypadView& view);
void render(uint32_t now_ms);
bool nextEvent(KeyEvent& out);

// Pure color choice. Safe to call before the LED pins are known.
cfg::Rgb colorForButton(uint8_t index, const KeypadView& view);

}  // namespace keypad
