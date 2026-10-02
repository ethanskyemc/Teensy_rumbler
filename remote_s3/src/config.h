#pragma once

// Remote behavior that should be editable without hunting through the UI.
// Playback state shown on the display still comes from Teensy status, not
// from these bindings.

#include "commands.h"

namespace cfg {

constexpr uint8_t kButtonCount = 16;

// Absolute master-volume step once a fresh status packet has reported the
// current level. Siren and Rumbler trims have protocol fields and no buttons yet.
constexpr uint8_t kVolumeStep = 5;

// One chase across the 16 keys, then the keypad follows link/playback state.
constexpr uint32_t kStartupAnimMs = 800;

// Slow link-lost pulse: brighter for this long, once per period.
constexpr uint32_t kLinkLostPeriodMs = 1000;
constexpr uint32_t kLinkLostBrightMs = 180;

// Hue cycle on the key whose sound the Teensy says is playing.
constexpr uint32_t kRainbowPeriodMs = 1400;
constexpr uint32_t kRainbowSteps = 28;

// All Stop sends one red pulse across every other key, in keypad order.
constexpr uint32_t kStopPulseStepMs = 48;
constexpr uint32_t kStopPulseWidthMs = 110;
constexpr uint32_t kStopPulseMs = (kButtonCount - 1) * kStopPulseStepMs + kStopPulseWidthMs;

// Volume keys brighten, then fade back to their resting green.
constexpr uint32_t kVolumeFlashMs = 320;

// Receiver ESP32-C3 STA MAC. All zeros sends to the ESP-NOW broadcast address.
constexpr uint8_t kPeerMac[6] = {0, 0, 0, 0, 0, 0};

// Keypad button scan. LED brightness is the Pimoroni half-scale default.
constexpr uint32_t kButtonSampleMs = 10;
constexpr uint32_t kButtonDebounceMs = 20;

enum class ButtonAction : uint8_t {
    PLAY_LATCHED = 0,
    PLAY_MOMENTARY,
    VOLUME_DOWN,
    VOLUME_UP,
    RUMBLER_TOGGLE,
    NEXT_BANK,
    RESERVED_PA,
    ALL_STOP,
};

struct ButtonBinding {
    ButtonAction action;
    siren::SoundID sound;
};

// Keypad index matches the Pimoroni RGB Keypad legend: 0-9 then A-F.
constexpr ButtonBinding kButtonBindings[kButtonCount] = {
    {ButtonAction::PLAY_MOMENTARY, siren::SoundID::AIR_HORN},  // 0
    {ButtonAction::PLAY_LATCHED, siren::SoundID::WAIL},        // 1
    {ButtonAction::PLAY_LATCHED, siren::SoundID::YELP},        // 2
    {ButtonAction::PLAY_LATCHED, siren::SoundID::PHASER},      // 3
    {ButtonAction::PLAY_LATCHED, siren::SoundID::HI_LO},       // 4
    {ButtonAction::PLAY_LATCHED, siren::SoundID::PIERCER},     // 5
    {ButtonAction::PLAY_LATCHED, siren::SoundID::CUSTOM_1},    // 6
    {ButtonAction::PLAY_LATCHED, siren::SoundID::CUSTOM_2},    // 7
    {ButtonAction::PLAY_LATCHED, siren::SoundID::CUSTOM_3},    // 8
    {ButtonAction::PLAY_LATCHED, siren::SoundID::CUSTOM_4},    // 9
    {ButtonAction::RESERVED_PA, siren::SoundID::NONE},         // A
    {ButtonAction::ALL_STOP, siren::SoundID::NONE},            // B
    {ButtonAction::RUMBLER_TOGGLE, siren::SoundID::NONE},      // C
    {ButtonAction::VOLUME_DOWN, siren::SoundID::NONE},         // D
    {ButtonAction::VOLUME_UP, siren::SoundID::NONE},           // E
    {ButtonAction::NEXT_BANK, siren::SoundID::NONE},           // F
};

static_assert(kButtonBindings[0].action == ButtonAction::PLAY_MOMENTARY, "air horn");
static_assert(kButtonBindings[1].sound == siren::SoundID::WAIL, "button 1");
static_assert(kButtonBindings[4].sound == siren::SoundID::HI_LO, "button 4");
static_assert(kButtonBindings[10].action == ButtonAction::RESERVED_PA, "pa");
static_assert(kButtonBindings[11].action == ButtonAction::ALL_STOP, "all stop");
static_assert(kButtonBindings[12].action == ButtonAction::RUMBLER_TOGGLE, "rumbler");
static_assert(kButtonBindings[15].action == ButtonAction::NEXT_BANK, "next bank");

struct Rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

constexpr Rgb kRgbOff = {0, 0, 0};
constexpr Rgb kRgbLatched = {0, 0, 12};
constexpr Rgb kRgbMomentary = {36, 4, 24};
constexpr Rgb kRgbStop = {36, 0, 0};
constexpr Rgb kRgbStopPulse = {180, 0, 0};
constexpr Rgb kRgbVolume = {0, 28, 0};
constexpr Rgb kRgbVolumeFlash = {0, 160, 0};
constexpr Rgb kRgbRumblerOff = {18, 4, 0};
constexpr Rgb kRgbRumblerOn = {150, 36, 0};
constexpr Rgb kRgbLinkLostDim = {28, 12, 0};
constexpr Rgb kRgbLinkLostBright = {90, 36, 0};
constexpr Rgb kRgbStartup = {0, 48, 90};

}  // namespace cfg
