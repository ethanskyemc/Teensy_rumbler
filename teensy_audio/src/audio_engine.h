#pragma once

#include "commands.h"

#include <stdint.h>

namespace audio_engine {

void begin();
void update(uint32_t now_ms);

// Control requests. Samples are generated in the audio library interrupt.
void requestPlay(siren::SoundID id, siren::CommandParam edge);
void stop();
void setMasterVolume(uint8_t volume);
void setSirenGain(uint8_t gain);
void setRumblerEnabled(bool enabled);
void setRumblerGain(uint8_t gain);

bool playing();
siren::SoundID currentSound();
uint8_t masterVolume();
uint8_t sirenGain();
uint8_t rumblerGain();
bool rumblerEnabled();
siren::TeensyStatus deviceStatus();

}  // namespace audio_engine
