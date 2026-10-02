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
constexpr uint8_t kLedBrightness = 31;  // 0.5 * 31, Pimoroni default

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
    Serial.flush();
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

bool buttonShowsPlayingSound(uint8_t index, const KeypadView& view) {
    const cfg::ButtonBinding& binding = cfg::kButtonBindings[index];
    if (!view.playing || binding.sound == siren::SoundID::NONE || binding.sound != view.active_sound) {
        return false;
    }
    return binding.action == cfg::ButtonAction::PLAY_LATCHED ||
           binding.action == cfg::ButtonAction::PLAY_MOMENTARY;
}

cfg::Rgb rainbow(uint32_t now_ms) {
    constexpr uint8_t kLevel = 170;
    const uint32_t step = (now_ms % cfg::kRainbowPeriodMs) * cfg::kRainbowSteps / cfg::kRainbowPeriodMs;
    uint8_t pos = static_cast<uint8_t>((step * 256u) / cfg::kRainbowSteps);
    pos = static_cast<uint8_t>(255u - pos);
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    if (pos < 85) {
        r = static_cast<uint8_t>(255u - pos * 3u);
        b = static_cast<uint8_t>(pos * 3u);
    } else if (pos < 170) {
        pos = static_cast<uint8_t>(pos - 85u);
        g = static_cast<uint8_t>(pos * 3u);
        b = static_cast<uint8_t>(255u - pos * 3u);
    } else {
        pos = static_cast<uint8_t>(pos - 170u);
        r = static_cast<uint8_t>(pos * 3u);
        g = static_cast<uint8_t>(255u - pos * 3u);
    }
    return cfg::Rgb{
        static_cast<uint8_t>((static_cast<uint16_t>(r) * kLevel) / 255u),
        static_cast<uint8_t>((static_cast<uint16_t>(g) * kLevel) / 255u),
        static_cast<uint8_t>((static_cast<uint16_t>(b) * kLevel) / 255u),
    };
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

cfg::Rgb restingColor(uint8_t index, const KeypadView& view) {
    switch (cfg::kButtonBindings[index].action) {
        case cfg::ButtonAction::PLAY_LATCHED:
            return cfg::kRgbLatched;
        case cfg::ButtonAction::PLAY_MOMENTARY:
            return cfg::kRgbMomentary;
        case cfg::ButtonAction::ALL_STOP:
            return cfg::kRgbStop;
        case cfg::ButtonAction::VOLUME_DOWN:
        case cfg::ButtonAction::VOLUME_UP:
            return cfg::kRgbVolume;
        case cfg::ButtonAction::RUMBLER_TOGGLE:
            return view.rumbler_enabled ? cfg::kRgbRumblerOn : cfg::kRgbRumblerOff;
        case cfg::ButtonAction::RESERVED_PA:
        case cfg::ButtonAction::NEXT_BANK:
            return cfg::kRgbOff;
    }
    return cfg::kRgbOff;
}

uint8_t quantize(uint8_t level) {
    return static_cast<uint8_t>((level / 16u) * 16u);
}

cfg::Rgb scale(cfg::Rgb color, uint8_t level) {
    return cfg::Rgb{
        static_cast<uint8_t>((static_cast<uint16_t>(color.r) * level) / 255u),
        static_cast<uint8_t>((static_cast<uint16_t>(color.g) * level) / 255u),
        static_cast<uint8_t>((static_cast<uint16_t>(color.b) * level) / 255u),
    };
}

cfg::Rgb blend(cfg::Rgb from, cfg::Rgb to, uint8_t toward_to) {
    const auto channel = [toward_to](uint8_t a, uint8_t b) {
        return static_cast<uint8_t>(a + (static_cast<int16_t>(b) - static_cast<int16_t>(a)) * toward_to / 255);
    };
    return cfg::Rgb{channel(from.r, to.r), channel(from.g, to.g), channel(from.b, to.b)};
}

bool stopPulseOrder(uint8_t index, uint8_t& order) {
    if (cfg::kButtonBindings[index].action == cfg::ButtonAction::ALL_STOP) {
        return false;
    }
    order = 0;
    for (uint8_t i = 0; i < index; ++i) {
        if (cfg::kButtonBindings[i].action != cfg::ButtonAction::ALL_STOP) {
            order = static_cast<uint8_t>(order + 1u);
        }
    }
    return true;
}

bool stopPulseLevel(uint8_t index, const KeypadView& view, uint8_t& level) {
    uint8_t order = 0;
    if (!view.stop_pulse || !stopPulseOrder(index, order)) {
        return false;
    }
    const uint32_t elapsed = view.now_ms - view.stop_pulse_start_ms;
    const uint32_t center = static_cast<uint32_t>(order) * cfg::kStopPulseStepMs;
    const uint32_t dist = elapsed > center ? elapsed - center : center - elapsed;
    if (dist >= cfg::kStopPulseWidthMs) {
        return false;
    }
    level = quantize(static_cast<uint8_t>(((cfg::kStopPulseWidthMs - dist) * 255u) / cfg::kStopPulseWidthMs));
    return level != 0;
}

bool volumeFlashFade(uint8_t index, const KeypadView& view, uint8_t& toward_rest) {
    for (uint8_t i = 0; i < view.volume_flash_count && i < 2; ++i) {
        if (view.volume_flash_button[i] != index) {
            continue;
        }
        const uint32_t elapsed = view.now_ms - view.volume_flash_start_ms[i];
        if (elapsed >= cfg::kVolumeFlashMs) {
            return false;
        }
        toward_rest = quantize(static_cast<uint8_t>((elapsed * 255u) / cfg::kVolumeFlashMs));
        return true;
    }
    return false;
}

}  // namespace

cfg::Rgb colorForButton(uint8_t index, const KeypadView& view) {
    if (index >= cfg::kButtonCount) {
        return cfg::kRgbOff;
    }

    if (view.in_startup) {
        const uint8_t lit = static_cast<uint8_t>((view.startup_elapsed_ms / 50u) % cfg::kButtonCount);
        return index == lit ? cfg::kRgbStartup : cfg::kRgbOff;
    }

    uint8_t level = 0;
    if (stopPulseLevel(index, view, level)) {
        return scale(cfg::kRgbStopPulse, level);
    }

    uint8_t toward_rest = 0;
    if (volumeFlashFade(index, view, toward_rest)) {
        return blend(cfg::kRgbVolumeFlash, cfg::kRgbVolume, toward_rest);
    }

    if (!view.link_up) {
        const bool bright = (view.now_ms % cfg::kLinkLostPeriodMs) < cfg::kLinkLostBrightMs;
        return bright ? cfg::kRgbLinkLostBright : cfg::kRgbLinkLostDim;
    }

    if (buttonShowsPlayingSound(index, view)) {
        return rainbow(view.now_ms);
    }

    return restingColor(index, view);
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
