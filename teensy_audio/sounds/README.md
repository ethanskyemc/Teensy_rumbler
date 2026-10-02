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

Left channel is the main siren. Right channel is the Rumbler. The names live in `common/sounds.h`. A missing file is logged and playback stays silent. The card is not required for the firmware to boot.
