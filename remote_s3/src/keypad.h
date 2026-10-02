#pragma once

#include "config.h"

#include <stdint.h>

namespace keypad {

struct KeyEvent {
    uint8_t button;
    bool pressed;
};

// What the LEDs are allowed to show. Playback fields come from Teensy status.
struct KeypadView {
    uint32_t now_ms;
    uint32_t startup_elapsed_ms;
    bool in_startup;
    bool link_up;
    bool playing;
    bool rumbler_enabled;
    siren::SoundID active_sound;
    bool stop_pulse;
    uint32_t stop_pulse_start_ms;
    uint8_t volume_flash_count;
    uint8_t volume_flash_button[2];
    uint32_t volume_flash_start_ms[2];
};

void begin();
void poll(uint32_t now_ms);
void setView(const KeypadView& view);
void render(uint32_t now_ms);
bool nextEvent(KeyEvent& out);

// Pure color choice. Safe to call before the LED pins are known.
cfg::Rgb colorForButton(uint8_t index, const KeypadView& view);

}  // namespace keypad
