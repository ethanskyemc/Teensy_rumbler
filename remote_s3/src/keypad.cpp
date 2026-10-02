#include "keypad.h"

#include "debug.h"
#include "pins.h"

#include <SPI.h>
#include <Wire.h>
#include <string.h>

namespace keypad {
namespace {

constexpr uint8_t kQueueCapacity = 16;

KeyEvent queue_[kQueueCapacity];
uint8_t queue_head_ = 0;
uint8_t queue_count_ = 0;
KeypadView view_ = {};
bool view_set_ = false;
bool logged_drop_ = false;
bool i2c_fault_logged_ = false;
bool i2c_started_ = false;

bool sample_started_ = false;
uint32_t last_sample_ms_ = 0;
bool seeded_ = false;
uint16_t published_ = 0;
uint16_t candidate_ = 0;
uint32_t candidate_ms_ = 0;

// HSPI is SPI3 on the S3. The panel owns SPI2.
SPIClass led_spi_(HSPI);
bool leds_ready_ = false;
bool led_frame_valid_ = false;
uint8_t led_frame_[72] = {};

constexpr uint8_t kLedFrameBytes = 72;
constexpr uint8_t kLedHeaderBytes = 4;
constexpr uint8_t kLedBrightness = 15;  // 0.5 * 31, Pimoroni default

void writeLeds(const uint8_t* frame) {
    digitalWrite(pins::kKeypadLedCs, LOW);
    led_spi_.beginTransaction(SPISettings(pins::kKeypadSpiHz, MSBFIRST, SPI_MODE0));
    led_spi_.writeBytes(frame, kLedFrameBytes);
    led_spi_.endTransaction();
    digitalWrite(pins::kKeypadLedCs, HIGH);
}

void startLeds() {
    pinMode(pins::kKeypadLedCs, OUTPUT);
    digitalWrite(pins::kKeypadLedCs, HIGH);
    leds_ready_ = led_spi_.begin(pins::kKeypadLedSck, -1, pins::kKeypadLedMosi, -1);
    if (!leds_ready_) {
        SIREN_LOG("keypad: led spi begin failed\n");
        return;
    }
    SIREN_LOG("keypad: leds spi3 sck=%d mosi=%d cs=%d\n", pins::kKeypadLedSck,
              pins::kKeypadLedMosi, pins::kKeypadLedCs);
}

bool pushEvent(const KeyEvent& event) {
    if (queue_count_ >= kQueueCapacity) {
        return false;
    }
    const uint8_t slot = static_cast<uint8_t>((queue_head_ + queue_count_) % kQueueCapacity);
    queue_[slot] = event;
    queue_count_ = static_cast<uint8_t>(queue_count_ + 1u);
    return true;
}

bool buttonShowsLatchedSound(uint8_t index, const KeypadView& view) {
    const cfg::ButtonBinding& binding = cfg::kButtonBindings[index];
    return view.playing && binding.action == cfg::ButtonAction::PLAY_LATCHED &&
           binding.sound == view.active_sound;
}

bool readButtons(uint16_t& bits) {
    Wire.beginTransmission(pins::kKeypadI2cAddress);
    Wire.write(static_cast<uint8_t>(0));
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    const uint8_t got = static_cast<uint8_t>(
        Wire.requestFrom(pins::kKeypadI2cAddress, static_cast<uint8_t>(2)));
    if (got != 2) {
        return false;
    }
    const uint16_t lo = static_cast<uint16_t>(Wire.read());
    const uint16_t hi = static_cast<uint16_t>(Wire.read());
    const uint16_t raw = static_cast<uint16_t>(lo | static_cast<uint16_t>(hi << 8));
    bits = static_cast<uint16_t>(~raw);
    return true;
}

void emitStable(uint16_t bits) {
    const uint16_t changed = static_cast<uint16_t>(published_ ^ bits);
    for (uint8_t i = 0; i < cfg::kButtonCount; ++i) {
        const uint16_t mask = static_cast<uint16_t>(1u << i);
        if ((changed & mask) == 0) {
            continue;
        }
        KeyEvent event = {};
        event.button = i;
        event.pressed = (bits & mask) != 0;
        if (!pushEvent(event) && !logged_drop_) {
            logged_drop_ = true;
            SIREN_LOG("keypad: event queue full\n");
        }
    }
    published_ = bits;
}

}  // namespace

cfg::Rgb colorForButton(uint8_t index, const KeypadView& view) {
    if (index >= cfg::kButtonCount) {
        return cfg::kRgbIdle;
    }

    if (view.in_startup) {
        const uint8_t lit = static_cast<uint8_t>((view.startup_elapsed_ms / 50u) % cfg::kButtonCount);
        return index == lit ? cfg::kRgbStartup : cfg::kRgbIdle;
    }

    if (!view.link_up) {
        const bool bright = (view.now_ms % cfg::kLinkLostPeriodMs) < cfg::kLinkLostBrightMs;
        return bright ? cfg::kRgbLinkLostBright : cfg::kRgbLinkLostDim;
    }

    const cfg::ButtonBinding& binding = cfg::kButtonBindings[index];
    if (binding.action == cfg::ButtonAction::PLAY_MOMENTARY && view.air_horn_held) {
        return cfg::kRgbAirHorn;
    }
    if (binding.action == cfg::ButtonAction::RUMBLER_TOGGLE && view.rumbler_enabled) {
        return cfg::kRgbRumblerOn;
    }
    if (buttonShowsLatchedSound(index, view)) {
        return cfg::kRgbActive;
    }
    return cfg::kRgbIdle;
}

void begin() {
    queue_head_ = 0;
    queue_count_ = 0;
    view_set_ = false;
    logged_drop_ = false;
    i2c_fault_logged_ = false;
    sample_started_ = false;
    seeded_ = false;
    published_ = 0;
    candidate_ = 0;

    KeyEvent sample = {};
    sample.button = 15;
    sample.pressed = true;
    pushEvent(sample);
    KeyEvent got = {};
    const bool queue_ok = nextEvent(got) && got.button == 15 && got.pressed && queue_count_ == 0;
    if (!queue_ok) {
        SIREN_LOG("keypad: event queue failed\n");
    }

    i2c_started_ = Wire.begin(pins::kKeypadSda, pins::kKeypadScl);
    if (!i2c_started_) {
        SIREN_LOG("keypad: i2c begin failed\n");
    } else {
        Wire.setClock(pins::kKeypadI2cHz);
        SIREN_LOG("keypad: i2c sda=%d scl=%d addr=0x%02X\n", pins::kKeypadSda, pins::kKeypadScl,
                  pins::kKeypadI2cAddress);
    }
    startLeds();
}

void poll(uint32_t now_ms) {
    if (!i2c_started_) {
        return;
    }
    if (sample_started_ && !siren::elapsedMs(now_ms, last_sample_ms_, cfg::kButtonSampleMs)) {
        return;
    }
    sample_started_ = true;
    last_sample_ms_ = now_ms;

    uint16_t bits = 0;
    if (!readButtons(bits)) {
        if (!i2c_fault_logged_) {
            i2c_fault_logged_ = true;
            SIREN_LOG("keypad: i2c read failed\n");
        }
        return;
    }
    if (i2c_fault_logged_) {
        i2c_fault_logged_ = false;
        SIREN_LOG("keypad: i2c read ok\n");
    }

    if (!seeded_) {
        published_ = bits;
        candidate_ = bits;
        seeded_ = true;
        return;
    }
    if (bits == published_) {
        candidate_ = bits;
        return;
    }
    if (bits != candidate_) {
        candidate_ = bits;
        candidate_ms_ = now_ms;
        return;
    }
    if (!siren::elapsedMs(now_ms, candidate_ms_, cfg::kButtonDebounceMs)) {
        return;
    }
    emitStable(bits);
}

void setView(const KeypadView& view) {
    view_ = view;
    view_set_ = true;
}

void render(uint32_t now_ms) {
    (void)now_ms;
    if (!view_set_ || !leds_ready_) {
        return;
    }

    uint8_t frame[kLedFrameBytes] = {};
    for (uint8_t i = 0; i < cfg::kButtonCount; ++i) {
        const cfg::Rgb color = colorForButton(i, view_);
        uint8_t* pixel = frame + kLedHeaderBytes + (i * 4);
        pixel[0] = static_cast<uint8_t>(0xE0 | kLedBrightness);
        pixel[1] = color.b;
        pixel[2] = color.g;
        pixel[3] = color.r;
    }
    if (led_frame_valid_ && memcmp(led_frame_, frame, kLedFrameBytes) == 0) {
        return;
    }
    memcpy(led_frame_, frame, kLedFrameBytes);
    led_frame_valid_ = true;
    writeLeds(frame);
}

bool nextEvent(KeyEvent& out) {
    if (queue_count_ == 0) {
        return false;
    }
    out = queue_[queue_head_];
    queue_head_ = static_cast<uint8_t>((queue_head_ + 1u) % kQueueCapacity);
    queue_count_ = static_cast<uint8_t>(queue_count_ - 1u);
    return true;
}

}  // namespace keypad
