/**
 * @file oled_display.c
 * @brief OLED 显示屏驱动 — SSD1309 (128x64) via I2C
 *
 * 纯 ESP-IDF 原生 i2c_master 驱动，不依赖任何外部图形库。
 * 内置 6x8 ASCII 点阵字体，支持 3 页自动轮播。
 *
 * 3 页自动轮播：
 *   1. 系统概览（WiFi状态、IP、设备ID/角色）
 *   2. 时间与任务（时钟、Cron数、内存）
 *   3. 最新消息
 */

#include "display/oled_display.h"
#include "mimi_config.h"

#include <string.h>
#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"

/* For WiFi detail page */
#include "esp_wifi.h"

/* 依赖其他模块获取状态 */
#include "wifi/wifi_manager.h"
#include "cron/cron_service.h"

/* Chinese font support */
#include "display/utf8_decode.h"
#include "display/font_chinese.h"

#if __has_include("espnow/esp_now_device.h")
#include "espnow/esp_now_device.h"
#endif

static const char *TAG = "oled";

/* ── OLED 配置 ─────────────────────────────────────────────────── */
#ifndef MIMI_OLED_SDA_GPIO
#define MIMI_OLED_SDA_GPIO   21
#endif
#ifndef MIMI_OLED_SCL_GPIO
#define MIMI_OLED_SCL_GPIO   22
#endif
#ifndef MIMI_OLED_REFRESH_MS
#define MIMI_OLED_REFRESH_MS 2000
#endif
#ifndef MIMI_OLED_PAGE_SWITCH_MS
#define MIMI_OLED_PAGE_SWITCH_MS 3000
#endif

#define OLED_I2C_ADDR       0x3C
#define OLED_I2C_FREQ_HZ    400000
#define OLED_I2C_TIMEOUT_MS 100
#define OLED_W              128
#define OLED_H              64
#define OLED_PAGES          8    /* 64/8 = 8 pages */

#define FONT_W    6
#define FONT_H    8
#define CHARS_PER_LINE  (OLED_W / FONT_W)  /* 21 */
#define MAX_LINES       (OLED_H / FONT_H)  /* 8 */

/* ── I2C 句柄 ──────────────────────────────────────────────────── */
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_i2c_dev = NULL;

/* ── 帧缓冲 (1024 bytes, 8 pages x 128 columns) ────────────────── */
static uint8_t s_fb[OLED_PAGES][OLED_W];

/* ── 任务句柄 ──────────────────────────────────────────────────── */
static TaskHandle_t s_task = NULL;
static bool s_inited = false;
static bool s_hw_ok  = false;   /* I2C device responding? */

/* ── Button GPIO state ──────────────────────────────────────────── */
static TaskHandle_t      s_btn_task = NULL;
static const int s_btn_gpios[4] = {
    MIMI_OLED_BTN_UP_GPIO,
    MIMI_OLED_BTN_DOWN_GPIO,
    MIMI_OLED_BTN_SELECT_GPIO,
    MIMI_OLED_BTN_BACK_GPIO,
};
static int s_btn_debounce[4] = {0, 0, 0, 0};  /* 连续低电平计数 */

/* ================================================================
 *  6x8 ASCII 字体 (仅可打印字符 0x20-0x7E)
 *  每个字符 6 bytes，每行 1 byte (8 pixel rows), MSB top
 * ================================================================ */
