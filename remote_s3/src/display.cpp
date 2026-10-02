#include "display.h"

#include "debug.h"
#include "pins.h"

namespace display {
namespace {

UiState last_ = {};
bool have_last_ = false;

bool sameState(const UiState& a, const UiState& b) {
    return a.link_up == b.link_up && a.playing == b.playing &&
           a.rumbler_enabled == b.rumbler_enabled && a.volume_valid == b.volume_valid &&
           a.battery_valid == b.battery_valid && a.volume_master == b.volume_master &&
           a.volume_siren == b.volume_siren && a.volume_rumbler == b.volume_rumbler &&
           a.battery_percent == b.battery_percent && a.flags == b.flags && a.sound == b.sound &&
           a.teensy == b.teensy && a.sd == b.sd;
}

}  // namespace

void begin() {
    have_last_ = false;
    // SH8601 QSPI pins are in pins.h. The panel is not initialized yet.
    if (!pins::kDisplayDriverReady) {
        SIREN_LOG("display: SH8601 pins assigned, panel not started\n");
    }
}

void render(const UiState& state) {
    if (have_last_ && sameState(last_, state)) {
        return;
    }
    last_ = state;
    have_last_ = true;
    SIREN_LOG(
        "display: link=%u sound=%u playing=%u rumbler=%u vol_valid=%u vol=%u teensy=%u sd=%u\n",
        state.link_up ? 1u : 0u, static_cast<unsigned>(state.sound), state.playing ? 1u : 0u,
        state.rumbler_enabled ? 1u : 0u, state.volume_valid ? 1u : 0u, state.volume_master,
        static_cast<unsigned>(state.teensy), static_cast<unsigned>(state.sd));
    if (!pins::kDisplayDriverReady) {
        return;
    }
    // TODO(hardware): paint connection, sound name, Rumbler, volume,
    // battery, and Teensy/SD status from `state` only.
}

}  // namespace display
