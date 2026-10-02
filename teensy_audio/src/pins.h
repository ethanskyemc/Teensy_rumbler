#pragma once

#include <stdint.h>

// Teensy 4.0 Serial1 is the UART to the ESP32-C3.
// Audio Shield Rev D, stacked on a Teensy 4.0, uses the pins below.
// The TPA3255 is fed from the shield line-out pads, not the headphone jack.
// Left pad = siren. Right pad = Rumbler. Codec setup uses the line outputs.

namespace pins {

constexpr uint8_t kSerial1Rx = 0;
constexpr uint8_t kSerial1Tx = 1;

// SGTL5000 I2S and control, Audio Shield Rev D.
constexpr uint8_t kI2sMclk = 23;
constexpr uint8_t kI2sBclk = 21;
constexpr uint8_t kI2sLrclk = 20;
constexpr uint8_t kI2sSdout = 7;  // Teensy -> codec
constexpr uint8_t kI2sSdin = 8;   // codec -> Teensy
constexpr uint8_t kCodecScl = 19;
constexpr uint8_t kCodecSda = 18;

// microSD on the shield, SPI.
constexpr uint8_t kSdCs = 10;
constexpr uint8_t kSdMosi = 11;
constexpr uint8_t kSdMiso = 12;
constexpr uint8_t kSdSck = 13;

// On-shield volume potentiometer. Not used as the siren volume source.
constexpr uint8_t kVolumePot = 15;

}  // namespace pins
