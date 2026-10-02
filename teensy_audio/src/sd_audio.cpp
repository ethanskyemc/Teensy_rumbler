#include "sd_audio.h"

#include "debug.h"
#include "sounds.h"

namespace sd_audio {
namespace {

siren::SdStatus status_ = siren::SdStatus::UNKNOWN;

}  // namespace

void begin() {
    status_ = siren::SdStatus::UNKNOWN;
    SIREN_LOG("sd: mount deferred, %u expected files, status unknown\n", siren::kStoredSoundCount);
}

siren::SdStatus status() {
    return status_;
}

bool filePresent(siren::SoundID id) {
    (void)id;
    return false;
}

}  // namespace sd_audio
