#include "display.h"

#include "debug.h"
#include "esp_lcd_sh8601.h"
#include "pins.h"
#include "sounds.h"

#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdio.h>
#include <string.h>

namespace display {
namespace {

UiState last_ = {};
bool have_last_ = false;
bool ready_ = false;
bool flush_timeout_logged_ = false;

esp_lcd_panel_handle_t panel_ = nullptr;
SemaphoreHandle_t flush_done_ = nullptr;
// One strip of the panel. The full frame is 257 KB, which does not fit in
// internal RAM, and the octal PSRAM pins are the keypad LED bus.
constexpr int kStripRows = 24;
int strip_y_ = 0;
uint16_t* strip_ = nullptr;

extern "C" bool onColorDone(esp_lcd_panel_io_handle_t panel_io,
                            esp_lcd_panel_io_event_data_t* edata, void* user_ctx) {
    (void)panel_io;
    (void)edata;
    (void)user_ctx;
    BaseType_t woke = pdFALSE;
    if (flush_done_ != nullptr) {
        xSemaphoreGiveFromISR(flush_done_, &woke);
    }
    return woke == pdTRUE;
}

constexpr int kWidth = pins::kDisplayWidth;
constexpr int kHeight = pins::kDisplayHeight;
constexpr size_t kStripPixels = static_cast<size_t>(kWidth) * kStripRows;

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t kBg = 0x0000;
constexpr uint16_t kLabel = rgb565(140, 150, 160);
constexpr uint16_t kValue = rgb565(235, 240, 245);
constexpr uint16_t kLinkDown = rgb565(220, 90, 20);
constexpr uint16_t kLinkUp = rgb565(40, 200, 90);

// 5x7 glyphs. Bit 4 is the left pixel. Order: space, hyphen, 0-9, A-Z.
constexpr uint8_t kFont[][7] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00},
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
    {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E},
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
    {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E},
    {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E},
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E},
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},
    {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F},
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F},
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},
    {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C},
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11},
    {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D},
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E},
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A},
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04},
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F},
};

bool sameState(const UiState& a, const UiState& b) {
    return a.link_up == b.link_up && a.playing == b.playing &&
           a.rumbler_enabled == b.rumbler_enabled && a.volume_valid == b.volume_valid &&
           a.battery_valid == b.battery_valid && a.volume_master == b.volume_master &&
           a.volume_siren == b.volume_siren && a.volume_rumbler == b.volume_rumbler &&
           a.battery_percent == b.battery_percent && a.flags == b.flags && a.sound == b.sound &&
           a.teensy == b.teensy && a.sd == b.sd;
}

int glyphIndex(char c) {
    if (c == ' ') {
        return 0;
    }
    if (c == '-') {
        return 1;
    }
    if (c >= '0' && c <= '9') {
        return 2 + (c - '0');
    }
    if (c >= 'a' && c <= 'z') {
        c = static_cast<char>(c - 'a' + 'A');
    }
    if (c >= 'A' && c <= 'Z') {
        return 12 + (c - 'A');
    }
    return -1;
}

void pixel(int x, int y, uint16_t color) {
    if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) {
        return;
    }
    // 180 degrees in the existing 536x240 window. The panel scan stays as
    // Waveshare set it; only the pixels we draw move.
    const int px = kWidth - 1 - x;
    const int py = kHeight - 1 - y;
    if (py < strip_y_ || py >= strip_y_ + kStripRows) {
        return;
    }
    strip_[static_cast<size_t>(py - strip_y_) * static_cast<size_t>(kWidth) + static_cast<size_t>(px)] =
        color;
}

void fill(uint16_t color) {
    for (size_t i = 0; i < kStripPixels; ++i) {
        strip_[i] = color;
    }
}

void bar(int x, int y, int w, int h, uint16_t color) {
    for (int row = y; row < y + h; ++row) {
        for (int col = x; col < x + w; ++col) {
            pixel(col, row, color);
        }
    }
}

void glyph(int x, int y, int index, uint16_t color, int scale) {
    if (index < 0) {
        return;
    }
    for (int row = 0; row < 7; ++row) {
        const uint8_t bits = kFont[index][row];
        for (int col = 0; col < 5; ++col) {
            if ((bits & (0x10 >> col)) == 0) {
                continue;
            }
            bar(x + col * scale, y + row * scale, scale, scale, color);
        }
    }
}

