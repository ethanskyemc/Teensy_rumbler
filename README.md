# Siren controller

Wireless controller for a siren speaker and a Federal Signal Rumbler. The remote is an ESP32-S3 with a keypad and a 1.91 inch AMOLED. An ESP32-C3 bridges ESP-NOW to a Teensy 4.0. The Teensy and an Audio Shield Rev D play audio into a TPA3255.

Phases 2 and 3 are in the tree: keypad presses travel ESP-NOW to the C3, then UART to the Teensy, which prints them on USB serial. WAV playback, the LED strip, and the AMOLED are still off, so the outputs stay silent.

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

The remote is a Waveshare ESP32-S3-AMOLED-1.91 with 16 MB flash and 8 MB PSRAM. The bridge UART is C3 GPIO20 RX and GPIO21 TX. See `docs/wiring.md`.

`SIREN_DEBUG` is 1 in each ini. Set it to 0 for a quiet build. `SIREN_DEBUG_VERBOSE` stays 0 so a 250 ms heartbeat cannot flood the serial log.

## What is live

- Commands and status use explicit little-endian frames with CRC-16/CCITT-FALSE. See `docs/protocol.md`.
- Each firmware runs that frame self-test once at boot.
- The remote reads the keypad over I2C, debounces edges, and sends PLAY, STOP, volume, rumbler, and a 250 ms heartbeat. With `kPeerMac` all zeros it sends to the ESP-NOW broadcast address.
- The C3 checks sequence and epoch, forwards the original frame on UART, and writes a failsafe STOP if heartbeats stop while a tone was commanded.
- The Teensy parses Serial1 and prints accepted and rejected commands on USB serial. STOP is applied as silence. PLAY does not open a WAV yet.
- Volume buttons send `REQUEST_STATUS` until a status packet exists. Status return, keypad LEDs, and the AMOLED are later phases.
- A queued ESP-NOW send is not a Teensy acknowledgement. The command is real when the Teensy log shows it.

## Hardware still needed

How the Teensy, C3, remote, and amplifier are powered. The audio path uses the shield line-out pads.
