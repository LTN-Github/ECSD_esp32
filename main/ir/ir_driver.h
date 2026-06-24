#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ── Data Types ─────────────────────────────────────────────── */

/* NEC decode result */
typedef struct {
    uint32_t address;     /* 8-bit (standard) or 16-bit (extended) address */
    uint32_t command;     /* 8-bit command */
    bool     is_repeat;   /* True if this was a repeat frame */
    bool     extended;    /* True if 16-bit address detected */
} ir_nec_frame_t;

/* Raw IR capture (pulse/space pairs in microseconds) */
typedef struct {
    uint16_t *raw;        /* Array of pulse/space durations in us */
    size_t    count;      /* Number of entries */
} ir_raw_capture_t;

/* Supported protocol IDs */
typedef enum {
    IR_PROTO_NEC = 0,
    IR_PROTO_RAW = 1,
} ir_protocol_t;

/* ── Driver Lifecycle ───────────────────────────────────────── */

/**
 * Initialize IR RMT channels (RX on MIMI_IR_RX_GPIO, TX on MIMI_IR_TX_GPIO).
 * Safe to call multiple times (idempotent).
 */
esp_err_t ir_driver_init(void);

/* ── Receive ────────────────────────────────────────────────── */

/**
 * Listen for an IR signal. Blocking call with timeout.
 * Fills capture with pulse/space durations in microseconds.
 * Caller must free capture->raw when done.
 *
 * @param timeout_ms  Max time to wait for a signal (0 = wait forever)
 */
esp_err_t ir_receive_raw(ir_raw_capture_t *capture, uint32_t timeout_ms);

/**
 * Attempt to decode a raw capture as NEC protocol.
 * Returns ESP_OK if decoded, ESP_ERR_NOT_FOUND if not NEC.
 */
esp_err_t ir_decode_nec(const ir_raw_capture_t *capture, ir_nec_frame_t *frame);

/* ── Transmit ───────────────────────────────────────────────── */

/**
 * Send a NEC frame (modulated at 38kHz).
 */
esp_err_t ir_send_nec(const ir_nec_frame_t *frame);

/**
 * Send raw pulse/space data (modulated at 38kHz carrier).
 * @param raw    Array of pulse/space durations in microseconds
 * @param count  Number of entries
 */
esp_err_t ir_send_raw(const uint16_t *raw, size_t count);

/**
 * Encode NEC address+command into raw symbol array.
 * @return number of symbols written, or 0 on error
 */
size_t ir_encode_nec(uint32_t address, uint32_t command,
                     uint16_t *out_raw, size_t out_max);