static const uint8_t font6x8[95][6] = {
    /* 0x20 ' ' */ {0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x21 '!' */ {0x00,0x00,0x5F,0x00,0x00,0x00},
    /* 0x22 '"' */ {0x00,0x07,0x00,0x07,0x00,0x00},
    /* 0x23 '#' */ {0x14,0x7F,0x14,0x7F,0x14,0x00},
    /* 0x24 '$' */ {0x24,0x2A,0x7F,0x2A,0x12,0x00},
    /* 0x25 '%' */ {0x23,0x13,0x08,0x64,0x62,0x00},
    /* 0x26 '&' */ {0x36,0x49,0x55,0x22,0x50,0x00},
    /* 0x27 ''' */ {0x00,0x05,0x03,0x00,0x00,0x00},
    /* 0x28 '(' */ {0x00,0x1C,0x22,0x41,0x00,0x00},
    /* 0x29 ')' */ {0x00,0x41,0x22,0x1C,0x00,0x00},
    /* 0x2A '*' */ {0x08,0x2A,0x1C,0x2A,0x08,0x00},
    /* 0x2B '+' */ {0x08,0x08,0x3E,0x08,0x08,0x00},
    /* 0x2C ',' */ {0x00,0x50,0x30,0x00,0x00,0x00},
    /* 0x2D '-' */ {0x08,0x08,0x08,0x08,0x08,0x00},
    /* 0x2E '.' */ {0x00,0x60,0x60,0x00,0x00,0x00},
    /* 0x2F '/' */ {0x20,0x10,0x08,0x04,0x02,0x00},
    /* 0x30 '0' */ {0x3E,0x51,0x49,0x45,0x3E,0x00},
    /* 0x31 '1' */ {0x00,0x42,0x7F,0x40,0x00,0x00},
    /* 0x32 '2' */ {0x42,0x61,0x51,0x49,0x46,0x00},
    /* 0x33 '3' */ {0x21,0x41,0x45,0x4B,0x31,0x00},
    /* 0x34 '4' */ {0x18,0x14,0x12,0x7F,0x10,0x00},
    /* 0x35 '5' */ {0x27,0x45,0x45,0x45,0x39,0x00},
    /* 0x36 '6' */ {0x3C,0x4A,0x49,0x49,0x30,0x00},
    /* 0x37 '7' */ {0x01,0x71,0x09,0x05,0x03,0x00},
    /* 0x38 '8' */ {0x36,0x49,0x49,0x49,0x36,0x00},
    /* 0x39 '9' */ {0x06,0x49,0x49,0x29,0x1E,0x00},
    /* 0x3A ':' */ {0x00,0x36,0x36,0x00,0x00,0x00},
    /* 0x3B ';' */ {0x00,0x56,0x36,0x00,0x00,0x00},
    /* 0x3C '<' */ {0x00,0x08,0x14,0x22,0x41,0x00},
    /* 0x3D '=' */ {0x14,0x14,0x14,0x14,0x14,0x00},
    /* 0x3E '>' */ {0x41,0x22,0x14,0x08,0x00,0x00},
    /* 0x3F '?' */ {0x02,0x01,0x51,0x09,0x06,0x00},
    /* 0x40 '@' */ {0x32,0x49,0x79,0x41,0x3E,0x00},
    /* 0x41 'A' */ {0x7E,0x11,0x11,0x11,0x7E,0x00},
    /* 0x42 'B' */ {0x7F,0x49,0x49,0x49,0x36,0x00},
    /* 0x43 'C' */ {0x3E,0x41,0x41,0x41,0x22,0x00},
    /* 0x44 'D' */ {0x7F,0x41,0x41,0x22,0x1C,0x00},
    /* 0x45 'E' */ {0x7F,0x49,0x49,0x49,0x41,0x00},
    /* 0x46 'F' */ {0x7F,0x09,0x09,0x01,0x01,0x00},
    /* 0x47 'G' */ {0x3E,0x41,0x41,0x51,0x32,0x00},
    /* 0x48 'H' */ {0x7F,0x08,0x08,0x08,0x7F,0x00},
    /* 0x49 'I' */ {0x00,0x41,0x7F,0x41,0x00,0x00},
    /* 0x4A 'J' */ {0x20,0x40,0x41,0x3F,0x01,0x00},
    /* 0x4B 'K' */ {0x7F,0x08,0x14,0x22,0x41,0x00},
    /* 0x4C 'L' */ {0x7F,0x40,0x40,0x40,0x40,0x00},
    /* 0x4D 'M' */ {0x7F,0x02,0x04,0x02,0x7F,0x00},
    /* 0x4E 'N' */ {0x7F,0x04,0x08,0x10,0x7F,0x00},
    /* 0x4F 'O' */ {0x3E,0x41,0x41,0x41,0x3E,0x00},
    /* 0x50 'P' */ {0x7F,0x09,0x09,0x09,0x06,0x00},
    /* 0x51 'Q' */ {0x3E,0x41,0x51,0x21,0x5E,0x00},
    /* 0x52 'R' */ {0x7F,0x09,0x19,0x29,0x46,0x00},
    /* 0x53 'S' */ {0x46,0x49,0x49,0x49,0x31,0x00},
    /* 0x54 'T' */ {0x01,0x01,0x7F,0x01,0x01,0x00},
    /* 0x55 'U' */ {0x3F,0x40,0x40,0x40,0x3F,0x00},
    /* 0x56 'V' */ {0x1F,0x20,0x40,0x20,0x1F,0x00},
    /* 0x57 'W' */ {0x7F,0x20,0x18,0x20,0x7F,0x00},
    /* 0x58 'X' */ {0x63,0x14,0x08,0x14,0x63,0x00},
    /* 0x59 'Y' */ {0x03,0x04,0x78,0x04,0x03,0x00},
    /* 0x5A 'Z' */ {0x61,0x51,0x49,0x45,0x43,0x00},
    /* 0x5B '[' */ {0x00,0x00,0x7F,0x41,0x41,0x00},
    /* 0x5C '\' */ {0x02,0x04,0x08,0x10,0x20,0x00},
    /* 0x5D ']' */ {0x41,0x41,0x7F,0x00,0x00,0x00},
    /* 0x5E '^' */ {0x04,0x02,0x01,0x02,0x04,0x00},
    /* 0x5F '_' */ {0x40,0x40,0x40,0x40,0x40,0x00},
    /* 0x60 '`' */ {0x00,0x01,0x02,0x04,0x00,0x00},
    /* 0x61 'a' */ {0x20,0x54,0x54,0x54,0x78,0x00},
    /* 0x62 'b' */ {0x7F,0x48,0x44,0x44,0x38,0x00},
    /* 0x63 'c' */ {0x38,0x44,0x44,0x44,0x20,0x00},
    /* 0x64 'd' */ {0x38,0x44,0x44,0x48,0x7F,0x00},
    /* 0x65 'e' */ {0x38,0x54,0x54,0x54,0x18,0x00},
    /* 0x66 'f' */ {0x08,0x7E,0x09,0x01,0x02,0x00},
    /* 0x67 'g' */ {0x08,0x14,0x54,0x54,0x3C,0x00},
    /* 0x68 'h' */ {0x7F,0x08,0x04,0x04,0x78,0x00},
    /* 0x69 'i' */ {0x00,0x44,0x7D,0x40,0x00,0x00},
    /* 0x6A 'j' */ {0x20,0x40,0x44,0x3D,0x00,0x00},
    /* 0x6B 'k' */ {0x00,0x7F,0x10,0x28,0x44,0x00},
    /* 0x6C 'l' */ {0x00,0x41,0x7F,0x40,0x00,0x00},
    /* 0x6D 'm' */ {0x7C,0x04,0x18,0x04,0x78,0x00},
    /* 0x6E 'n' */ {0x7C,0x08,0x04,0x04,0x78,0x00},
    /* 0x6F 'o' */ {0x38,0x44,0x44,0x44,0x38,0x00},
    /* 0x70 'p' */ {0x7C,0x14,0x14,0x14,0x08,0x00},
    /* 0x71 'q' */ {0x08,0x14,0x14,0x18,0x7C,0x00},
    /* 0x72 'r' */ {0x7C,0x08,0x04,0x04,0x08,0x00},
    /* 0x73 's' */ {0x48,0x54,0x54,0x54,0x20,0x00},
    /* 0x74 't' */ {0x04,0x3F,0x44,0x40,0x20,0x00},
    /* 0x75 'u' */ {0x3C,0x40,0x40,0x20,0x7C,0x00},
    /* 0x76 'v' */ {0x1C,0x20,0x40,0x20,0x1C,0x00},
    /* 0x77 'w' */ {0x3C,0x40,0x30,0x40,0x3C,0x00},
    /* 0x78 'x' */ {0x44,0x28,0x10,0x28,0x44,0x00},
    /* 0x79 'y' */ {0x0C,0x50,0x50,0x50,0x3C,0x00},
    /* 0x7A 'z' */ {0x44,0x64,0x54,0x4C,0x44,0x00},
    /* 0x7B '{' */ {0x00,0x08,0x36,0x41,0x00,0x00},
    /* 0x7C '|' */ {0x00,0x00,0x7F,0x00,0x00,0x00},
    /* 0x7D '}' */ {0x00,0x41,0x36,0x08,0x00,0x00},
    /* 0x7E '~' */ {0x08,0x04,0x08,0x10,0x08,0x00},
};

/* ================================================================
 *  I2C 底层
 * ================================================================ */

static esp_err_t oled_write_cmd(uint8_t cmd)
{
    uint8_t buf[2] = {0x00, cmd};  /* Co=0, D/C#=0 (command) */
    return i2c_master_transmit(s_i2c_dev, buf, 2, OLED_I2C_TIMEOUT_MS);
}

static esp_err_t oled_write_data(const uint8_t *data, size_t len)
{
    /* Send data in chunks ≤32 bytes to avoid WDT (FIFO depth = 32).
     * Each chunk is a separate I2C transaction with Co=0, D/C#=1. */
    if (len == 0) return ESP_OK;

    size_t offset = 0;
    while (offset < len) {
        size_t chunk = len - offset;
        if (chunk > 31) chunk = 31;  /* 1 ctrl + 31 data = 32 = FIFO depth */
        uint8_t buf[32]; /* 1 cmd + 31 data = FIFO-friendly */
        buf[0] = 0x40;
        memcpy(buf + 1, data + offset, chunk);
        esp_err_t err = i2c_master_transmit(s_i2c_dev, buf, chunk + 1, OLED_I2C_TIMEOUT_MS);
        if (err != ESP_OK) return err;
        offset += chunk;
        vTaskDelay(pdMS_TO_TICKS(1)); /* let WDT breathe between chunks */
    }
    return ESP_OK;
}

/* ================================================================
 *  SSD1309 初始化序列
 * ================================================================ */

static esp_err_t oled_hw_init(void)
{
    /* 1. Init I2C master bus */
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port   = -1,
        .scl_io_num = MIMI_OLED_SCL_GPIO,
        .sda_io_num = MIMI_OLED_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus: %s", esp_err_to_name(err));
        return err;
    }

    /* 2. Scan I2C bus (informational) */
    ESP_LOGI(TAG, "Scanning I2C bus (SDA=%d, SCL=%d)...",
             MIMI_OLED_SDA_GPIO, MIMI_OLED_SCL_GPIO);
    int found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        if (i2c_master_probe(s_i2c_bus, addr, 50) == ESP_OK) {
            ESP_LOGI(TAG, "  I2C device found at 0x%02X!", addr);
            found++;
        }
    }
    if (found == 0) {
        ESP_LOGE(TAG, "No I2C devices found! Check wiring and power.");
        ESP_LOGE(TAG, "1. Is OLED VCC=3.3V, GND=GND?");
        ESP_LOGE(TAG, "2. Are SDA/SCL connected to correct pins?");
        ESP_LOGE(TAG, "3. Does OLED module have pull-up resistors?");
        ESP_LOGE(TAG, "   If not, add 4.7k from SDA to 3.3V and SCL to 3.3V.");
    }
    /* Feed watchdog after lengthy scan */
    vTaskDelay(pdMS_TO_TICKS(10));

    /* 3. Add device */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = OLED_I2C_ADDR,
        .scl_speed_hz    = OLED_I2C_FREQ_HZ,
    };
    err = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_i2c_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device: %s", esp_err_to_name(err));
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
        return err;
    }

    /* 4. SSD1309 init sequence (same as SSD1306) */
    oled_write_cmd(0xAE); /* display off */
    oled_write_cmd(0xD5); /* set osc clock */
    oled_write_cmd(0x80);
    oled_write_cmd(0xA8); /* set multiplex */
    oled_write_cmd(0x3F); /* 64 lines */
    oled_write_cmd(0xD3); /* set display offset */
    oled_write_cmd(0x00);
    oled_write_cmd(0x40); /* set start line */
    oled_write_cmd(0x8D); /* charge pump */
    oled_write_cmd(0x14); /* enable */
    oled_write_cmd(0x20); /* memory mode */
    oled_write_cmd(0x00); /* horizontal */
    oled_write_cmd(0xA1); /* segment remap — column 127 mapped to SEG0 */
    oled_write_cmd(0xC8); /* COM output scan direction — remapped */
    oled_write_cmd(0xDA); /* COM pins hardware config */
    oled_write_cmd(0x12); /* alternative */
    oled_write_cmd(0x81); /* contrast */
    oled_write_cmd(0xCF);
    oled_write_cmd(0xD9); /* pre-charge period */
    oled_write_cmd(0xF1);
    oled_write_cmd(0xDB); /* VCOMH deselect level */
    oled_write_cmd(0x40);
    oled_write_cmd(0xA4); /* resume to RAM content */
    oled_write_cmd(0xA6); /* normal display (not inverted) */
    oled_write_cmd(0x2E); /* deactivate scroll */
    oled_write_cmd(0xAF); /* display on */

    /* 5. Clear screen and probe device */
    memset(s_fb, 0, sizeof(s_fb));
    /* Probe: write one page — if no ACK, device is absent */
    esp_err_t probe_err = ESP_OK;
    oled_write_cmd(0xB0);  /* page 0 */
    oled_write_cmd(0x00);  /* column low */
    oled_write_cmd(0x10);  /* column high */
    probe_err = oled_write_data(s_fb[0], OLED_W);
    if (probe_err != ESP_OK) {
        ESP_LOGW(TAG, "OLED not detected (I2C NACK) — display disabled");
        s_hw_ok = false;
        /* Clean up I2C resources so they don't interfere with other subsystems */
        i2c_master_bus_rm_device(s_i2c_dev);
        s_i2c_dev = NULL;
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
        return ESP_OK;  /* graceful: no crash, no WDT timeout */
    }

    s_hw_ok = true;
    /* Write remaining pages */
    for (int page = 1; page < OLED_PAGES; page++) {
        oled_write_cmd(0xB0 + page);
        oled_write_cmd(0x00);
        oled_write_cmd(0x10);
        oled_write_data(s_fb[page], OLED_W);
    }

    return ESP_OK;
}

