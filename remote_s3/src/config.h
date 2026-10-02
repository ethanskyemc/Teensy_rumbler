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

// Receiver ESP32-C3 STA MAC. All zeros sends to the ESP-NOW broadcast address.
constexpr uint8_t kPeerMac[6] = {0, 0, 0, 0, 0, 0};

// Keypad button scan. The LED strip stays off until its driver is written.
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
    {ButtonAction::PLAY_LATCHED, siren::SoundID::WAIL},          // 0
    {ButtonAction::PLAY_LATCHED, siren::SoundID::YELP},          // 1
    {ButtonAction::PLAY_LATCHED, siren::SoundID::PHASER},        // 2
    {ButtonAction::PLAY_LATCHED, siren::SoundID::HI_LO},         // 3
    {ButtonAction::PLAY_LATCHED, siren::SoundID::PIERCER},       // 4
    {ButtonAction::PLAY_MOMENTARY, siren::SoundID::AIR_HORN},    // 5
    {ButtonAction::PLAY_LATCHED, siren::SoundID::CUSTOM_1},      // 6
    {ButtonAction::PLAY_LATCHED, siren::SoundID::CUSTOM_2},      // 7
    {ButtonAction::VOLUME_DOWN, siren::SoundID::NONE},           // 8
    {ButtonAction::VOLUME_UP, siren::SoundID::NONE},             // 9
    {ButtonAction::RUMBLER_TOGGLE, siren::SoundID::NONE},        // A
    {ButtonAction::NEXT_BANK, siren::SoundID::NONE},             // B
    {ButtonAction::RESERVED_PA, siren::SoundID::NONE},           // C
    {ButtonAction::PLAY_LATCHED, siren::SoundID::CUSTOM_3},      // D
    {ButtonAction::PLAY_LATCHED, siren::SoundID::CUSTOM_4},      // E
    {ButtonAction::ALL_STOP, siren::SoundID::NONE},              // F
};

static_assert(kButtonBindings[0].sound == siren::SoundID::WAIL, "button 0");
static_assert(kButtonBindings[2].sound == siren::SoundID::PHASER, "button 2");
static_assert(kButtonBindings[3].sound == siren::SoundID::HI_LO, "button 3");
static_assert(kButtonBindings[5].action == ButtonAction::PLAY_MOMENTARY, "air horn");
static_assert(kButtonBindings[10].action == ButtonAction::RUMBLER_TOGGLE, "rumbler");
static_assert(kButtonBindings[12].action == ButtonAction::RESERVED_PA, "pa");
static_assert(kButtonBindings[15].action == ButtonAction::ALL_STOP, "all stop");

struct Rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

constexpr Rgb kRgbIdle = {0, 0, 12};
constexpr Rgb kRgbActive = {0, 140, 32};
constexpr Rgb kRgbAirHorn = {160, 110, 0};
constexpr Rgb kRgbRumblerOn = {150, 36, 0};
constexpr Rgb kRgbLinkLostDim = {28, 12, 0};
constexpr Rgb kRgbLinkLostBright = {90, 36, 0};
constexpr Rgb kRgbStartup = {0, 48, 90};

}  // namespace cfg
