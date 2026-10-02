#include "audio_engine.h"

#include "config.h"
#include "debug.h"
#include "rumbler.h"
#include "sound_player.h"

namespace audio_engine {
namespace {

uint8_t master_ = cfg::kDefaultMasterVolume;
uint8_t siren_gain_ = cfg::kDefaultSirenGain;
siren::TeensyStatus status_ = siren::TeensyStatus::BOOT;

uint8_t clampVolume(uint8_t volume) {
    if (volume > siren::kVolumeMax) {
        return static_cast<uint8_t>(siren::kVolumeMax);
    }
    return volume;
}

}  // namespace

void begin() {
    master_ = cfg::kDefaultMasterVolume;
    siren_gain_ = cfg::kDefaultSirenGain;
    status_ = siren::TeensyStatus::BOOT;
    sound_player::begin();
    rumbler::begin();
    SIREN_LOG("audio: silent, master %u siren %u rumbler %u\n", master_, siren_gain_,
              rumbler::gain());
}

void update(uint32_t now_ms) {
    (void)now_ms;
    // Sample generation stays in the audio library's interrupts once playback
    // exists. This update is only for control-plane bookkeeping.
}

void requestPlay(siren::SoundID id, siren::CommandParam edge) {
    SIREN_LOG("audio: play ignored sound=%u edge=%u\n", static_cast<unsigned>(id),
              static_cast<unsigned>(edge));
    (void)sound_player::open(id);
}

void stop() {
    sound_player::stop();
}

void setMasterVolume(uint8_t volume) {
    master_ = clampVolume(volume);
    SIREN_LOG("audio: master %u\n", master_);
}

void setSirenGain(uint8_t gain) {
    siren_gain_ = clampVolume(gain);
    SIREN_LOG("audio: siren gain %u\n", siren_gain_);
}

void setRumblerEnabled(bool enabled) {
    rumbler::setEnabled(enabled);
}

void setRumblerGain(uint8_t gain) {
    rumbler::setGain(clampVolume(gain));
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