/* ================================================================
 *  绘制原语
 * ================================================================ */

static void fb_set_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= OLED_W || y < 0 || y >= OLED_H) return;
    if (on) {
        s_fb[y / 8][x] |= (1 << (y & 7));
    } else {
        s_fb[y / 8][x] &= ~(1 << (y & 7));
    }
}

static void fb_draw_char(int x, int y, char c)
{
    if (c < 0x20 || c > 0x7E) c = '?';
    const uint8_t *glyph = font6x8[(uint8_t)c - 0x20];
    for (int col = 0; col < FONT_W; col++) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < FONT_H; row++) {
            fb_set_pixel(x + col, y + row, (bits >> row) & 1);
        }
    }
}

static void fb_draw_str(int x, int y, const char *str)
{
    int cx = x;
    while (*str) {
        fb_draw_char(cx, y, *str);
        cx += FONT_W;
        if (cx > OLED_W - FONT_W) break;
        str++;
    }
}

/* ── 16×16 glyph drawing (CJK via HZK16) ──────────────────────── */
static void fb_draw_glyph16(int x, int y, const uint8_t *bitmap)
{
    /* bitmap: 32 bytes = 16 rows × 2 bytes/row, MSB-left per byte */
    for (int row = 0; row < HZK_GLYPH_H; row++) {
        uint8_t left  = bitmap[row * 2];
        uint8_t right = bitmap[row * 2 + 1];
        for (int col = 0; col < 8; col++) {
            if (left & (0x80 >> col)) {
                fb_set_pixel(x + col, y + row, true);
            }
            if (right & (0x80 >> col)) {
                fb_set_pixel(x + 8 + col, y + row, true);
            }
        }
    }
}

