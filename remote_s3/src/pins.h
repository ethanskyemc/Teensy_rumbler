#pragma once

#include <stdint.h>

// Waveshare ESP32-S3-AMOLED-1.91, Pico-style 40-pin header.
// Header pin numbers match a Raspberry Pi Pico. The ESP32 GPIO on that
// pin does not match the Pico GP number.
//
// Pimoroni Pico RGB Keypad (pimoroni-pico pico_rgb_keypad.cpp):
//   Pico GP4  SDA, GP5 SCL, GP17 CS, GP18 SCK, GP19 MOSI.
// Those Pico pads land on header pins 6, 7, 22, 24, and 25.

namespace pins {

// Button I2C is read by keypad.cpp. This flag still gates the LED strip.
// The panel driver stays off as well.
constexpr bool kKeypadDriverReady = false;
constexpr bool kDisplayDriverReady = false;

// Keypad buttons: I2C expander at 0x20, 400 kHz.
// The library writes register 0, reads two bytes, and inverts them.
// Pressed keys are 1. This bus is separate from the panel touch bus.
constexpr int kKeypadSda = 2;   // header 6, Pico GP4
constexpr int kKeypadScl = 3;   // header 7, Pico GP5
constexpr uint8_t kKeypadI2cAddress = 0x20;
constexpr uint32_t kKeypadI2cHz = 400000;

// Keypad LEDs: SPI, APA102/SK9822 frame layout, 4 MHz in the Pico library.
// CS is driven around the transfer. Use a different SPI host from the panel.
constexpr int kKeypadLedCs = 35;    // header 22, Pico GP17
constexpr int kKeypadLedSck = 36;   // header 24, Pico GP18
constexpr int kKeypadLedMosi = 37;  // header 25, Pico GP19
constexpr uint32_t kKeypadSpiHz = 4000000;

// AMOLED, QSPI, SH8601, 240 x 536. Confirmed by the Waveshare LVGL example
// and the display-control table. There is no backlight GPIO; brightness is
// SH8601 register 0x51. Reset is also header pin 35, which the keypad does
// not use.
constexpr int kDisplayCs = 6;
constexpr int kDisplaySck = 47;
constexpr int kDisplayD0 = 18;
constexpr int kDisplayD1 = 7;
constexpr int kDisplayD2 = 48;
constexpr int kDisplayD3 = 5;
constexpr int kDisplayRst = 17;
constexpr int kDisplayWidth = 240;
constexpr int kDisplayHeight = 536;

// FT3168 touch controller, address 0x38, on the board I2C with the QMI8658.
// Header pins 29 and 27. Not used by the keypad.
constexpr int kTouchSda = 40;
constexpr int kTouchScl = 39;
constexpr uint8_t kTouchI2cAddress = 0x38;

// Header pin 5. The ADC is not read yet.
constexpr int kBatteryAdc = 1;

}  // namespace pins
