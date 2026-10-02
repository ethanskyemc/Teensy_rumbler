# Architecture

Three processors share one job split. The remote never generates audio. The bridge never generates audio. The Teensy owns playback, gains, and silence.

```
Remote ESP32-S3                ESP32-C3 bridge              Teensy 4.0
keypad, LEDs, display    ESP-NOW     validate and       UART     WAV playback
button map, heartbeat  <------->     forward frames   <------>   mix, mute, gains
status display           same frame bytes both ways              Audio Shield
                                                                  LINE OUT L  siren
                                                                  LINE OUT R  Rumbler
                                                                       |
                                                                   TPA3255
```

Status travels the reverse path: Teensy, UART, C3, ESP-NOW, remote. The screen and the latched-siren LEDs follow that status. They do not follow the last button press.

## Responsibilities

| Processor | Owns | Does not own |
| --- | --- | --- |
| ESP32-S3 remote | Keys, RGB feedback, display, ESP-NOW commands, heartbeat, drawing acknowledged status | WAV files, mixing, DSP, the decision that audio is actually playing |
| ESP32-C3 bridge | ESP-NOW validate/forward, UART framing, heartbeat timeout, failsafe STOP | Audio synthesis, SD, a second copy of the mixer |
| Teensy 4.0 | SD, WAV playback, master/siren/Rumbler gains, right-channel mute, local silence if UART dies | Keypad, display, ESP-NOW |

The C3 keeps one failsafe hint, set when it forwards a PLAY press and cleared when it forwards STOP or a momentary release. That bit exists so a lost heartbeat can force STOP. It is not an audio engine. Phase 7 status will replace the hint.

## Software layout

Shared wire types live in `common/`. Each target is its own PlatformIO project and includes `common/` with `-I`.

- `remote_s3`: `keypad`, `display`, `wireless`, `ui`
- `receiver_c3`: `espnow`, `uart_bridge`
- `teensy_audio`: `command_handler`, `audio_engine`, `sound_player`, `rumbler`, `sd_audio`

`ui` passes a `UiState` into `display::render`. LED colors are chosen by `keypad::colorForButton` from a `KeypadView`. The remote pin map is in `remote_s3/src/pins.h`. The C3 link is UART0 on GPIO20 (RX) and GPIO21 (TX).

Button actions are a table in `remote_s3/src/config.h`. Sound file names are a table in `common/sounds.h`.

## Silence and failsafe

Boot, reset, a malformed command, a stale sequence, and a missing WAV all leave the outputs silent. Nothing starts a tone by itself.

Two timers use the same 1 second limit:

1. The C3 sends STOP if audio is active and no valid remote heartbeat has arrived for 1 second. The remote heartbeat period is 250 ms.
2. The Teensy stops itself in `command_handler::poll` if the UART goes quiet for 1 second while the audio engine reports playback. Until a WAV is actually playing, that check has nothing to stop.

ALL STOP wins over every other command once it is accepted. Acceptance still requires a valid frame, a locked epoch, and a newer sequence, so an old STOP or an old PLAY cannot be replayed into a new session.

A heartbeat is the only packet that may lock a remote epoch. PLAY before that heartbeat is rejected. After the remote reboots, its new epoch is locked by a heartbeat, and a PLAY from the previous epoch is rejected.

## Control vs audio

`loop()` on every target is a non-blocking poll. The Teensy splits that poll into `command_handler` and `audio_engine`. When WAV playback arrives, samples stay in the Teensy Audio library interrupts. The command path only starts, stops, and sets gains.

## Phase plan

| Phase | Goal | State |
| --- | --- | --- |
| 1 | Tree, shared protocol, docs, three projects that compile | Done |
| 2 | C3 UART to Teensy, commands printed on Teensy USB serial | In this tree, not yet run on hardware |
| 3 | ESP-NOW from keypad events through to the Teensy log | In this tree, not yet run on hardware |
| 4 | SD WAV playback | Not started |
| 5 | Stereo routing and independent Rumbler mute | Not started |
| 6 | Keypad LEDs and AMOLED UI | Not started |
| 7 | Status and acknowledgement both directions | Not started |
| 8 | Diagnostics, banks, more sounds | Not started |

## Out of scope

PA audio, microphone input, CAN, Wi-Fi networks, web servers, Bluetooth, and cloud services are not part of this firmware. The keypad reserves one key for a future PA mode. ESP-NOW uses the Wi-Fi radio in station mode and does not join an access point.

## Libraries

The firmware links the PlatformIO Arduino cores (ESP32 Arduino and Teensyduino). The keypad button expander is read over I2C. The LED SPI strip and the SH8601 panel are not started. Later audio phases use the PJRC Audio library and SD support that ship with Teensyduino.

Debug logs are compiled in with `SIREN_DEBUG` in each `platformio.ini`. `SIREN_DEBUG_VERBOSE` stays off so heartbeats are not printed every 250 ms.