/* ── 8×16 ASCII glyph drawing (half-width) ────────────────────── */
static void fb_draw_char_8x16(int x, int y, char c)
{
    const uint8_t *glyph = font_ascii_8x16(c);
    if (!glyph) {
        /* Fallback: draw '?' using 16×16 glyph if available, else skip */
        uint8_t fbmp[HZK_GLYPH_BYTES];
        if (font_chinese_lookup('?', fbmp)) {
            fb_draw_glyph16(x, y, fbmp);
        }
        return;
    }
    /* glyph: 16 bytes = 8 columns × 2 bytes/col (rows 0-7, rows 8-15) */
    for (int col = 0; col < ASCII_W; col++) {
        uint8_t lo = glyph[col * 2];      /* rows 0-7 */
        uint8_t hi = glyph[col * 2 + 1];  /* rows 8-15 */
        for (int row = 0; row < 8; row++) {
            if (lo & (1 << row)) {
                fb_set_pixel(x + col, y + row, true);
            }
            if (hi & (1 << row)) {
                fb_set_pixel(x + col, y + 8 + row, true);
            }
        }
    }
}

/* ── UTF-8 aware string drawing with line wrap ────────────────── */
static int fb_draw_str_utf8(int x, int y, const char *str, int max_lines)
{
    int cx = x;
    int cy = y;
    int line = 0;
    int line_height = FONT_H;   /* current line height (upgraded to 16 on CJK) */
    const char *p = str;

    while (*p && line < max_lines) {
        uint32_t cp = utf8_decode(&p);
        if (cp == 0) continue;  /* skip invalid bytes */

        int char_w;
        bool is_wide = utf8_is_wide(cp);

        if (is_wide) {
            char_w = HZK_GLYPH_W;       /* 16 */
            line_height = HZK_GLYPH_H;  /* upgrade line height */
        } else if (cp >= 0x20 && cp < 0x7F) {
            char_w = ASCII_W;           /* 8 */
        } else {
            /* Control character: newline */
            if (cp == '\n') {
                cx = x;
                cy += line_height;
                line++;
                line_height = FONT_H;
                continue;
            }
            /* Other non-printable: render as '?' using ASCII width */
            char_w = ASCII_W;
        }

        /* Line wrap */
        if (cx + char_w > OLED_W) {
            cx = x;
            cy += line_height;
            line++;
            line_height = FONT_H;
            if (line >= max_lines) break;
        }

        /* Draw the character */
        if (is_wide) {
            uint8_t glyph[HZK_GLYPH_BYTES];
            if (font_chinese_lookup(cp, glyph)) {
                fb_draw_glyph16(cx, cy, glyph);
            } else {
                /* CJK char not in font — draw a box placeholder */
                memset(glyph, 0, sizeof(glyph));
                for (int i = 0; i < 16; i++) {
                    glyph[i * 2]     |= 0x80;  /* left edge */
                    glyph[i * 2 + 1] |= 0x01;  /* right edge */
                }
                glyph[0]  = 0xFF; glyph[1]  = 0xFF;  /* top border */
                glyph[30] = 0xFF; glyph[31] = 0xFF;  /* bottom border */
                fb_draw_glyph16(cx, cy, glyph);
            }
            cx += HZK_GLYPH_W;
        } else {
            fb_draw_char_8x16(cx, cy, (char)(cp & 0x7F));
            cx += ASCII_W;
        }
    }

    return cy + line_height;
}

static void fb_clear(void)
{
    memset(s_fb, 0, sizeof(s_fb));
}

static void fb_flush(void)
{
    if (!s_hw_ok) return;  /* OLED not connected — skip I2C */
    for (int page = 0; page < OLED_PAGES; page++) {
        oled_write_cmd(0xB0 + page);
        oled_write_cmd(0x00);
        oled_write_cmd(0x10);
        oled_write_data(s_fb[page], OLED_W);
        vTaskDelay(pdMS_TO_TICKS(1));  /* let WDT breathe */
    }
}

/* ================================================================
 *  Title Bar (top 16px of every page)
 * ================================================================ */

/* Page descriptor type — defined before use by title bar and draw fns */
typedef struct oled_page {
    const char *title;
    int8_t      parent;
    int8_t      children[4];
    bool        autorotate;
    void      (*draw)(void);
} oled_page_t;

/* s_pages[] is defined later — extern forward reference */
extern const oled_page_t s_pages[];

