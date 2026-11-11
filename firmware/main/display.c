/*
 * SSD1306 / SH1106 over I2C - no external library.
 *
 * Memory layout of both controllers: 8 "pages" of 8 pixel rows each. One byte is a
 * vertical column of 8 pixels (bit 0 at the top). The frame buffer mirrors that 1:1:
 * s_fb[page * 128 + x].
 *
 * Transfers use page mode (works on both controllers):
 *   commands: set page (0xB0+p), set column (0x00/0x10 + offset)
 *   data:     128 bytes
 * I2C control byte: 0x00 = the following bytes are commands, 0x40 = data.
 */
#include "display.h"

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include "font5x7.h"

static const char *TAG = "display";

static i2c_master_dev_handle_t s_dev;
static bool s_ok;
static char s_status[64] = "not initialised";
static uint8_t s_fb[DISP_W * DISP_H / 8];

/* ---------------- I2C ---------------- */

static esp_err_t send_cmds(const uint8_t *cmds, size_t n)
{
    uint8_t buf[1 + 32];
    buf[0] = 0x00;
    memcpy(&buf[1], cmds, n);
    return i2c_master_transmit(s_dev, buf, n + 1, 50);
}

static esp_err_t send_data(const uint8_t *data, size_t n)
{
    uint8_t ctrl = 0x40;
    i2c_master_transmit_multi_buffer_info_t parts[] = {
        {.write_buffer = &ctrl, .buffer_size = 1},
        {.write_buffer = data, .buffer_size = n},
    };
    return i2c_master_multi_buffer_transmit(s_dev, parts, 2, 50);
}

bool display_init(void)
{
    i2c_master_bus_handle_t bus = i2c_bus();

    if (i2c_master_probe(bus, OLED_I2C_ADDR, 50) != ESP_OK) {
        // Scan the bus so the log shows whether anything answers at all
        int n = snprintf(s_status, sizeof s_status, "not at 0x%02X, bus:", OLED_I2C_ADDR);
        int found = 0;
        for (uint16_t a = 0x08; a < 0x78; a++) {
            if (i2c_master_probe(bus, a, 10) == ESP_OK && n < (int)sizeof s_status - 6) {
                n += snprintf(&s_status[n], sizeof s_status - n, " 0x%02X", a);
                found++;
            }
        }
        if (!found) {
            snprintf(&s_status[n], sizeof s_status - n, " empty (SDA=%d SCL=%d)", PIN_I2C_SDA, PIN_I2C_SCL);
        }
        ESP_LOGW(TAG, "display %s", s_status);
        return false;
    }

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = OLED_I2C_ADDR,
        .scl_speed_hz = OLED_I2C_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &s_dev));

    static const uint8_t init[] = {
        0xAE,             // display off
        0xD5, 0x80,       // clock divider / oscillator
        0xA8, 0x3F,       // multiplex 1/64
        0xD3, 0x00,       // display offset 0
        0x40,             // start line 0
#if OLED_CONTROLLER_SH1106
        0xAD, 0x8B,       // SH1106: internal DC/DC on
#else
        0x8D, 0x14,       // SSD1306: charge pump on
        0x20, 0x02,       // SSD1306: page addressing
#endif
        0xA1,             // columns mirrored (module orientation)
        0xC8,             // scan rows bottom to top
        0xDA, 0x12,       // COM pin configuration for 128x64
        0x81, OLED_CONTRAST,
        0xD9, 0x22,       // pre-charge
        0xDB, 0x35,       // VCOMH
        0xA4,             // show RAM contents
        0xA6,             // not inverted
    };
    if (send_cmds(init, sizeof init) != ESP_OK) {
        snprintf(s_status, sizeof s_status, "answers, init failed");
        ESP_LOGE(TAG, "display %s", s_status);
        return false;
    }

    s_ok = true;
    display_clear();
    display_flush();
    static const uint8_t on = 0xAF;
    send_cmds(&on, 1);
    snprintf(s_status, sizeof s_status, "%s at 0x%02X ok", OLED_CONTROLLER_SH1106 ? "SH1106" : "SSD1306", OLED_I2C_ADDR);
    ESP_LOGI(TAG, "display %s", s_status);
    return true;
}

