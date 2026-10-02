#include <Arduino.h>

#include "debug.h"
#include "espnow.h"
#include "protocol.h"
#include "uart_bridge.h"

void setup() {
    Serial.begin(115200);
    if (!siren::protocolSelfTest()) {
        Serial.println("protocol self-test FAILED");
    } else {
        SIREN_LOG("bridge: protocol self-test ok\n");
    }
    uart_bridge::begin();
    espnow::begin();
    SIREN_LOG("bridge: esp-now forward and uart live\n");
}

void loop() {
    const uint32_t now = millis();
    uart_bridge::poll(now);
    espnow::poll(now);
    yield();
}
