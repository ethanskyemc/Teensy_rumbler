#pragma once

#include "commands.h"

namespace sound_player {

void begin();
void stop();
bool open(siren::SoundID id);
bool playing();
siren::SoundID current();

}  // namespace sound_player