void text(int x, int y, const char* s, uint16_t color, int scale) {
    const int advance = 5 * scale + 2;
    for (int i = 0; s[i] != '\0'; ++i) {
        glyph(x, y, glyphIndex(s[i]), color, scale);
        x += advance;
    }
}

const char* teensyName(siren::TeensyStatus status) {
    switch (status) {
        case siren::TeensyStatus::BOOT:
            return "BOOT";
        case siren::TeensyStatus::READY:
            return "READY";
        case siren::TeensyStatus::FAULT:
            return "FAULT";
        case siren::TeensyStatus::UNKNOWN:
            return "--";
    }
    return "--";
}

const char* sdName(siren::SdStatus status) {
    switch (status) {
        case siren::SdStatus::OK:
            return "OK";
        case siren::SdStatus::NOT_PRESENT:
            return "NO CARD";
        case siren::SdStatus::MOUNT_FAILED:
            return "MOUNT";
        case siren::SdStatus::FILE_MISSING:
            return "NO FILE";
        case siren::SdStatus::UNKNOWN:
            return "--";
    }
    return "--";
}

void numberOrDash(char* out, size_t out_len, bool valid, uint8_t value) {
    if (!valid) {
        snprintf(out, out_len, "--");
        return;
    }
    snprintf(out, out_len, "%u", value);
}

void paint(const UiState& state) {
    fill(kBg);
    const uint16_t accent = state.link_up ? kLinkUp : kLinkDown;
    bar(0, 0, 8, kHeight, accent);

    constexpr int kScale = 3;
    constexpr int kLine = 7 * kScale + 8;
    int y = 16;
    const int x = 24;
    char value[16];

    text(x, y, "LINK", kLabel, kScale);
    text(x + 110, y, state.link_up ? "UP" : "DOWN", accent, kScale);
    y += kLine;

    text(x, y, "SOUND", kLabel, kScale);
    text(x + 140, y, state.link_up ? siren::soundName(state.sound) : "--", kValue, kScale);
    y += kLine;

    text(x, y, "PLAY", kLabel, kScale);
    text(x + 140, y, state.link_up ? (state.playing ? "ON" : "OFF") : "--", kValue, kScale);
    y += kLine;

    text(x, y, "RUMBLER", kLabel, kScale);
    text(x + 180, y, state.link_up ? (state.rumbler_enabled ? "ON" : "OFF") : "--", kValue, kScale);
    y += kLine;

    text(x, y, "VOL", kLabel, kScale);
    numberOrDash(value, sizeof(value), state.link_up && state.volume_valid, state.volume_master);
    text(x + 140, y, value, kValue, kScale);
    y += kLine;

    text(x, y, "TEENSY", kLabel, kScale);
    text(x + 160, y, state.link_up ? teensyName(state.teensy) : "--", kValue, kScale);
    text(x + 320, y, "SD", kLabel, kScale);
    text(x + 380, y, state.link_up ? sdName(state.sd) : "--", kValue, kScale);
    y += kLine;

    text(x, y, "BAT", kLabel, kScale);
    numberOrDash(value, sizeof(value), state.battery_valid, state.battery_percent);
    text(x + 140, y, value, kValue, kScale);
}

bool flushStrip() {
    // Internal RAM is not on the data cache. msync rejects that address.
    // DMA reads the same bytes the CPU just wrote.
    if (esp_ptr_external_ram(strip_)) {
        const esp_err_t synced = esp_cache_msync(
            strip_, kStripPixels * sizeof(uint16_t),
            ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
        if (synced != ESP_OK) {
            SIREN_LOG("display: cache sync failed\n");
        }
    }
    const esp_err_t drawn = esp_lcd_panel_draw_bitmap(panel_, 0, strip_y_, kWidth,
                                                      strip_y_ + kStripRows, strip_);
    if (drawn != ESP_OK) {
        SIREN_LOG("display: draw failed\n");
        return false;
    }
    if (xSemaphoreTake(flush_done_, pdMS_TO_TICKS(300)) != pdTRUE) {
        if (!flush_timeout_logged_) {
            flush_timeout_logged_ = true;
            SIREN_LOG("display: flush timeout\n");
        }
        return false;
    }
    return true;
}

}  // namespace