static void draw_title_bar(const char *title, int page_idx, int total,
                            bool is_sub_page)
{
    char buf[40];
    int y = 0;

    if (is_sub_page) {
        /* "← [2.1] 模型信息" — parent_pos is the autorotate index
         * of the top-level parent page (1-based) */
        int top = s_pages[page_idx].parent;
        int parent_pos = 0;
        for (int i = 0; i <= top && i < OLED_PAGE_COUNT; i++) {
            if (s_pages[i].autorotate && s_pages[i].parent < 0) parent_pos++;
        }
        snprintf(buf, sizeof(buf), "<- [%d.%d] %s",
                 parent_pos,
                 page_idx - s_pages[top].children[0] + 1,
                 title);
    } else {
        /* "[1/5] 系统状态" */
        /* Find this page's position in autorotate sequence */
        int pos = 0;
        for (int i = 0; i <= page_idx && i < OLED_PAGE_COUNT; i++) {
            if (s_pages[i].autorotate && s_pages[i].parent < 0) pos++;
        }
        snprintf(buf, sizeof(buf), "[%d/%d] %s", pos, total, title);
    }

    fb_draw_str(0, y, buf);
    /* Separator line (pixel row 8 → page 1, column 0-127) */
    for (int col = 0; col < OLED_W; col++) {
        fb_set_pixel(col, 8, true);
    }
}

/* ================================================================
 *  Page Descriptors
 * ================================================================ */

/* Forward declarations of all draw functions */
static void draw_page_system(void);
static void draw_page_wifi_detail(void);
static void draw_page_mem_detail(void);
static void draw_page_ai(void);
static void draw_page_model_info(void);
static void draw_page_session(void);
static void draw_page_network(void);
static void draw_page_espnow_peers(void);
static void draw_page_tasks(void);
static void draw_page_cron_list(void);
static void draw_page_heartbeat(void);
static void draw_page_message(void);

const oled_page_t s_pages[OLED_PAGE_COUNT] = {
    /*  0 */ { "System Status",   -1, { 1, 2,-1,-1 }, true,  draw_page_system      },
    /*  1 */ { "WiFi Detail",      0, {-1,-1,-1,-1 }, false, draw_page_wifi_detail  },
    /*  2 */ { "Memory Detail",    0, {-1,-1,-1,-1 }, false, draw_page_mem_detail   },
    /*  3 */ { "AI Agent",        -1, { 4, 5,-1,-1 }, true,  draw_page_ai           },
    /*  4 */ { "Model Info",       3, {-1,-1,-1,-1 }, false, draw_page_model_info   },
    /*  5 */ { "Session Info",     3, {-1,-1,-1,-1 }, false, draw_page_session      },
    /*  6 */ { "Network",         -1, { 7,-1,-1,-1 }, true,  draw_page_network      },
    /*  7 */ { "ESP-NOW Peers",    6, {-1,-1,-1,-1 }, false, draw_page_espnow_peers },
    /*  8 */ { "Tasks & Cron",    -1, { 9,10,-1,-1 }, true,  draw_page_tasks        },
    /*  9 */ { "Cron List",        8, {-1,-1,-1,-1 }, false, draw_page_cron_list    },
    /* 10 */ { "Heartbeat",        8, {-1,-1,-1,-1 }, false, draw_page_heartbeat    },
    /* 11 */ { "Messages",        -1, {-1,-1,-1,-1 }, true,  draw_page_message      },
};

/* Count top-level autorotate pages */
static int count_autorotate_pages(void)
{
    int n = 0;
    for (int i = 0; i < OLED_PAGE_COUNT; i++) {
        if (s_pages[i].autorotate && s_pages[i].parent < 0) n++;
    }
    return n;
}

/* ================================================================
 *  Navigation State
 * ================================================================ */

static int         s_current_page    = OLED_PAGE_SYSTEM;
static oled_mode_t s_mode            = OLED_MODE_AUTO;
static uint32_t    s_manual_start_ms = 0;
static uint32_t    s_page_start_ms   = 0;
static char        s_last_msg[256]   = {0};

#define OLED_MANUAL_TIMEOUT_MS  30000

/* ================================================================
 *  Page: System Status (0)
 * ================================================================ */
static void draw_page_system(void)
{
    int total = count_autorotate_pages();
    draw_title_bar("System Status", OLED_PAGE_SYSTEM, total, false);

    char buf[40];
    int y = 12;

    fb_draw_str(0, y, "MimiClaw v1.0");  y += 8;

    bool wifi_ok = wifi_manager_is_connected();
    snprintf(buf, sizeof(buf), "WiFi: %s", wifi_ok ? "Connected" : "Offline");
    fb_draw_str(0, y, buf);  y += 8;

    if (wifi_ok) {
        snprintf(buf, sizeof(buf), "IP:  %s", wifi_manager_get_ip());
    } else {
        snprintf(buf, sizeof(buf), "IP:  ---");
    }
    fb_draw_str(0, y, buf);  y += 8;

#if __has_include("espnow/esp_now_device.h")
    snprintf(buf, sizeof(buf), "Role:%s(%s)",
             esp_now_device_is_master() ? "M" : "S",
             esp_now_device_get_id());
#else
    snprintf(buf, sizeof(buf), "Role: STANDALONE");
#endif
    fb_draw_str(0, y, buf);  y += 8;

    int free_kb = (int)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    snprintf(buf, sizeof(buf), "Mem: %dKB free", free_kb);
    fb_draw_str(0, y, buf);  y += 8;

    int uptime_s = (int)(esp_timer_get_time() / 1000000);
    int h = uptime_s / 3600, m = (uptime_s % 3600) / 60, s = uptime_s % 60;
    snprintf(buf, sizeof(buf), "Up: %dh%02dm%02ds", h, m, s);
    fb_draw_str(0, y, buf);
}

/* ================================================================
 *  Page: WiFi Detail (1)
 * ================================================================ */
static void draw_page_wifi_detail(void)
{
    draw_title_bar("WiFi Detail", OLED_PAGE_WIFI_DETAIL, 0, true);
    char buf[40];
    int y = 12;

    wifi_ap_record_t ap;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap);
    if (err == ESP_OK) {
        snprintf(buf, sizeof(buf), "SSID: %s", (const char *)ap.ssid);
        fb_draw_str(0, y, buf);  y += 8;
        snprintf(buf, sizeof(buf), "RSSI: %ddBm CH:%d", ap.rssi, ap.primary);
        fb_draw_str(0, y, buf);  y += 8;
        snprintf(buf, sizeof(buf), "BSSID:%02X:%02X:%02X...",
                 ap.bssid[0], ap.bssid[1], ap.bssid[2]);
        fb_draw_str(0, y, buf);  y += 8;
    } else {
        fb_draw_str(0, y, "WiFi: not connected");  y += 8;
    }

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(buf, sizeof(buf), "MAC:%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    fb_draw_str(0, y, buf);
}

