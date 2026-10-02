# Audio

The Teensy 4.0 and the Audio Shield Rev D own every sample. Phase 1 does not open the codec, the SD card, or a WAV file. The engine still has a place for each later piece so playback, the Rumbler mute, and a future synthesizer do not get mixed into the radio code.

## Format

Initial files are 44.1 kHz, 16-bit PCM, stereo WAV. The directory `teensy_audio/sounds/` lists the filenames. The only map in firmware is `soundFilename()` in `common/sounds.h`.

Left is the siren speaker. Right is the Rumbler. Two playback styles need to be able to share the engine later:

1. A pre-rendered stereo WAV, with the Rumbler already in the right channel.
2. A generated or processed right channel. Not in this phase.

WAV playback is the only path that will be implemented first. The engine API is `requestPlay`, `stop`, and the three gain setters. `requestPlay` currently ignores the request and stays silent.

## Rumbler mute

Rumbler enable is independent of which WAV is open. Disabled means the right output is muted and the left output is left alone. The flag boots disabled. Enabling it before the codec is up only stores the flag.

## Gains

Levels are application units, 0 to 100. Defaults, stored and not written to the codec:

| Control | Default |
| --- | --- |
| Master | 30 |
| Siren | 50 |
| Rumbler | 30 |

They are low on purpose because a TPA3255 follows the shield. The amplifier is wired to the line-out pads, so the codec path is the SGTL5000 line outputs. The headphone jack is unused. The gain curve is part of the playback phase.

## Failure

A missing file, a failed SD mount, a bad command, and a link timeout do not start audio and do not reset the board. `sd_audio` reports `SdStatus::UNKNOWN` until mount is implemented. The boot status packet template is stopped, sound `NONE`, Teensy `BOOT`.
