#pragma once

#include "commands.h"

namespace sd_audio {

void begin();
siren::SdStatus status();
bool filePresent(siren::SoundID id);

}  // namespace sd_audio