/* ================================================================
 *  Page: Memory Detail (2)
 * ================================================================ */
static void draw_page_mem_detail(void)
{
    draw_title_bar("Memory Detail", OLED_PAGE_MEM_DETAIL, 0, true);
    char buf[40];
    int y = 12;

    size_t dram_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t dram_free  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t dram_min   = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    snprintf(buf, sizeof(buf), "DRAM: %d/%dKB",
             (int)(dram_free/1024), (int)(dram_total/1024));
    fb_draw_str(0, y, buf);  y += 8;
    snprintf(buf, sizeof(buf), "min free: %dKB", (int)(dram_min/1024));
    fb_draw_str(0, y, buf);  y += 8;

    size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t psram_free  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    snprintf(buf, sizeof(buf), "PSRAM: %d/%dKB",
             (int)(psram_free/1024), (int)(psram_total/1024));
    fb_draw_str(0, y, buf);  y += 8;

    int frag = dram_free > 0
        ? (int)(100 - ((int64_t)dram_min * 100 / dram_free)) : 0;
    snprintf(buf, sizeof(buf), "Frag: ~%d%%", frag);
    fb_draw_str(0, y, buf);
}

/* ================================================================
 *  Page: AI Agent (3)
 * ================================================================ */
static void draw_page_ai(void)
{
    int total = count_autorotate_pages();
    draw_title_bar("AI Agent", OLED_PAGE_AI, total, false);
    char buf[40];
    int y = 12;

    /* Model / provider from config */
    const char *model = MIMI_SECRET_MODEL;
    const char *prov  = MIMI_SECRET_MODEL_PROVIDER;
    if (!model || model[0] == '\0') model = "(not set)";
    if (!prov  || prov[0]  == '\0') prov  = "?";

    snprintf(buf, sizeof(buf), "M:%.16s", model);
    fb_draw_str(0, y, buf);  y += 8;
    snprintf(buf, sizeof(buf), "P:%s", prov);
    fb_draw_str(0, y, buf);  y += 8;

    bool wifi_ok = wifi_manager_is_connected();
    fb_draw_str(0, y, wifi_ok ? "API: ready" : "API: offline");  y += 8;

    /* Last message timestamp */
    if (s_last_msg[0]) {
        fb_draw_str(0, y, "Last: active");
    } else {
        fb_draw_str(0, y, "Last: idle");
    }
}

/* ================================================================
 *  Page: Model Info (4)
 * ================================================================ */
static void draw_page_model_info(void)
{
    draw_title_bar("Model Info", OLED_PAGE_MODEL_INFO, 0, true);
    char buf[40];
    int y = 12;

    snprintf(buf, sizeof(buf), "Model: %s",
             MIMI_SECRET_MODEL[0] ? MIMI_SECRET_MODEL : "(unset)");
    fb_draw_str(0, y, buf);  y += 8;

    snprintf(buf, sizeof(buf), "Provider: %s",
             MIMI_SECRET_MODEL_PROVIDER[0] ? MIMI_SECRET_MODEL_PROVIDER : "?");
    fb_draw_str(0, y, buf);  y += 8;

    fb_draw_str(0, y, "Max iter: 10");  y += 8;
    fb_draw_str(0, y, "Stream: off");
}

/* ================================================================
 *  Page: Session Info (5)
 * ================================================================ */
static void draw_page_session(void)
{
    draw_title_bar("Session Info", OLED_PAGE_SESSION, 0, true);
    char buf[40];
    int y = 12;

    int psram_kb = (int)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
    snprintf(buf, sizeof(buf), "PSRAM free: %dKB", psram_kb);
    fb_draw_str(0, y, buf);  y += 8;

    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    strftime(buf, sizeof(buf), "%m-%d %H:%M", &local);
    fb_draw_str(0, y, buf);  y += 8;

    int uptime_s = (int)(esp_timer_get_time() / 1000000);
    snprintf(buf, sizeof(buf), "Up: %dh%02dm", uptime_s / 3600,
             (uptime_s % 3600) / 60);
    fb_draw_str(0, y, buf);
}

/* ================================================================
 *  Page: Network (6)
 * ================================================================ */
static void draw_page_network(void)
{
    int total = count_autorotate_pages();
    draw_title_bar("Network", OLED_PAGE_NETWORK, total, false);
    char buf[40];
    int y = 12;

    bool wifi_ok = wifi_manager_is_connected();
    snprintf(buf, sizeof(buf), "WiFi: %s", wifi_ok ? "OK" : "Down");
    fb_draw_str(0, y, buf);  y += 8;

    if (wifi_ok) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            snprintf(buf, sizeof(buf), "RSSI: %ddBm", ap.rssi);
            fb_draw_str(0, y, buf);  y += 8;
        }
    }

#if __has_include("espnow/esp_now_device.h")
    int peers = 0; /* TODO: expose esp_now peer count */
    snprintf(buf, sizeof(buf), "ESP-NOW: %d peers", peers);
    fb_draw_str(0, y, buf);  y += 8;
#endif

    fb_draw_str(0, y, "WS: port 18789");
}

/* ================================================================
 *  Page: ESP-NOW Peers (7)
 * ================================================================ */
static void draw_page_espnow_peers(void)
{
    draw_title_bar("ESP-NOW Peers", OLED_PAGE_ESPNOW_PEERS, 0, true);
    int y = 12;

#if __has_include("espnow/esp_now_device.h")
    fb_draw_str(0, y, "ID: "); /* simplified */
    fb_draw_str(18, y, esp_now_device_get_id());
    y += 8;
    fb_draw_str(0, y, esp_now_device_is_master() ? "Role: Master" : "Role: Slave");
    y += 8;
    fb_draw_str(0, y, "(peer list TBD)");
#else
    fb_draw_str(0, y, "ESP-NOW: disabled");  y += 8;
    fb_draw_str(0, y, "Standalone mode");
#endif
}

