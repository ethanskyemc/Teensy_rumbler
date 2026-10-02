#pragma once

#include "commands.h"

namespace sound_player {

void begin();
void poll();
void stop();
bool open(siren::SoundID id, bool announce = true);
bool playing();
siren::SoundID current();
bool codecReady();

// Left line output only. 0 is silent, 1 is full scale. Does not touch the right channel.
void setSirenLevel(float gain);

// Right line output level used when the Rumbler is unmuted. 0 is silent, 1 is full scale.
void setRumblerLevel(float gain);

// Mutes only the right DAC and the right-channel samples. The left channel is left as it is.
void setRumblerMuted(bool muted);

}  // namespace sound_player
