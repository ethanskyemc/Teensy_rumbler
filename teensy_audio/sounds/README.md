# WAV files

Place 44.1 kHz, 16-bit PCM, stereo WAV files in the root of the Audio Shield microSD card.

| Sound | File |
| --- | --- |
| Wail | `/WAIL.WAV` |
| Yelp | `/YELP.WAV` |
| Hi-Lo | `/HILO.WAV` |
| Phaser | `/PHASER.WAV` |
| Piercer | `/PIERCER.WAV` |
| Air horn | `/AIRHORN.WAV` |
| Custom 1 | `/CUSTOM1.WAV` |
| Custom 2 | `/CUSTOM2.WAV` |
| Custom 3 | `/CUSTOM3.WAV` |
| Custom 4 | `/CUSTOM4.WAV` |

Left channel is the main siren. Right channel is the Rumbler. The names live in `common/sounds.h`. A missing file is reported later and must not crash the Teensy. No files are required for the Phase 1 build.
