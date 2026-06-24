#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Chinese font subsystem — loads HZK16 from SPIFFS into PSRAM.
 *
 * Provides 16×16 bitmap glyphs for CJK characters via Unicode codepoint
 * lookup, plus an embedded 8×16 ASCII font for half-width text.
 */

/* ── Glyph dimensions ────────────────────────────────────────────── */
#define HZK_GLYPH_W   16
#define HZK_GLYPH_H   16
#define HZK_GLYPH_BYTES 32    /* 16 rows × 2 bytes/row */

#define ASCII_W   8
#define ASCII_H  16
#define ASCII_BYTES 16         /* 8 cols × 16 rows = 16 bytes */

/**
 * @brief Initialize the Chinese font subsystem.
 *
 * Loads the HZK16 binary from SPIFFS into PSRAM.
 * Safe to call before WiFi is connected (SPIFFS must be mounted).
 *
 * @return ESP_OK on success, or error code if font file is missing/corrupt.
 */
esp_err_t font_chinese_init(void);

/**
 * @brief Look up a 16×16 glyph by Unicode codepoint.
 *
 * @param unicode   Unicode codepoint (e.g. 0x4E2D for 中)
 * @param bitmap    [out] 32 bytes of pixel data (row-major, 2 bytes/row, MSB-left)
 * @return true if glyph was found and copied, false if character not in font
 */
bool font_chinese_lookup(uint32_t unicode, uint8_t bitmap[HZK_GLYPH_BYTES]);

/**
 * @brief Get the 8×16 ASCII glyph for a printable character.
 *
 * @param c   ASCII character (0x20–0x7E)
 * @return pointer to 16-byte glyph data (row-major, 1 byte/col, MSB-top),
 *         or NULL for non-printable characters
 */
const uint8_t *font_ascii_8x16(char c);

/**
 * @brief Check if the font subsystem is loaded.
 */
bool font_chinese_is_ready(void);
