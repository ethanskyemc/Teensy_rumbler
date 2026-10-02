#include "rumbler.h"

#include "config.h"
#include "debug.h"

namespace rumbler {
namespace {

bool enabled_ = false;
uint8_t gain_ = cfg::kDefaultRumblerGain;

}  // namespace

void begin() {
    enabled_ = false;
    gain_ = cfg::kDefaultRumblerGain;
    SIREN_LOG("rumbler: disabled, gain %u\n", gain_);
}

void setEnabled(bool enabled) {
    if (enabled_ == enabled) {
        return;
    }
    enabled_ = enabled;
    // Disabled mutes the right DAC. The left siren channel is not part of this flag.
    SIREN_LOG("rumbler: %s\n", enabled_ ? "enabled" : "disabled");
}

void setGain(uint8_t gain) {
    gain_ = gain;
    SIREN_LOG("rumbler: gain %u\n", gain_);
}

bool enabled() {
    return enabled_;
}

uint8_t gain() {
    return gain_;
}

}  // namespace rumbler
