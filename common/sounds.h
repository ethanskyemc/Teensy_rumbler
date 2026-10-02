#pragma once

// Single map from SoundID to a WAV path. Firmware must not hard-code these
// names anywhere else. Paths are at the SD card root.

#include "commands.h"

namespace siren {

// Siren tones repeat until another tone, ALL STOP, or a failsafe stop
// replaces them. One-shot effects play the file once and then stop.
// Momentary tones play only while held.
inline bool soundIsMomentary(SoundID id) {
    return id == SoundID::AIR_HORN;
}

inline bool soundIsOneShot(SoundID id) {
    return id == SoundID::CUSTOM_1 || id == SoundID::CUSTOM_2 || id == SoundID::CUSTOM_3 ||
           id == SoundID::CUSTOM_4;
}

inline const char* soundName(SoundID id) {
    switch (id) {
        case SoundID::NONE:
            return "NONE";
        case SoundID::WAIL:
            return "WAIL";
        case SoundID::YELP:
            return "YELP";
        case SoundID::HI_LO:
            return "HILO";
        case SoundID::PHASER:
            return "PHASER";
        case SoundID::PIERCER:
            return "PIERCER";
        case SoundID::AIR_HORN:
            return "AIRHORN";
        case SoundID::CUSTOM_1:
            return "MEEPMEP";
        case SoundID::CUSTOM_2:
            return "COIN";
        case SoundID::CUSTOM_3:
            return "1UP";
        case SoundID::CUSTOM_4:
            return "MARIO";
    }
    return "?";
}

inline const char* soundFilename(SoundID id) {
    switch (id) {
        case SoundID::WAIL:
            return "/WAIL.WAV";
        case SoundID::YELP:
            return "/YELP.WAV";
        case SoundID::HI_LO:
            return "/HILO.WAV";
        case SoundID::PHASER:
            return "/PHASER.WAV";
        case SoundID::PIERCER:
            return "/PIERCER.WAV";
        case SoundID::AIR_HORN:
            return "/AIRHORN.WAV";
        case SoundID::CUSTOM_1:
            return "/MEEPMEP.WAV";
        case SoundID::CUSTOM_2:
            return "/COIN.WAV";
        case SoundID::CUSTOM_3:
            return "/1UP.WAV";
        case SoundID::CUSTOM_4:
            return "/MARIO.WAV";
        case SoundID::NONE:
            return 0;
    }
    return 0;
}

constexpr SoundID kStoredSounds[] = {
    SoundID::WAIL,    SoundID::YELP,     SoundID::HI_LO,    SoundID::PHASER,
    SoundID::PIERCER, SoundID::AIR_HORN, SoundID::CUSTOM_1, SoundID::CUSTOM_2,
    SoundID::CUSTOM_3, SoundID::CUSTOM_4,
};

constexpr uint8_t kStoredSoundCount = sizeof(kStoredSounds) / sizeof(kStoredSounds[0]);

}  // namespace siren
