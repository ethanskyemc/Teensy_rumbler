# Wiring

Confirmed connections are listed below. System power is the remaining open item.

## Confirmed path

```
ESP32-S3 remote
    ESP-NOW
ESP32-C3 bridge
    UART 115200 8N1, 3.3 V, common ground
    C3 GPIO21 TX -> Teensy pin 0 RX1
    C3 GPIO20 RX <- Teensy pin 1 TX1
Teensy 4.0 Audio Shield Rev D line-out pads
    LEFT  -> TPA3255 channel 1 -> siren speaker
    RIGHT -> TPA3255 channel 2 -> Rumbler
```

The headphone jack is not in this path. Debug serial on the C3 is USB, so it does not share GPIO20 and GPIO21. Teensy 4.0 and the ESP32-C3 are both 3.3 V on this UART.

## Audio Shield Rev D, stacked on Teensy 4.0

These are the PJRC shield's stacking pins, recorded in `teensy_audio/src/pins.h`. Serial1 on pins 0 and 1 is free of that stack.

| Function | Teensy pin |
| --- | --- |
| Serial1 RX from C3 | 0 |
| Serial1 TX to C3 | 1 |
| I2S TX to codec | 7 |
| I2S RX from codec | 8 |
| SD CS | 10 |
| SD MOSI | 11 |
| SD MISO | 12 |
| SD SCK | 13 |
| Volume potentiometer | 15 |
| Codec SDA | 18 |
| Codec SCL | 19 |
| I2S LRCLK | 20 |
| I2S BCLK | 21 |
| I2S MCLK | 23 |

The potentiometer is not the siren volume control. Master, siren, and Rumbler levels come from the protocol. The TPA3255 inputs are the shield's line-out pads: left is the siren channel, right is the Rumbler. The 3.5 mm headphone jack is unused, so the codec is set up on its line outputs when playback is added.

Power for the Teensy, the C3, the remote, and the amplifier is not documented yet.

## Remote

The remote is a Waveshare ESP32-S3-AMOLED-1.91. The 1.91 inch panel is on that board. The 40-pin header uses Raspberry Pi Pico pin numbers. An ESP32 GPIO on header pin 6 is not Pico GP6.

### Pimoroni Pico RGB Keypad

From `pimoroni-pico` `libraries/pico_rgb_keypad/pico_rgb_keypad.cpp`. The Pico GPIO numbers below are the keypad's own wiring. The ESP32 column is the Waveshare GPIO on that same header pin.

| Function | Pico GPIO | Header pin | ESP32-S3 GPIO |
| --- | --- | --- | --- |
| Button SDA | GP4 | 6 | GPIO2 |
| Button SCL | GP5 | 7 | GPIO3 |
| LED CS | GP17 | 22 | GPIO35 |
| LED SCK | GP18 | 24 | GPIO36 |
| LED MOSI | GP19 | 25 | GPIO37 |

Buttons are an I2C expander at address `0x20`, 400 kHz. The Pico library writes register 0, reads two bytes, and inverts the result so a pressed key is a 1.

The 16 RGB LEDs use SPI at 4 MHz with an APA102/SK9822 frame: 4 bytes of start, then 16 groups of brightness, blue, green, red, then end bytes. CS frames each transfer. That SPI host must stay separate from the panel QSPI.

GPIO3 is an ESP32-S3 strap pin (JTAG source). The keypad SCL pull-up holds it high, which is the normal USB-serial boot state.

### AMOLED

QSPI, driver SH8601. The glass is 240 by 536; the Waveshare init addresses it as 536 by 240. Pins match Waveshare's LVGL example and the display-control table.

| Signal | ESP32-S3 GPIO |
| --- | --- |
| QSPI CS | GPIO6 |
| QSPI SCK | GPIO47 |
| QSPI D0 | GPIO18 |
| QSPI D1 | GPIO7 |
| QSPI D2 | GPIO48 |
| QSPI D3 | GPIO5 |
| Reset | GPIO17 |
| Backlight | none |

Brightness is SH8601 register `0x51`, not a GPIO. Reset is also broken out on header pin 35. The keypad does not use that pin.

Touch is an FT3168 at I2C address `0x38` on GPIO40 (SDA) and GPIO39 (SCL), shared with the onboard QMI8658. That is the board's I2C bus, not the keypad bus. Battery sense is GPIO1 on header pin 5.

The keypad button bus (GPIO2/GPIO3) is initialized. LED frames go out SPI3 (HSPI) at 4 MHz, chip-select GPIO35. The panel is SH8601 QSPI on SPI2, addressed as 536 by 240. Brightness is register `0x51` at `kDisplayBrightness`. Touch is not started. Battery GPIO1 is not converted to a percentage.

## ESP-NOW

Both radios are configured for channel 1 in `common/commands.h` (`kEspNowChannel`). A peer MAC of all zeros means the remote sends to the broadcast address and the C3 receives those frames. Each board prints its STA MAC at boot. Paste those into the two `config.h` files to leave broadcast mode. The C3 logs the remote MAC from the first packet it hears.

## What is still open

System power for the Teensy, the C3, the remote, and the amplifier is not documented yet.
