#include "sound_player.h"

#include "debug.h"
#include "sd_audio.h"
#include "sounds.h"

#include <Audio.h>
#include <Wire.h>

namespace sound_player {
namespace {

// Library default line-out code. About 1.29 V peak-to-peak into the TPA3255.
constexpr uint8_t kLineOutLevel = 29;
constexpr uint8_t kAudioBlocks = 24;

// Audio Shield Rev D, CTRL low. CHIP_ADCDAC_CTRL bit 3 mutes only the right DAC.
constexpr uint8_t kCodecAddress = 0x0A;
constexpr uint16_t kAdcDacCtrl = 0x000E;
constexpr uint16_t kDacMuteRight = 1u << 3;
constexpr uint16_t kDacMuteLeft = 1u << 2;

// WAV left  -> siren gain   -> I2S left  -> line out L -> siren speaker
// WAV right -> rumbler gain -> I2S right -> line out R -> Rumbler
AudioPlaySdWav play_wav_;
AudioAmplifier left_amp_;
AudioAmplifier right_amp_;
AudioOutputI2S i2s_;
AudioConnection left_in_(play_wav_, 0, left_amp_, 0);
AudioConnection right_in_(play_wav_, 1, right_amp_, 0);
AudioConnection left_out_(left_amp_, 0, i2s_, 0);
AudioConnection right_out_(right_amp_, 0, i2s_, 1);
AudioControlSGTL5000 codec_;

bool playing_ = false;
bool codec_ready_ = false;
bool right_muted_ = true;
bool mute_applied_ = false;
float left_level_ = 0.0f;
float right_level_ = 0.0f;
siren::SoundID current_ = siren::SoundID::NONE;

float clampGain(float gain) {
    if (gain < 0.0f) {
        return 0.0f;
    }
    if (gain > 1.0f) {
        return 1.0f;
    }
    return gain;
}

uint16_t readCodec(uint16_t reg) {
    Wire.beginTransmission(kCodecAddress);
    Wire.write(static_cast<uint8_t>(reg >> 8));
    Wire.write(static_cast<uint8_t>(reg));
    if (Wire.endTransmission(false) != 0) {
        return 0xFFFF;
    }
    if (Wire.requestFrom(static_cast<int>(kCodecAddress), 2) < 2) {
        return 0xFFFF;
    }
    const uint16_t hi = static_cast<uint16_t>(Wire.read());
    const uint16_t lo = static_cast<uint16_t>(Wire.read());
    return static_cast<uint16_t>((hi << 8) | lo);
}

bool writeCodec(uint16_t reg, uint16_t value) {
    Wire.beginTransmission(kCodecAddress);
    Wire.write(static_cast<uint8_t>(reg >> 8));
    Wire.write(static_cast<uint8_t>(reg));
    Wire.write(static_cast<uint8_t>(value >> 8));
    Wire.write(static_cast<uint8_t>(value));
    return Wire.endTransmission() == 0;
}

// Bit 3 only. Bit 2, the left DAC mute, is forced clear.
bool writeRightDacMute(bool muted) {
    if (!codec_ready_) {
        return false;
    }
    const uint16_t current = readCodec(kAdcDacCtrl);
    if (current == 0xFFFF) {
        SIREN_LOG("player: right dac read failed\n");
        return false;
    }
    uint16_t next = static_cast<uint16_t>(current & ~kDacMuteLeft);
    if (muted) {
        next = static_cast<uint16_t>(next | kDacMuteRight);
    } else {
        next = static_cast<uint16_t>(next & ~kDacMuteRight);
    }
    if (next == current) {
        return true;
    }
    if (!writeCodec(kAdcDacCtrl, next)) {
        SIREN_LOG("player: right dac write failed\n");
        return false;
    }
    return true;
}

}  // namespace

void begin() {
    playing_ = false;
    codec_ready_ = false;
    right_muted_ = true;
    mute_applied_ = false;
    left_level_ = 0.0f;
    right_level_ = 0.0f;
    current_ = siren::SoundID::NONE;
    AudioMemory(kAudioBlocks);
    left_amp_.gain(0.0f);
    right_amp_.gain(0.0f);

    // enable() blocks once while the SGTL5000 powers up. It is not on the
    // command path. The headphone jack stays muted; the line-out pads feed
    // the amplifier. The right DAC starts muted so the Rumbler cannot play
    // before the first command.
    codec_ready_ = codec_.enable();
    if (!codec_ready_) {
        SIREN_LOG("player: codec enable failed\n");
        return;
    }
    codec_.muteHeadphone();
    codec_.unmuteLineout();
    codec_.lineOutLevel(kLineOutLevel);
    mute_applied_ = writeRightDacMute(true);
    SIREN_LOG("player: codec up, line out level %u, right dac muted\n", kLineOutLevel);
}

void poll() {
    if (!playing_ || !play_wav_.isStopped()) {
        return;
    }
    const siren::SoundID ended = current_;
    if (!siren::soundIsOneShot(ended) && !siren::soundIsMomentary(ended)) {
        if (open(ended, false)) {
            SIREN_LOG_VERBOSE("player: loop %s\n", siren::soundName(ended));
            return;
        }
    }
    playing_ = false;
    current_ = siren::SoundID::NONE;
    SIREN_LOG("player: file ended %s\n", siren::soundName(ended));
}

void stop() {
    play_wav_.stop();
    if (playing_ || current_ != siren::SoundID::NONE) {
        SIREN_LOG("player: stop\n");
    }
    playing_ = false;
    current_ = siren::SoundID::NONE;
}

bool open(siren::SoundID id, bool announce) {
    const char* path = siren::soundFilename(id);
    if (path == nullptr) {
        SIREN_LOG("player: no file for sound %u\n", static_cast<unsigned>(id));
        return false;
    }
    if (!codec_ready_ || sd_audio::status() != siren::SdStatus::OK) {
        SIREN_LOG("player: not ready for %s\n", path);
        return false;
    }
    if (!sd_audio::filePresent(id)) {
        SIREN_LOG("player: missing %s\n", path);
        return false;
    }
    // play() stops the previous file first. A failed open leaves silence.
    if (!play_wav_.play(path)) {
        playing_ = false;
        current_ = siren::SoundID::NONE;
        SIREN_LOG("player: open failed %s\n", path);
        return false;
    }
    playing_ = true;
    current_ = id;
    if (announce) {
        SIREN_LOG("player: play %s\n", path);
    }
    return true;
}

bool playing() {
    return playing_;
}

siren::SoundID current() {
    return current_;
}

bool codecReady() {
    return codec_ready_;
}

void setSirenLevel(float gain) {
    gain = clampGain(gain);
    if (gain == left_level_) {
        return;
    }
    left_level_ = gain;
    left_amp_.gain(gain);
}

void setRumblerLevel(float gain) {
    right_level_ = clampGain(gain);
    if (!right_muted_) {
        right_amp_.gain(right_level_);
    }
}

void setRumblerMuted(bool muted) {
    if (mute_applied_ && muted == right_muted_) {
        return;
    }
    right_muted_ = muted;
    if (muted) {
        const bool wrote = writeRightDacMute(true);
        right_amp_.gain(0.0f);
        mute_applied_ = wrote || !codec_ready_;
        SIREN_LOG("player: rumbler mute %s\n", wrote ? "dac" : "samples only");
        return;
    }
    right_amp_.gain(right_level_);
    const bool wrote = writeRightDacMute(false);
    mute_applied_ = wrote || !codec_ready_;
    SIREN_LOG("player: rumbler open %s\n", wrote ? "dac" : "samples only");
}

}  // namespace sound_player