void begin() {
    have_last_ = false;
    ready_ = false;

    const size_t bytes = kStripPixels * sizeof(uint16_t);
    strip_ = static_cast<uint16_t*>(
        heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
    if (strip_ == nullptr) {
        SIREN_LOG("display: strip alloc failed\n");
        Serial.flush();
        return;
    }

    flush_done_ = xSemaphoreCreateBinary();
    if (flush_done_ == nullptr) {
        SIREN_LOG("display: flush semaphore failed\n");
        return;
    }

    spi_bus_config_t bus = {};
    bus.data0_io_num = pins::kDisplayD0;
    bus.data1_io_num = pins::kDisplayD1;
    bus.sclk_io_num = pins::kDisplaySck;
    bus.data2_io_num = pins::kDisplayD2;
    bus.data3_io_num = pins::kDisplayD3;
    bus.data4_io_num = -1;
    bus.data5_io_num = -1;
    bus.data6_io_num = -1;
    bus.data7_io_num = -1;
    bus.max_transfer_sz = static_cast<int>(bytes);
    const esp_err_t bus_ok = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (bus_ok != ESP_OK) {
        SIREN_LOG("display: qspi bus init failed\n");
        return;
    }

    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = pins::kDisplayCs;
    io_config.dc_gpio_num = -1;
    io_config.spi_mode = 0;
    io_config.pclk_hz = 40 * 1000 * 1000;
    io_config.trans_queue_depth = 10;
    io_config.on_color_trans_done = onColorDone;
    io_config.lcd_cmd_bits = 32;
    io_config.lcd_param_bits = 8;
    io_config.flags.quad_mode = 1;

    esp_lcd_panel_io_handle_t io = nullptr;
    const esp_err_t io_ok = esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &io);
    if (io_ok != ESP_OK) {
        SIREN_LOG("display: panel io failed\n");
        return;
    }

    static const uint8_t kZero = 0x00;
    static const uint8_t kMadctl[] = {0xF0};
    static const uint8_t kColmod[] = {0x55};
    static const uint8_t kCols[] = {0x00, 0x00, 0x02, 0x17};
    static const uint8_t kRows[] = {0x00, 0x00, 0x00, 0xEF};
    static const uint8_t kBrightOff[] = {0x00};
    static const uint8_t kBright[] = {pins::kDisplayBrightness};
    static const sh8601_lcd_init_cmd_t kInit[] = {
        {0x11, &kZero, 0, 120},
        {0x36, kMadctl, 1, 0},
        {0x3A, kColmod, 1, 0},
        {0x2A, kCols, 4, 0},
        {0x2B, kRows, 4, 0},
        {0x51, kBrightOff, 1, 10},
        {0x29, &kZero, 0, 10},
        {0x51, kBright, 1, 0},
    };

    sh8601_vendor_config_t vendor = {};
    vendor.init_cmds = kInit;
    vendor.init_cmds_size = sizeof(kInit) / sizeof(kInit[0]);
    vendor.flags.use_qspi_interface = 1;

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = pins::kDisplayRst;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.data_endian = LCD_RGB_DATA_ENDIAN_LITTLE;
    panel_config.bits_per_pixel = 16;
    panel_config.vendor_config = &vendor;

    const esp_err_t panel_ok = esp_lcd_new_panel_sh8601(io, &panel_config, &panel_);
    if (panel_ok != ESP_OK || esp_lcd_panel_reset(panel_) != ESP_OK ||
        esp_lcd_panel_init(panel_) != ESP_OK || esp_lcd_panel_disp_on_off(panel_, true) != ESP_OK) {
        SIREN_LOG("display: panel init failed\n");
        return;
    }

    ready_ = true;
    SIREN_LOG("display: SH8601 %dx%d brightness=%u\n", kWidth, kHeight, pins::kDisplayBrightness);
    Serial.flush();
}

void render(const UiState& state) {
    if (have_last_ && sameState(last_, state)) {
        return;
    }
    last_ = state;
    have_last_ = true;
    SIREN_LOG(
        "display: link=%u sound=%u playing=%u rumbler=%u vol_valid=%u vol=%u teensy=%u sd=%u\n",
        state.link_up ? 1u : 0u, static_cast<unsigned>(state.sound), state.playing ? 1u : 0u,
        state.rumbler_enabled ? 1u : 0u, state.volume_valid ? 1u : 0u, state.volume_master,
        static_cast<unsigned>(state.teensy), static_cast<unsigned>(state.sd));
    if (!ready_) {
        return;
    }
    for (int y = 0; y < kHeight; y += kStripRows) {
        strip_y_ = y;
        paint(state);
        if (!flushStrip()) {
            break;
        }
    }
}

}  // namespace display