/* ================================================================
 *  Page: Tasks & Cron (8)
 * ================================================================ */
static void draw_page_tasks(void)
{
    int total = count_autorotate_pages();
    draw_title_bar("Tasks & Cron", OLED_PAGE_TASKS, total, false);
    char buf[40];
    int y = 12;

    const cron_job_t *jobs;
    int cron_count;
    cron_list_jobs(&jobs, &cron_count);
    int active = 0;
    for (int i = 0; i < cron_count; i++) {
        if (jobs[i].enabled) active++;
    }
    snprintf(buf, sizeof(buf), "Cron: %d/%d jobs", active, cron_count);
    fb_draw_str(0, y, buf);  y += 8;

    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    strftime(buf, sizeof(buf), "Time: %H:%M:%S", &local);
    fb_draw_str(0, y, buf);  y += 8;

    int uptime_s = (int)(esp_timer_get_time() / 1000000);
    snprintf(buf, sizeof(buf), "Up: %dh%02dm", uptime_s / 3600,
             (uptime_s % 3600) / 60);
    fb_draw_str(0, y, buf);
}

/* ================================================================
 *  Page: Cron List (9)
 * ================================================================ */
static void draw_page_cron_list(void)
{
    draw_title_bar("Cron List", OLED_PAGE_CRON_LIST, 0, true);
    char buf[40];
    int y = 12;

    const cron_job_t *jobs;
    int count;
    cron_list_jobs(&jobs, &count);

    if (count == 0) {
        fb_draw_str(0, y, "(no cron jobs)");
        return;
    }

    int shown = 0;
    for (int i = 0; i < count && shown < 3; i++) {
        if (!jobs[i].enabled) continue;
        snprintf(buf, sizeof(buf), "%s", jobs[i].name);
        fb_draw_str(0, y, buf);  y += 8;
        shown++;
    }
}

/* ================================================================
 *  Page: Heartbeat (10)
 * ================================================================ */
static void draw_page_heartbeat(void)
{
    draw_title_bar("Heartbeat", OLED_PAGE_HEARTBEAT, 0, true);
    char buf[40];
    int y = 12;

    snprintf(buf, sizeof(buf), "Interval: %dmin",
             MIMI_HEARTBEAT_INTERVAL_MS / 60000);
    fb_draw_str(0, y, buf);  y += 8;

    fb_draw_str(0, y, "Source: HEARTBEAT.md");  y += 8;

    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    strftime(buf, sizeof(buf), "Now: %H:%M:%S", &local);
    fb_draw_str(0, y, buf);
}

/* ================================================================
 *  Page: Messages (11)
 * ================================================================ */
static void draw_page_message(void)
{
    int total = count_autorotate_pages();
    draw_title_bar("Messages", OLED_PAGE_MESSAGE, total, false);

    if (s_last_msg[0] == '\0') {
        fb_draw_str(0, 24, "(no messages yet)");
    } else {
        fb_draw_str_utf8(0, 12, s_last_msg, 3);
    }
}

/* ================================================================
 *  Refresh Task
 * ================================================================ */

static void draw_page(int page_idx)
{
    if (page_idx >= 0 && page_idx < OLED_PAGE_COUNT) {
        fb_clear();
        s_pages[page_idx].draw();
        fb_flush();
    }
}

static void oled_task(void *arg)
{
    (void)arg;

    /* If no hardware, exit immediately — don't waste stack/CPU */
    if (!s_hw_ok) {
        ESP_LOGW(TAG, "OLED task: no hardware, exiting");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "OLED refresh task started");

    /* Boot splash — deferred to avoid WDT during I2C flush */
    fb_clear();
    fb_draw_str(12, 24, "MimiClaw");
    fb_draw_str(12, 40, "Booting...");
    ESP_LOGI(TAG, "Splash drawn, flushing...");
    fb_flush();
    ESP_LOGI(TAG, "Splash flush done");

    s_page_start_ms = (uint32_t)(esp_timer_get_time() / 1000);

    while (1) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

        switch (s_mode) {

        case OLED_MODE_AUTO:
            /* Auto-rotate through top-level autorotate pages */
            if (now_ms - s_page_start_ms >= MIMI_OLED_PAGE_SWITCH_MS) {
                /* Find next autorotate page */
                int next = (s_current_page + 1) % OLED_PAGE_COUNT;
                while (!s_pages[next].autorotate || s_pages[next].parent >= 0) {
                    next = (next + 1) % OLED_PAGE_COUNT;
                    if (next == s_current_page) break; /* safety */
                }
                s_current_page = next;
                s_page_start_ms = now_ms;
            }
            draw_page(s_current_page);
            break;

        case OLED_MODE_MANUAL:
            /* Fixed page — check timeout */
            if (now_ms - s_manual_start_ms >= OLED_MANUAL_TIMEOUT_MS) {
                s_mode = OLED_MODE_AUTO;
                s_page_start_ms = now_ms;
                ESP_LOGI(TAG, "Manual timeout, returning to auto-rotate");
            }
            draw_page(s_current_page);
            break;

        case OLED_MODE_MENU:
            /* Future: button-driven cursor navigation */
            draw_page(s_current_page);
            /* Inactivity timeout → back to AUTO */
            if (now_ms - s_manual_start_ms >= OLED_MANUAL_TIMEOUT_MS) {
                s_mode = OLED_MODE_AUTO;
                s_page_start_ms = now_ms;
            }
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(MIMI_OLED_REFRESH_MS));
    }
}

/* ================================================================
 *  Public API
 * ================================================================ */

esp_err_t oled_display_init(void)
{
    if (s_inited) return ESP_OK;

    esp_err_t err = oled_hw_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OLED hardware init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Attempt to load Chinese font (graceful degradation if missing) */
    font_chinese_init();
    /* Feed watchdog after font load (267KB from SPIFFS into PSRAM can take time) */
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Button GPIOs (all on free pins: UP=45, DOWN=5, SEL=6, BACK=7) */
    oled_display_btn_init();

    BaseType_t ok = xTaskCreate(
        oled_task, "oled", 4096, NULL,
        2, /* Low priority */
        &s_task
    );
    if (ok != pdPASS || !s_task) {
        ESP_LOGE(TAG, "Failed to create OLED task");
        return ESP_FAIL;
    }

    s_inited = true;
    ESP_LOGI(TAG, "OLED display initialized (SDA=%d, SCL=%d)",
             MIMI_OLED_SDA_GPIO, MIMI_OLED_SCL_GPIO);
    return ESP_OK;
}

