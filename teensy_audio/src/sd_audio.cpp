#include "sd_audio.h"

#include "debug.h"
#include "pins.h"
#include "sounds.h"

#include <SD.h>
#include <SPI.h>

namespace sd_audio {
namespace {

siren::SdStatus status_ = siren::SdStatus::UNKNOWN;

}  // namespace

void begin() {
    status_ = siren::SdStatus::UNKNOWN;
    SPI.setMOSI(pins::kSdMosi);
    SPI.setMISO(pins::kSdMiso);
    SPI.setSCK(pins::kSdSck);
    if (!SD.begin(pins::kSdCs)) {
        status_ = siren::SdStatus::MOUNT_FAILED;
        SIREN_LOG("sd: mount failed cs=%u\n", pins::kSdCs);
        return;
    }
    status_ = siren::SdStatus::OK;

    uint8_t missing = 0;
    for (uint8_t i = 0; i < siren::kStoredSoundCount; ++i) {
        const char* path = siren::soundFilename(siren::kStoredSounds[i]);
        if (path == nullptr || SD.exists(path)) {
            continue;
        }
        missing = static_cast<uint8_t>(missing + 1u);
        SIREN_LOG("sd: missing %s\n", path);
    }
    SIREN_LOG("sd: mounted, %u of %u files missing\n", missing, siren::kStoredSoundCount);
}

siren::SdStatus status() {
    return status_;
}

bool filePresent(siren::SoundID id) {
    if (status_ != siren::SdStatus::OK) {
        return false;
    }
    const char* path = siren::soundFilename(id);
    if (path == nullptr) {
        return false;
    }
    return SD.exists(path);
}

}  // namespace sd_audio
