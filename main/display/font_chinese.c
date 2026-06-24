/**
 * @file font_chinese.c
 * @brief Chinese font subsystem — HZK16 loader + 8×16 ASCII font.
 *
 * Loads the HZK16 binary (267 KB) from SPIFFS into PSRAM on init.
 * Provides Unicode→glyph lookup via a sorted mapping table (generated
 * by scripts/generate_font_data.py) with binary search.
 *
 * HZK16 glyph format: 16 rows × 2 bytes/row, MSB-left per byte.
 *   Row r: byte[0] = columns 0-7, byte[1] = columns 8-15.
 */

#include "display/font_chinese.h"
#include "display/font_chinese_data.h"
#include "mimi_config.h"

#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "font_chn";

/* ── HZK16 data loaded into PSRAM ──────────────────────────────── */
static uint8_t *s_hzk16 = NULL;
static bool     s_ready = false;

/* ── 8×16 ASCII font (auto-generated) ──────────────────────────── */
#include "display/ascii_font_8x16.inc"

/* ================================================================
 *  Init
 * ================================================================ */

esp_err_t font_chinese_init(void)
{
    if (s_ready) return ESP_OK;

    /* Open font file from SPIFFS */
    FILE *f = fopen(MIMI_FONT_CHINESE_PATH, "rb");
    if (!f) {
        ESP_LOGW(TAG, "Chinese font not found at %s — CJK rendering disabled",
                 MIMI_FONT_CHINESE_PATH);
        s_ready = true;  /* mark as "initialised but empty" */
        return ESP_OK;   /* graceful degradation: Chinese chars show as '?' */
    }

    /* Check file size */
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize < MIMI_FONT_HZK16_SIZE) {
        ESP_LOGW(TAG, "HZK16 file too small: %ld (expected %d)",
                 fsize, MIMI_FONT_HZK16_SIZE);
        fclose(f);
        s_ready = true;
        return ESP_OK;
    }

    /* Allocate PSRAM buffer */
    s_hzk16 = heap_caps_calloc(1, MIMI_FONT_HZK16_SIZE, MALLOC_CAP_SPIRAM);
    if (!s_hzk16) {
        ESP_LOGE(TAG, "Failed to allocate %d bytes in PSRAM for HZK16",
                 MIMI_FONT_HZK16_SIZE);
        fclose(f);
        s_ready = true;
        return ESP_ERR_NO_MEM;
    }

    /* Read entire font into PSRAM */
    size_t read_bytes = fread(s_hzk16, 1, MIMI_FONT_HZK16_SIZE, f);
    fclose(f);

    if (read_bytes < (size_t)MIMI_FONT_HZK16_SIZE) {
        ESP_LOGW(TAG, "Short read: %d/%d bytes", (int)read_bytes,
                 MIMI_FONT_HZK16_SIZE);
    }

    s_ready = true;
    ESP_LOGI(TAG, "Loaded HZK16 font: %d bytes into PSRAM, %d CJK glyphs",
             MIMI_FONT_HZK16_SIZE, UNICODE_TO_HZK_COUNT);
    return ESP_OK;
}

/* ================================================================
 *  Glyph Lookup
 * ================================================================ */

bool font_chinese_lookup(uint32_t unicode, uint8_t bitmap[HZK_GLYPH_BYTES])
{
    if (!s_hzk16 || unicode == 0) {
        return false;
    }

    uint16_t offset_32;
    if (!hzk_lookup(unicode, &offset_32)) {
        return false;
    }

    /* offset_32 is in units of 32 bytes */
    uint32_t byte_offset = (uint32_t)offset_32 * 32;
    if (byte_offset + 32 > (uint32_t)MIMI_FONT_HZK16_SIZE) {
        return false;
    }

    memcpy(bitmap, s_hzk16 + byte_offset, HZK_GLYPH_BYTES);
    return true;
}

const uint8_t *font_ascii_8x16(char c)
{
    if (c < 0x20 || c > 0x7E) {
        return NULL;
    }
    return ascii_font_8x16[(uint8_t)c - 0x20];
}

bool font_chinese_is_ready(void)
{
    return s_ready;
}
