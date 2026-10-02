# Audio

The Teensy 4.0 and the Audio Shield Rev D own every sample. A PLAY press opens one stereo WAV from the microSD card and sends it to the SGTL5000 line outputs. The headphone jack stays muted. Boot, a missing file, and ALL STOP leave both outputs silent.

## Format

Initial files are 44.1 kHz, 16-bit PCM, stereo WAV. The directory `teensy_audio/sounds/` lists the filenames. The only map in firmware is `soundFilename()` in `common/sounds.h`.

Left is the siren speaker. Right is the Rumbler. Two playback styles need to be able to share the engine later:

1. A pre-rendered stereo WAV, with the Rumbler already in the right channel.
2. A generated or processed right channel. Not in this phase.

One `AudioPlaySdWav` plays at a time. Wail, yelp, phaser, hi-lo, and piercer start the file again when it ends, so the tone holds until another sound, All Stop, or the failsafe. Meep meep, coin, 1-up, and Mario play once and then stop. Air horn replaces the current file too, and release stops playback. The previous file is not resumed. A future mixer can layer the horn without changing the command path.

## Rumbler mute

Rumbler enable is independent of which WAV is open and of the left-channel gain. Disabled mutes the SGTL5000 right DAC and zeros the right-channel samples. The left DAC mute bit is left clear. The flag boots disabled, so the Rumbler is muted before the first command. Toggling it does not restart the WAV.

## Gains

Levels are application units, 0 to 100. Channel gain is `(master * channel) / 100`, then divided by 100 again to reach the 0.0–1.0 audio-library gain. The codec line-out level stays at the library default, about 1.29 V peak-to-peak. Defaults:

| Control | Default |
| --- | --- |
| Master | 30 |
| Siren | 50 |
| Rumbler | 30 |

They are low on purpose because a TPA3255 follows the shield. Siren gain scales only the left output. Rumbler gain scales only the right output, and only while the Rumbler is enabled. With the defaults the siren is 15% of full scale. The amplifier is wired to the line-out pads.

## Failure

A missing file, a failed SD mount, a bad command, and a link timeout do not start audio and do not reset the board. Mount failure is `SdStatus::MOUNT_FAILED`. A mounted card with a missing name stays `SdStatus::OK` and the player logs that name. While the codec is up, the status snapshot reports Teensy `READY`.
