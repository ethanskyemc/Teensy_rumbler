#include <Arduino.h>

#include "debug.h"
#include "display.h"
#include "keypad.h"
#include "protocol.h"
#include "ui.h"
#include "wireless.h"

void setup() {
    Serial.begin(115200);
    if (!siren::protocolSelfTest()) {
        Serial.println("protocol self-test FAILED");
    } else {
        SIREN_LOG("remote: protocol self-test ok\n");
    }

    keypad::begin();
    display::begin();
    wireless::begin();
    ui::begin();
    SIREN_LOG("remote: keypad and radio live, leds and panel off\n");
}

void loop() {
    const uint32_t now = millis();
    keypad::poll(now);
    wireless::update(now);
    ui::update(now);
    keypad::render(now);
    yield();
}
