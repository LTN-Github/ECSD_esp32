#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Decode the next Unicode codepoint from a UTF-8 string.
 *
 * Advances *p past the consumed bytes.  Handles 1–4 byte sequences.
 * Returns 0 for an invalid or incomplete sequence (rendered as '?').
 *
 * @param p   Pointer to string pointer (advanced in-place)
 * @return    Unicode codepoint (0–0x10FFFF), or 0 on error
 *
 * Usage:
 *   const char *s = "Hello中文";
 *   while (*s) {
 *       uint32_t cp = utf8_decode(&s);
 *       // render cp
 *   }
 */
uint32_t utf8_decode(const char **p);

/**
 * @brief Check if a Unicode codepoint is a CJK character (wide).
 *
 * Covers:
 *   U+4E00–U+9FFF   CJK Unified Ideographs
 *   U+3400–U+4DBF   CJK Unified Ideographs Extension A
 *   U+F900–U+FAFF   CJK Compatibility Ideographs
 *   U+2F800–U+2FA1F CJK Compatibility Supplement
 *   U+3000–U+303F   CJK Symbols and Punctuation (fullwidth)
 *   U+FF00–U+FFEF   Halfwidth and Fullwidth Forms (fullwidth)
 *   U+2000–U+206F   General Punctuation (em-dash etc.)
 *
 * @return true if this codepoint should take 2× the width of ASCII
 */
bool utf8_is_wide(uint32_t cp);