const char *display_status(void)
{
    return s_status;
}

void display_flush(void)
{
    if (!s_ok) {
        return;
    }
    for (uint8_t page = 0; page < DISP_H / 8; page++) {
        const uint8_t addr[] = {
            0xB0 | page,
            0x00 | (OLED_COL_OFFSET & 0x0F),
            0x10 | (OLED_COL_OFFSET >> 4),
        };
        if (send_cmds(addr, sizeof addr) != ESP_OK || send_data(&s_fb[page * DISP_W], DISP_W) != ESP_OK) {
            ESP_LOGW(TAG, "I2C error during transfer");
            return;
        }
    }
}

/* ---------------- Drawing ---------------- */

void display_clear(void)
{
    memset(s_fb, 0, sizeof s_fb);
}

void display_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= DISP_W || y < 0 || y >= DISP_H) {
        return;
    }
    uint8_t *b = &s_fb[(y / 8) * DISP_W + x];
    const uint8_t bit = 1u << (y & 7);
    *b = on ? (*b | bit) : (*b & ~bit);
}

void display_fill_rect(int x, int y, int w, int h, bool on)
{
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            display_pixel(xx, yy, on);
        }
    }
}

void display_rect(int x, int y, int w, int h)
{
    for (int i = 0; i < w; i++) {
        display_pixel(x + i, y, true);
        display_pixel(x + i, y + h - 1, true);
    }
    for (int i = 0; i < h; i++) {
        display_pixel(x, y + i, true);
        display_pixel(x + w - 1, y + i, true);
    }
}

/* UTF-8 -> code page 437 for the German special characters, everything else becomes '?'. */
static uint8_t next_glyph(const char **p)
{
    const uint8_t c = (uint8_t) * (*p)++;
    if (c < 0x80) {
        return c;
    }
    if (c == 0xC2 && (uint8_t)**p == 0xB7) { // middle dot
        (*p)++;
        return 0xFA;
    }
    if (c == 0xC2 && (uint8_t)**p == 0xB1) { // plus-minus
        (*p)++;
        return 0xF1;
    }
    if (c == 0xC3 && **p) {
        switch ((uint8_t) * (*p)++) {
        case 0xA4: return 0x84; // ä
        case 0xB6: return 0x94; // ö
        case 0xBC: return 0x81; // ü
        case 0x84: return 0x8E; // Ä
        case 0x96: return 0x99; // Ö
        case 0x9C: return 0x9A; // Ü
        case 0x9F: return 0xE1; // ß
        default: return '?';
        }
    }
    while ((**p & 0xC0) == 0x80) { // skip the remaining bytes of an unknown character
        (*p)++;
    }
    return '?';
}

int display_text_width(const char *s, int scale)
{
    int n = 0;
    while (*s) {
        next_glyph(&s);
        n++;
    }
    return n * 6 * scale;
}

int display_text(int x, int y, const char *s, int scale, bool invert)
{
    const int x0 = x;
    while (*s) {
        const uint8_t g = next_glyph(&s);
        const uint8_t *cols = &font5x7[g * FONT5X7_WIDTH];
        for (int col = 0; col < 6; col++) {
            const uint8_t bits = col < FONT5X7_WIDTH ? cols[col] : 0x00; // 6th column = spacing
            for (int row = 0; row < 8; row++) {
                const bool on = ((bits >> row) & 1) != invert;
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        display_pixel(x + col * scale + sx, y + row * scale + sy, on);
                    }
                }
            }
        }
        x += 6 * scale;
    }
    return x - x0;
}
