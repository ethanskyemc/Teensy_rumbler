#include "ui.h"

#include "config.h"
#include "debug.h"
#include "keypad.h"
#include "pins.h"
#include "wireless.h"

namespace ui {
namespace {

UiState state_ = {};
uint32_t boot_ms_ = 0;
uint32_t last_status_ms_ = 0;
// Boot default is rumbler off. Flips on each toggle so a press can be sent
// before the first status packet. The screen still waits for that status.
bool commanded_rumbler_ = false;
bool stop_pulse_ = false;
uint32_t stop_pulse_start_ms_ = 0;
bool volume_flash_active_[2] = {};
uint8_t volume_flash_button_[2] = {};
uint32_t volume_flash_start_ms_[2] = {};

void startVolumeFlash(uint8_t button, uint32_t now_ms) {
    for (uint8_t i = 0; i < 2; ++i) {
        if (volume_flash_active_[i] && volume_flash_button_[i] == button) {
            volume_flash_start_ms_[i] = now_ms;
            return;
        }
    }
    for (uint8_t i = 0; i < 2; ++i) {
        if (!volume_flash_active_[i]) {
            volume_flash_active_[i] = true;
            volume_flash_button_[i] = button;
            volume_flash_start_ms_[i] = now_ms;
            return;
        }
    }
}

void applyDefaults() {
    state_ = {};
    state_.link_up = false;
    state_.playing = false;
    state_.rumbler_enabled = false;
    state_.volume_valid = false;
    state_.battery_valid = false;
    state_.sound = siren::SoundID::NONE;
    state_.teensy = siren::TeensyStatus::UNKNOWN;
    state_.sd = siren::SdStatus::UNKNOWN;
}

keypad::KeypadView makeView(uint32_t now_ms) {
    keypad::KeypadView view = {};
    view.now_ms = now_ms;
    view.startup_elapsed_ms = now_ms - boot_ms_;
    view.in_startup = view.startup_elapsed_ms < cfg::kStartupAnimMs;
    view.link_up = state_.link_up;
    view.playing = state_.playing;
    view.rumbler_enabled = state_.rumbler_enabled;
    view.active_sound = state_.sound;
    if (stop_pulse_ && siren::elapsedMs(now_ms, stop_pulse_start_ms_, cfg::kStopPulseMs)) {
        stop_pulse_ = false;
    }
    view.stop_pulse = stop_pulse_;
    view.stop_pulse_start_ms = stop_pulse_start_ms_;
    view.volume_flash_count = 0;
    for (uint8_t i = 0; i < 2; ++i) {
        if (!volume_flash_active_[i]) {
            continue;
        }
        if (siren::elapsedMs(now_ms, volume_flash_start_ms_[i], cfg::kVolumeFlashMs)) {
            volume_flash_active_[i] = false;
            continue;
        }
        const uint8_t slot = view.volume_flash_count;
        view.volume_flash_button[slot] = volume_flash_button_[i];
        view.volume_flash_start_ms[slot] = volume_flash_start_ms_[i];
        view.volume_flash_count = static_cast<uint8_t>(slot + 1u);
    }
    return view;
}

bool statusFresh(uint32_t now_ms) {
    return last_status_ms_ != 0 &&
           !siren::elapsedMs(now_ms, last_status_ms_, siren::kLinkTimeoutMs);
}

int16_t clampVolume(int16_t value) {
    if (value < siren::kVolumeMin) {
        return siren::kVolumeMin;
    }
    if (value > siren::kVolumeMax) {
        return siren::kVolumeMax;
    }
    return value;
}

void noteButton(const keypad::KeyEvent& event, uint32_t now_ms) {
    if (event.button >= cfg::kButtonCount) {
        return;
    }
    const cfg::ButtonBinding& binding = cfg::kButtonBindings[event.button];
    SIREN_LOG("ui: button %u %s\n", event.button, event.pressed ? "down" : "up");

    // Nothing here marks a tone as playing. That bit comes only from Teensy status.
    if (binding.action == cfg::ButtonAction::PLAY_MOMENTARY) {
        wireless::sendCommand(siren::makePlay(0, 0, binding.sound,
                                              event.pressed ? siren::CommandParam::PRESS
                                                            : siren::CommandParam::RELEASE));
        return;
    }
    if (!event.pressed) {
        return;
    }

    switch (binding.action) {
        case cfg::ButtonAction::PLAY_LATCHED:
            if (statusFresh(now_ms) && state_.playing && state_.sound == binding.sound) {
                wireless::sendCommand(siren::makeStop(0, 0));
                break;
            }
            wireless::sendCommand(
                siren::makePlay(0, 0, binding.sound, siren::CommandParam::PRESS));
            break;
        case cfg::ButtonAction::ALL_STOP:
            stop_pulse_ = true;
            stop_pulse_start_ms_ = now_ms;
            wireless::sendCommand(siren::makeStop(0, 0));
            break;
        case cfg::ButtonAction::RUMBLER_TOGGLE:
            commanded_rumbler_ = !commanded_rumbler_;
            wireless::sendCommand(siren::makeRumbler(0, 0, commanded_rumbler_));
            break;
        case cfg::ButtonAction::VOLUME_UP:
        case cfg::ButtonAction::VOLUME_DOWN:
            startVolumeFlash(event.button, now_ms);
            if (!state_.volume_valid || !statusFresh(now_ms)) {
                wireless::sendCommand(siren::makeRequestStatus(0, 0));
                SIREN_LOG("ui: volume waiting for status\n");
                break;
            }
            {
                int16_t next = state_.volume_master;
                if (binding.action == cfg::ButtonAction::VOLUME_UP) {
                    next = static_cast<int16_t>(next + cfg::kVolumeStep);
                } else {
                    next = static_cast<int16_t>(next - cfg::kVolumeStep);
                }
                wireless::sendCommand(siren::makeSetVolume(0, 0, siren::CommandParam::GAIN_MASTER,
                                                           clampVolume(next)));
            }
            break;
        case cfg::ButtonAction::NEXT_BANK:
            SIREN_LOG("ui: next bank has no command yet\n");
            break;
        case cfg::ButtonAction::RESERVED_PA:
            SIREN_LOG("ui: pa reserved\n");
            break;
        case cfg::ButtonAction::PLAY_MOMENTARY:
            break;
    }
}

void applyStatus(const siren::StatusPacket& status, uint32_t now_ms) {
    if (!siren::statusFieldsValid(status)) {
        SIREN_LOG("ui: rejected status packet\n");
        return;
    }
    last_status_ms_ = now_ms;
    state_.playing = status.playing;
    state_.rumbler_enabled = status.rumbler_enabled;
    state_.volume_valid = true;
    state_.volume_master = status.volume_master;
    state_.volume_siren = status.volume_siren;
    state_.volume_rumbler = status.volume_rumbler;
    state_.flags = status.flags;
    state_.sound = status.sound;
    state_.teensy = status.teensy;
    state_.sd = status.sd;
    commanded_rumbler_ = status.rumbler_enabled;
}

}  // namespace

void begin() {
    boot_ms_ = millis();
    last_status_ms_ = 0;
    commanded_rumbler_ = false;
    stop_pulse_ = false;
    volume_flash_active_[0] = false;
    volume_flash_active_[1] = false;
    applyDefaults();
    display::render(state_);
    keypad::setView(makeView(boot_ms_));
}

void update(uint32_t now_ms) {
    keypad::KeyEvent event = {};
    while (keypad::nextEvent(event)) {
        noteButton(event, now_ms);
    }

    siren::StatusPacket status = {};
    while (wireless::takeStatus(status)) {
        applyStatus(status, now_ms);
    }

    const bool status_fresh =
        last_status_ms_ != 0 && !siren::elapsedMs(now_ms, last_status_ms_, siren::kLinkTimeoutMs);
    state_.link_up = status_fresh;
    if (pins::kBatteryAdc < 0) {
        state_.battery_valid = false;
    }

    keypad::setView(makeView(now_ms));
    display::render(state_);
}

const UiState& state() {
    return state_;
}

}  // namespace ui
