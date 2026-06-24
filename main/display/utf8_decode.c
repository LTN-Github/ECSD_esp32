/**
 * @file utf8_decode.c
 * @brief Minimal UTF-8 → Unicode codepoint decoder.
 *
 * Pure C, no dynamic allocation, designed for embedded use.
 * Handles 1–4 byte sequences per RFC 3629.
 */

#include "display/utf8_decode.h"

/* ── UTF-8 byte-length table (indexed by first-byte high nibble) ────
 * Returns 0 for invalid leading bytes (0x80–0xBF, 0xFE–0xFF).        */
static const uint8_t s_utf8_len[16] = {
    /* 0x0_  0x1_  0x2_  0x3_  0x4_  0x5_  0x6_  0x7_ */
    1, 1, 1, 1, 1, 1, 1, 1,
    /* 0x8_  0x9_  0xA_  0xB_  0xC_  0xD_  0xE_  0xF_ */
    0, 0, 0, 0, 2, 2, 3, 4,
};

uint32_t utf8_decode(const char **p)
{
    const uint8_t *s = (const uint8_t *)*p;
    uint8_t c = *s;

    if (c == 0) {
        return 0;  /* NUL terminator — caller should stop */
    }

    /* ── ASCII (fast path) ─────────────────────────────────────── */
    if (c < 0x80) {
        (*p)++;
        return c;
    }

    /* ── Multi-byte ────────────────────────────────────────────── */
    uint8_t len = s_utf8_len[c >> 4];
    if (len < 2) {
        /* Invalid leading byte (0x80–0xBF continuation, or 0xFE/0xFF) */
        (*p)++;
        return 0;
    }

    /* Check that all continuation bytes are present and valid */
    uint32_t cp = c & ((1u << (7 - len)) - 1);  /* mask for data bits */

    for (uint8_t i = 1; i < len; i++) {
        uint8_t cont = s[i];
        if ((cont & 0xC0) != 0x80) {
            /* Truncated or invalid sequence — skip what we can */
            *p += (i > 0) ? i : 1;
            return 0;
        }
        cp = (cp << 6) | (cont & 0x3F);
    }

    (*p) += len;

    /* ── Overlong / surrogate / out-of-range rejection ─────────── */
    if (cp > 0x10FFFF) return 0;
    if (cp >= 0xD800 && cp <= 0xDFFF) return 0;  /* surrogates */

    /* Reject overlong encodings */
    if (len == 2 && cp < 0x80)   return 0;
    if (len == 3 && cp < 0x800)  return 0;
    if (len == 4 && cp < 0x10000) return 0;

    return cp;
}

/* ── Wide-character detection ────────────────────────────────────── */

bool utf8_is_wide(uint32_t cp)
{
    return (cp >= 0x1100 && cp <= 0x115F)   ||  /* Hangul Jamo          */
           (cp >= 0x2329 && cp <= 0x232A)   ||  /* angle brackets       */
           (cp >= 0x2E80 && cp <= 0x303E)   ||  /* CJK Radicals … Kana  */
           (cp >= 0x3041 && cp <= 0x33BF)   ||  /* Hiragana … CJK compat*/
           (cp >= 0x3400 && cp <= 0x4DBF)   ||  /* CJK Ext-A            */
           (cp >= 0x4E00 && cp <= 0x9FFF)   ||  /* CJK Unified          */
           (cp >= 0xA000 && cp <= 0xA4CF)   ||  /* Yi                   */
           (cp >= 0xAC00 && cp <= 0xD7AF)   ||  /* Hangul Syllables     */
           (cp >= 0xF900 && cp <= 0xFAFF)   ||  /* CJK Compat           */
           (cp >= 0xFE10 && cp <= 0xFE1F)   ||  /* Vertical forms       */
           (cp >= 0xFE30 && cp <= 0xFE6F)   ||  /* CJK compat forms     */
           (cp >= 0xFF00 && cp <= 0xFF60)   ||  /* Fullwidth Latin      */
           (cp >= 0xFFE0 && cp <= 0xFFE6)   ||  /* Fullwidth signs      */
           (cp >= 0x1F000 && cp <= 0x1F02F) ||  /* Mahjong tiles        */
           (cp >= 0x1F0A0 && cp <= 0x1F0FF) ||  /* Playing cards        */
           (cp >= 0x1F100 && cp <= 0x1F64F) ||  /* Enclosed … Emoticons */
           (cp >= 0x1F680 && cp <= 0x1F6FF) ||  /* Transport            */
           (cp >= 0x20000 && cp <= 0x2FFFD) ||  /* CJK Ext-B+ (SIP)     */
           (cp >= 0x30000 && cp <= 0x3FFFD);     /* CJK Ext-G+ (TIP)    */
}
