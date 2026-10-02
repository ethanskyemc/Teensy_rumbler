# Siren controller

Wireless controller for a siren speaker and a Federal Signal Rumbler. The remote is an ESP32-S3 with a keypad and a 1.91 inch AMOLED. An ESP32-C3 bridges ESP-NOW to a Teensy 4.0. The Teensy and an Audio Shield Rev D play audio into a TPA3255.

Keypad presses travel ESP-NOW to the C3, then UART to the Teensy. A PLAY command opens a stereo WAV on the Audio Shield. The left line output is the siren. The right line output is the Rumbler, and its DAC stays muted until the Rumbler key enables it. The Teensy sends a status frame after each accepted command. The C3 forwards those bytes, and the keypad LEDs and the AMOLED follow that status.

```
remote_s3/  ESP32-S3 UI
receiver_c3/  ESP32-C3 bridge
teensy_audio/  Teensy 4.0 audio
common/  framed command and status protocol
docs/  architecture, protocol, wiring, audio
```

## Build

Each target is a separate PlatformIO project.

```sh
pio run -d remote_s3
pio run -d receiver_c3
pio run -d teensy_audio
```

The remote is a Waveshare ESP32-S3-AMOLED-1.91 with 16 MB flash. Its 8 MB PSRAM is left off because those pins are the keypad LED bus. The bridge UART is C3 GPIO20 RX and GPIO21 TX. See `docs/wiring.md`.

`SIREN_DEBUG` is 1 in each ini. Set it to 0 for a quiet build. `SIREN_DEBUG_VERBOSE` stays 0 so a 250 ms heartbeat cannot flood the serial log.

## What is live

- Commands and status use explicit little-endian frames with CRC-16/CCITT-FALSE. See `docs/protocol.md`.
- Each firmware runs that frame self-test once at boot.
- The remote reads the keypad over I2C, debounces edges, and sends PLAY, STOP, volume, rumbler, and a 250 ms heartbeat. With `kPeerMac` all zeros it sends to the ESP-NOW broadcast address.
- The C3 checks sequence and epoch, forwards the original frame on UART, and writes a failsafe STOP if heartbeats stop while a tone was commanded.
- The Teensy parses Serial1, prints the command, and plays `/WAIL.WAV` and the other names in `common/sounds.h`. STOP, a momentary release, a missing file, and a 1 second UART gap while audio is playing all go silent.
- The Teensy replies with status on every accepted command, including the 250 ms heartbeat, and again when playback changes on its own. The remote treats status younger than 1 second as the link. Volume steps use the master level in that packet. Battery stays `--`.
- A queued ESP-NOW send is not a Teensy acknowledgement. The screen and the latched-key LED update when the status packet arrives.

## Hardware still needed

How the Teensy, C3, remote, and amplifier are powered. The audio path uses the shield line-out pads.
