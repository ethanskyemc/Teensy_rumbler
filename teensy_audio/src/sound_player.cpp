#include "sound_player.h"

#include "debug.h"
#include "sounds.h"

namespace sound_player {
namespace {

bool playing_ = false;
siren::SoundID current_ = siren::SoundID::NONE;

}  // namespace

void begin() {
    playing_ = false;
    current_ = siren::SoundID::NONE;
    SIREN_LOG("player: wav playback deferred, %u files mapped\n", siren::kStoredSoundCount);
}

void stop() {
    if (playing_ || current_ != siren::SoundID::NONE) {
        SIREN_LOG("player: stop\n");
    }
    playing_ = false;
    current_ = siren::SoundID::NONE;
}

bool open(siren::SoundID id) {
    const char* path = siren::soundFilename(id);
    if (path == 0) {
        SIREN_LOG("player: no file for sound %u\n", static_cast<unsigned>(id));
        return false;
    }
    // Phase 4 opens the WAV. A missing file must not crash; it returns false.
    SIREN_LOG("player: open deferred for %s\n", path);
    return false;
}

bool playing() {
    return playing_;
}

siren::SoundID current() {
    return current_;
}

}  // namespace sound_player