void oled_display_set_last_msg(const char *msg)
{
    if (msg) {
        strncpy(s_last_msg, msg, sizeof(s_last_msg) - 1);
        s_last_msg[sizeof(s_last_msg) - 1] = '\0';
    }
}

void oled_display_set_page(int page_index)
{
    if (page_index < 0 || page_index >= OLED_PAGE_COUNT) return;
    s_current_page = page_index;
    s_mode = OLED_MODE_MANUAL;
    s_manual_start_ms = (uint32_t)(esp_timer_get_time() / 1000);
    ESP_LOGI(TAG, "Manual page: %d (%s)", page_index,
             s_pages[page_index].title);
}

void oled_display_set_mode(oled_mode_t mode)
{
    s_mode = mode;
    s_page_start_ms   = (uint32_t)(esp_timer_get_time() / 1000);
    s_manual_start_ms = s_page_start_ms;
    ESP_LOGI(TAG, "Mode: %s",
             mode == OLED_MODE_AUTO   ? "AUTO" :
             mode == OLED_MODE_MANUAL ? "MANUAL" : "MENU");
}

int oled_display_get_page(void)
{
    return s_current_page;
}

const char *oled_display_get_page_title(void)
{
    if (s_current_page >= 0 && s_current_page < OLED_PAGE_COUNT) {
        return s_pages[s_current_page].title;
    }
    return "?";
}

oled_mode_t oled_display_get_mode(void)
{
    return s_mode;
}

/* ── Button Hardware: pure polling + debounce ──────────────────── */

#define BTN_SCAN_MS         20    /* 50 Hz scan rate */
#define BTN_DEBOUNCE_CNT     2    /* N consecutive lows → valid press */

/* Order must match s_btn_gpios[] */
typedef void (*btn_handler_t)(void);
static const btn_handler_t s_btn_handlers[4] = {
    oled_display_btn_up,
    oled_display_btn_down,
    oled_display_btn_select,
    oled_display_btn_back,
};

static void btn_task(void *arg)
{
    (void)arg;
    while (1) {
        for (int i = 0; i < 4; i++) {
            if (gpio_get_level(s_btn_gpios[i]) == 0) {
                /* Pressed */
                if (s_btn_debounce[i] < BTN_DEBOUNCE_CNT) {
                    s_btn_debounce[i]++;
                    if (s_btn_debounce[i] == BTN_DEBOUNCE_CNT) {
                        s_btn_handlers[i]();
                    }
                }
            } else {
                /* Released — wind down */
                if (s_btn_debounce[i] > 0) {
                    s_btn_debounce[i]--;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(BTN_SCAN_MS));
    }
}

esp_err_t oled_display_btn_init(void)
{
    if (s_btn_task) return ESP_OK;

    /* Configure each button GPIO as input with pull-up (no ISR) */
    for (int i = 0; i < 4; i++) {
        gpio_config_t cfg = {
            .pin_bit_mask = (1ULL << s_btn_gpios[i]),
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        esp_err_t err = gpio_config(&cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "btn GPIO%d config failed: %s",
                     s_btn_gpios[i], esp_err_to_name(err));
            return err;
        }
    }

    BaseType_t ok = xTaskCreate(btn_task, "oled_btn", 2048, NULL,
                                 1, &s_btn_task);  /* low priority, don't preempt OLED */
    if (ok != pdPASS || !s_btn_task) {
        ESP_LOGE(TAG, "btn task create failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Buttons init ok (UP=%d DOWN=%d SEL=%d BACK=%d)",
             s_btn_gpios[0], s_btn_gpios[1],
             s_btn_gpios[2], s_btn_gpios[3]);
    return ESP_OK;
}

/* ── Button Placeholders (future 4-button hardware) ────────────── */

void oled_display_btn_up(void)
{
    if (s_mode == OLED_MODE_MENU) {
        /* Navigate to previous sibling page */
        int parent = s_pages[s_current_page].parent;
        int prev   = s_current_page;
        do {
            prev = (prev - 1 + OLED_PAGE_COUNT) % OLED_PAGE_COUNT;
            if (s_pages[prev].parent == parent) break;
        } while (prev != s_current_page);
        if (prev != s_current_page) s_current_page = prev;
    } else {
        /* Exit auto/manual → enter menu mode */
        s_mode = OLED_MODE_MENU;
    }
    s_manual_start_ms = (uint32_t)(esp_timer_get_time() / 1000);
}

void oled_display_btn_down(void)
{
    if (s_mode == OLED_MODE_MENU) {
        int parent = s_pages[s_current_page].parent;
        int next   = s_current_page;
        do {
            next = (next + 1) % OLED_PAGE_COUNT;
            if (s_pages[next].parent == parent) break;
        } while (next != s_current_page);
        if (next != s_current_page) s_current_page = next;
    } else {
        s_mode = OLED_MODE_MENU;
    }
    s_manual_start_ms = (uint32_t)(esp_timer_get_time() / 1000);
}

void oled_display_btn_select(void)
{
    if (s_mode == OLED_MODE_MENU) {
        /* Drill down to first child */
        const oled_page_t *page = &s_pages[s_current_page];
        if (page->children[0] >= 0) {
            s_current_page = page->children[0];
        }
    } else {
        s_mode = OLED_MODE_MENU;
    }
    s_manual_start_ms = (uint32_t)(esp_timer_get_time() / 1000);
}

void oled_display_btn_back(void)
{
    if (s_mode == OLED_MODE_MENU) {
        int parent = s_pages[s_current_page].parent;
        if (parent >= 0) {
            s_current_page = parent;
        } else {
            /* At top level → return to auto-rotate */
            s_mode = OLED_MODE_AUTO;
            s_page_start_ms = (uint32_t)(esp_timer_get_time() / 1000);
            return;
        }
    } else {
        s_mode = OLED_MODE_AUTO;
        s_page_start_ms = (uint32_t)(esp_timer_get_time() / 1000);
        return;
    }
    s_manual_start_ms = (uint32_t)(esp_timer_get_time() / 1000);
}
