#pragma once

// ESP32-C3 UART to the Teensy. Silkscreen RX/TX are UART0.
// C3 GPIO21 (TX) -> Teensy pin 0 (RX1)
// C3 GPIO20 (RX) <- Teensy pin 1 (TX1)
// Common ground. Both sides are 3.3 V logic.
// Debug logs use USB CDC so they do not share this UART.

namespace pins {

constexpr int kUartRx = 20;
constexpr int kUartTx = 21;

}  // namespace pins
