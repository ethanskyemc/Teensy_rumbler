#include <Arduino.h>

#include "audio_engine.h"
#include "command_handler.h"
#include "debug.h"
#include "protocol.h"
#include "sd_audio.h"

void setup() {
    Serial.begin(115200);
    // Do not wait for the USB host. A headless boot must still reach silence.
    if (!siren::protocolSelfTest()) {
        Serial.println("protocol self-test FAILED");
    } else {
        SIREN_LOG("teensy: protocol self-test ok\n");
    }

    sd_audio::begin();
    audio_engine::begin();
    command_handler::begin();
    SIREN_LOG("teensy: wav playback live, silent until a command\n");
}

void loop() {
    const uint32_t now = millis();
    command_handler::poll(now);
    audio_engine::update(now);
    yield();
}
