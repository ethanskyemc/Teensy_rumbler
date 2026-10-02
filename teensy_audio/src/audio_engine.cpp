#include "audio_engine.h"

#include "config.h"
#include "debug.h"
#include "rumbler.h"
#include "sound_player.h"
#include "sounds.h"

namespace audio_engine {
namespace {

uint8_t master_ = cfg::kDefaultMasterVolume;
uint8_t siren_gain_ = cfg::kDefaultSirenGain;
uint8_t applied_left_ = 255;
uint8_t applied_right_ = 255;
bool applied_mute_ = false;
bool gains_applied_ = false;
siren::TeensyStatus status_ = siren::TeensyStatus::BOOT;

uint8_t clampVolume(uint8_t volume) {
    if (volume > siren::kVolumeMax) {
        return static_cast<uint8_t>(siren::kVolumeMax);
    }
    return volume;
}

uint8_t scaled(uint8_t master, uint8_t channel) {
    return static_cast<uint8_t>((static_cast<uint16_t>(master) * channel) / 100u);
}

void applyGains() {
    const uint8_t left = scaled(master_, siren_gain_);
    const uint8_t right = scaled(master_, rumbler::gain());
    const bool muted = !rumbler::enabled();
    if (gains_applied_ && left == applied_left_ && right == applied_right_ && muted == applied_mute_) {
        return;
    }
    applied_left_ = left;
    applied_right_ = right;
    applied_mute_ = muted;
    gains_applied_ = true;
    // Level first, then the mute, so enabling uses the stored right level
    // and disabling never writes that level onto the live right channel.
    sound_player::setSirenLevel(static_cast<float>(left) / 100.0f);
    sound_player::setRumblerLevel(static_cast<float>(right) / 100.0f);
    sound_player::setRumblerMuted(muted);
    SIREN_LOG("audio: siren %u rumbler %u %s\n", left, right, muted ? "muted" : "open");
}

}  // namespace

void begin() {
    master_ = cfg::kDefaultMasterVolume;
    siren_gain_ = cfg::kDefaultSirenGain;
    applied_left_ = 255;
    applied_right_ = 255;
    applied_mute_ = true;
    gains_applied_ = false;
    status_ = siren::TeensyStatus::BOOT;
    sound_player::begin();
    rumbler::begin();
    status_ = sound_player::codecReady() ? siren::TeensyStatus::READY : siren::TeensyStatus::FAULT;
    applyGains();
    SIREN_LOG("audio: %s, master %u siren %u rumbler %u\n",
              status_ == siren::TeensyStatus::READY ? "ready" : "fault", master_, siren_gain_,
              rumbler::gain());
}

void update(uint32_t now_ms) {
    (void)now_ms;
    sound_player::poll();
}

void requestPlay(siren::SoundID id, siren::CommandParam edge) {
    if (edge == siren::CommandParam::RELEASE) {
        if (siren::soundIsMomentary(id) && sound_player::current() == id) {
            stop();
        }
        return;
    }
    if (edge != siren::CommandParam::PRESS) {
        return;
    }
    if (!sound_player::open(id)) {
        stop();
        return;
    }
}

void stop() {
    sound_player::stop();
}

void setMasterVolume(uint8_t volume) {
    master_ = clampVolume(volume);
    SIREN_LOG("audio: master %u\n", master_);
    applyGains();
}

void setSirenGain(uint8_t gain) {
    siren_gain_ = clampVolume(gain);
    SIREN_LOG("audio: siren gain %u\n", siren_gain_);
    applyGains();
}

void setRumblerEnabled(bool enabled) {
    rumbler::setEnabled(enabled);
    applyGains();
}

void setRumblerGain(uint8_t gain) {
    rumbler::setGain(clampVolume(gain));
    applyGains();
}

bool playing() {
    return sound_player::playing();
}

siren::SoundID currentSound() {
    return sound_player::current();
}

uint8_t masterVolume() {
    return master_;
}

uint8_t sirenGain() {
    return siren_gain_;
}

uint8_t rumblerGain() {
    return rumbler::gain();
}

bool rumblerEnabled() {
    return rumbler::enabled();
}

siren::TeensyStatus deviceStatus() {
    return status_;
}

}  // namespace audio_engine
