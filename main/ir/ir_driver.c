#include "ir/ir_driver.h"
#include "mimi_config.h"

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_encoder.h"

static const char *TAG = "ir_drv";

/* ================================================================
 *  NEC Protocol Timing (microseconds)
 * ================================================================
 *  Leader:  9000us pulse + 4500us space
 *  Bit 0:   562us pulse  + 562us space
 *  Bit 1:   562us pulse  + 1687us space
 *  Repeat:  9000us pulse + 2250us space + 562us pulse
 *  End:     562us pulse
 * ================================================================ */
#define NEC_LEADER_PULSE_US     9000
#define NEC_LEADER_SPACE_US     4500
#define NEC_REPEAT_SPACE_US     2250
#define NEC_BIT_PULSE_US        562
#define NEC_BIT0_SPACE_US       562
#define NEC_BIT1_SPACE_US       1687
#define NEC_END_PULSE_US        562

/* Timing tolerance: ±20% */
#define TOL(val, expected)  ((val) > (expected) * 8 / 10 && (val) < (expected) * 12 / 10)

/* Max raw symbols we can capture */
#define RAW_BUF_SIZE    (MIMI_IR_RX_BUF_SYMBOLS)

/* ================================================================
 *  Static State
 * ================================================================ */
static bool                 s_init_ok    = false;
static rmt_channel_handle_t s_tx_channel = NULL;
static rmt_channel_handle_t s_rx_channel = NULL;
static SemaphoreHandle_t    s_rx_sem     = NULL;
static rmt_symbol_word_t    s_rx_buf[RAW_BUF_SIZE];
static size_t               s_rx_count   = 0;
static bool                 s_shared_pin = false; /* RX and TX share same GPIO */
static SemaphoreHandle_t    s_ir_mutex   = NULL;  /* serialize RX/TX access */

/* ================================================================
 *  RX Done Callback
 * ================================================================ */
static bool IRAM_ATTR rx_done_cb(rmt_channel_handle_t channel,
                                  const rmt_rx_done_event_data_t *edata,
                                  void *user_data)
{
    BaseType_t wake = pdFALSE;
    s_rx_count = edata->num_symbols;
    if (s_rx_count > RAW_BUF_SIZE) {
        s_rx_count = RAW_BUF_SIZE;
    }
    /* s_rx_buf is the same buffer passed to rmt_receive — no copy needed */
    xSemaphoreGiveFromISR(s_rx_sem, &wake);
    return wake == pdTRUE;
}

/* ================================================================
 *  Initialization
 * ================================================================ */
esp_err_t ir_driver_init(void)
{
    if (s_init_ok) return ESP_OK;

    s_shared_pin = (MIMI_IR_RX_GPIO == MIMI_IR_TX_GPIO);

    /* ── IR mutex (serialises RX/TX on half-duplex pin) ───────── */
    s_ir_mutex = xSemaphoreCreateMutex();
    if (!s_ir_mutex) {
        ESP_LOGE(TAG, "mutex create failed");
        return ESP_ERR_NO_MEM;
    }

    /* ── RX channel ─────────────────────────────────────────── */
    rmt_rx_channel_config_t rx_cfg = {
        .gpio_num            = MIMI_IR_RX_GPIO,
        .clk_src             = RMT_CLK_SRC_DEFAULT,
        .resolution_hz       = MIMI_IR_RX_RESOLUTION_HZ,
        .mem_block_symbols   = MIMI_IR_RX_MEM_BLOCKS,
    };
    esp_err_t err = rmt_new_rx_channel(&rx_cfg, &s_rx_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_rx_channel: %s", esp_err_to_name(err));
        goto fail_mutex;
    }

    s_rx_sem = xSemaphoreCreateBinary();
    if (!s_rx_sem) {
        ESP_LOGE(TAG, "semaphore create failed");
        rmt_del_channel(s_rx_channel);
        s_rx_channel = NULL;
        err = ESP_ERR_NO_MEM;
        goto fail_mutex;
    }

    rmt_rx_event_callbacks_t rx_cbs = {
        .on_recv_done = rx_done_cb,
    };
    err = rmt_rx_register_event_callbacks(s_rx_channel, &rx_cbs, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rx callback reg: %s", esp_err_to_name(err));
        goto fail_rx;
    }

    err = rmt_enable(s_rx_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_enable rx: %s", esp_err_to_name(err));
        goto fail_rx;
    }

    /* TX channel: only created on demand for half-duplex mode.
     * Separate-pin mode creates TX now (backward compatible). */
    if (!s_shared_pin) {
        rmt_tx_channel_config_t tx_cfg = {
            .gpio_num            = MIMI_IR_TX_GPIO,
            .clk_src             = RMT_CLK_SRC_DEFAULT,
            .resolution_hz       = MIMI_IR_TX_RESOLUTION_HZ,
            .mem_block_symbols   = MIMI_IR_TX_MEM_BLOCKS,
            .trans_queue_depth   = 4,
        };
        err = rmt_new_tx_channel(&tx_cfg, &s_tx_channel);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "rmt_new_tx_channel: %s", esp_err_to_name(err));
            goto fail_tx;
        }
        rmt_carrier_config_t carrier_cfg = {
            .frequency_hz = MIMI_IR_CARRIER_HZ,
            .duty_cycle   = 0.33f,
        };
        err = rmt_apply_carrier(s_tx_channel, &carrier_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "rmt_apply_carrier: %s", esp_err_to_name(err));
            goto fail_tx_enable;
        }
        err = rmt_enable(s_tx_channel);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "rmt_enable tx: %s", esp_err_to_name(err));
            goto fail_tx_enable;
        }
        ESP_LOGI(TAG, "init ok (RX=GPIO%d, TX=GPIO%d, carrier=%dHz)",
                 MIMI_IR_RX_GPIO, MIMI_IR_TX_GPIO, MIMI_IR_CARRIER_HZ);
    } else {
        ESP_LOGI(TAG, "init ok (half-duplex on GPIO%d, carrier=%dHz)",
                 MIMI_IR_RX_GPIO, MIMI_IR_CARRIER_HZ);
    }

    s_init_ok = true;
    return ESP_OK;

fail_tx_enable:
    rmt_del_channel(s_tx_channel);
    s_tx_channel = NULL;
fail_tx:
    rmt_disable(s_rx_channel);
fail_rx:
    rmt_del_channel(s_rx_channel);
    s_rx_channel = NULL;
    if (s_rx_sem) { vSemaphoreDelete(s_rx_sem); s_rx_sem = NULL; }
fail_mutex:
    if (s_ir_mutex) { vSemaphoreDelete(s_ir_mutex); s_ir_mutex = NULL; }
    return err;
}

/* ================================================================
 *  Half-Duplex Helpers (shared RX/TX pin — dynamic create/destroy)
 * ================================================================ */

/* Destroy RX, create TX on the shared GPIO. Caller holds s_ir_mutex. */
static esp_err_t ir_switch_to_tx(void)
{
    if (!s_shared_pin) return ESP_OK;

    /* Tear down RX */
    if (s_rx_channel) {
        rmt_disable(s_rx_channel);
        rmt_del_channel(s_rx_channel);
        s_rx_channel = NULL;
    }

    /* Create TX on the shared GPIO */
    rmt_tx_channel_config_t tx_cfg = {
        .gpio_num            = MIMI_IR_TX_GPIO,
        .clk_src             = RMT_CLK_SRC_DEFAULT,
        .resolution_hz       = MIMI_IR_TX_RESOLUTION_HZ,
        .mem_block_symbols   = MIMI_IR_TX_MEM_BLOCKS,
        .trans_queue_depth   = 4,
    };
    esp_err_t err = rmt_new_tx_channel(&tx_cfg, &s_tx_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "switch_to_tx new_tx: %s", esp_err_to_name(err));
        return err;
    }

    rmt_carrier_config_t carrier_cfg = {
        .frequency_hz = MIMI_IR_CARRIER_HZ,
        .duty_cycle   = 0.33f,
    };
    err = rmt_apply_carrier(s_tx_channel, &carrier_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "switch_to_tx carrier: %s", esp_err_to_name(err));
        rmt_del_channel(s_tx_channel);
        s_tx_channel = NULL;
        return err;
    }

    err = rmt_enable(s_tx_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "switch_to_tx enable: %s", esp_err_to_name(err));
        rmt_del_channel(s_tx_channel);
        s_tx_channel = NULL;
    }
    return err;
}

/* Destroy TX, recreate RX on the shared GPIO. Caller holds s_ir_mutex. */
static esp_err_t ir_switch_to_rx(void)
{
    if (!s_shared_pin) return ESP_OK;
    if (s_rx_channel != NULL) return ESP_OK;  /* already in RX mode */

    /* Tear down TX */
    if (s_tx_channel) {
        rmt_disable(s_tx_channel);
        rmt_del_channel(s_tx_channel);
        s_tx_channel = NULL;
    }

    /* Recreate RX */
    rmt_rx_channel_config_t rx_cfg = {
        .gpio_num            = MIMI_IR_RX_GPIO,
        .clk_src             = RMT_CLK_SRC_DEFAULT,
        .resolution_hz       = MIMI_IR_RX_RESOLUTION_HZ,
        .mem_block_symbols   = MIMI_IR_RX_MEM_BLOCKS,
    };
    esp_err_t err = rmt_new_rx_channel(&rx_cfg, &s_rx_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "switch_to_rx new_rx: %s", esp_err_to_name(err));
        return err;
    }

    rmt_rx_event_callbacks_t rx_cbs = {
        .on_recv_done = rx_done_cb,
    };
    err = rmt_rx_register_event_callbacks(s_rx_channel, &rx_cbs, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "switch_to_rx callback: %s", esp_err_to_name(err));
        rmt_del_channel(s_rx_channel);
        s_rx_channel = NULL;
        return err;
    }

    err = rmt_enable(s_rx_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "switch_to_rx enable: %s", esp_err_to_name(err));
        rmt_del_channel(s_rx_channel);
        s_rx_channel = NULL;
    }
    return err;
}

/* ================================================================
 *  Receive
 * ================================================================ */
esp_err_t ir_receive_raw(ir_raw_capture_t *capture, uint32_t timeout_ms)
{
    if (!s_init_ok) return ESP_ERR_INVALID_STATE;
    if (!capture)   return ESP_ERR_INVALID_ARG;

    /* Serialise with TX */
    if (xSemaphoreTake(s_ir_mutex, pdMS_TO_TICKS(timeout_ms + 100)) != pdTRUE) {
        ESP_LOGW(TAG, "receive: IR busy (tx in progress)");
        return ESP_ERR_TIMEOUT;
    }

    /* Ensure RX channel is active (half-duplex recovery) */
    if (s_shared_pin) ir_switch_to_rx();

    /* Arm the RX channel */
    rmt_receive_config_t rx_receive_cfg = {
        .signal_range_min_ns = 5000,      /* 5us minimum pulse */
        .signal_range_max_ns = 50000000,  /* 50ms maximum (covers NEC leader) */
    };
    esp_err_t err = rmt_receive(s_rx_channel, s_rx_buf,
                                 RAW_BUF_SIZE, &rx_receive_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_receive: %s", esp_err_to_name(err));
        xSemaphoreGive(s_ir_mutex);
        return err;
    }

    /* Wait for data */
    TickType_t ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if (xSemaphoreTake(s_rx_sem, ticks) != pdTRUE) {
        ESP_LOGW(TAG, "receive timeout (%lums)", (unsigned long)timeout_ms);
        xSemaphoreGive(s_ir_mutex);
        return ESP_ERR_TIMEOUT;
    }

    if (s_rx_count == 0) {
        ESP_LOGW(TAG, "receive: 0 symbols");
        xSemaphoreGive(s_ir_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    /* Convert RMT symbols to us durations (pulse+space pairs) */
    size_t pairs = s_rx_count;
    uint16_t *raw = malloc(pairs * 2 * sizeof(uint16_t));
    if (!raw) {
        ESP_LOGE(TAG, "malloc failed (%d pairs)", (int)pairs);
        xSemaphoreGive(s_ir_mutex);
        return ESP_ERR_NO_MEM;
    }

    for (size_t i = 0; i < pairs; i++) {
        /* duration0 = pulse (level=1), duration1 = space (level=0) */
        /* RMT resolution is 1MHz → 1 tick = 1us */
        raw[i * 2 + 0] = (uint16_t)s_rx_buf[i].duration0;
        raw[i * 2 + 1] = (uint16_t)s_rx_buf[i].duration1;
    }

    capture->raw   = raw;
    capture->count = pairs * 2;

    xSemaphoreGive(s_ir_mutex);

    ESP_LOGI(TAG, "captured %d symbols (%d us entries)", (int)pairs, (int)(pairs * 2));
    return ESP_OK;
}

esp_err_t ir_decode_nec(const ir_raw_capture_t *capture, ir_nec_frame_t *frame)
{
    if (!capture || !frame || !capture->raw || capture->count < 4) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint16_t *d = capture->raw;
    size_t cnt = capture->count;

    /* Expect: pulse, space, pulse, space, ... */
    /* d[0]=leader pulse, d[1]=leader space, then pairs of bit pulse+space */

    /* ── Check leader ───────────────────────────────────────── */
    if (!TOL(d[0], NEC_LEADER_PULSE_US)) {
        return ESP_ERR_NOT_FOUND;
    }

    /* ── Repeat frame: leader pulse + 2250us space + end pulse */
    if (TOL(d[1], NEC_REPEAT_SPACE_US)) {
        frame->address   = 0;
        frame->command   = 0;
        frame->is_repeat = true;
        frame->extended  = false;
        ESP_LOGI(TAG, "NEC repeat frame");
        return ESP_OK;
    }

    /* ── Normal frame: leader pulse + 4500us space ──────────── */
    if (!TOL(d[1], NEC_LEADER_SPACE_US)) {
        return ESP_ERR_NOT_FOUND;
    }

    /* Need at least 32 data bits = 64 entries after leader (2 entries per bit) */
    /* Plus leader (2 entries) = 66 minimum, with end pulse = 67 */
    if (cnt < 66) {
        ESP_LOGW(TAG, "NEC: too few entries (%d)", (int)cnt);
        return ESP_ERR_NOT_FOUND;
    }

    /* ── Decode 32 data bits ────────────────────────────────── */
    uint32_t data = 0;
    for (int bit = 0; bit < 32; bit++) {
        size_t idx = 2 + bit * 2;     /* skip leader (2 entries) */
        uint16_t pulse = d[idx];
        uint16_t space = d[idx + 1];

        if (!TOL(pulse, NEC_BIT_PULSE_US)) {
            ESP_LOGD(TAG, "NEC: bad bit %d pulse=%d", bit, pulse);
            return ESP_ERR_NOT_FOUND;
        }

        if (TOL(space, NEC_BIT1_SPACE_US)) {
            data |= (1u << bit);      /* bit 1 */
        } else if (!TOL(space, NEC_BIT0_SPACE_US)) {
            ESP_LOGD(TAG, "NEC: bad bit %d space=%d", bit, space);
            return ESP_ERR_NOT_FOUND;
        }
        /* bit 0: space already 0 */
    }

    /* ── Extract fields (NEC: addr_low, addr_high, cmd, ~cmd) ─ */
    uint8_t addr_lo = (data >>  0) & 0xFF;
    uint8_t addr_hi = (data >>  8) & 0xFF;
    uint8_t cmd     = (data >> 16) & 0xFF;
    uint8_t cmd_inv = (data >> 24) & 0xFF;

    /* Validate command complement */
    if ((cmd ^ cmd_inv) != 0xFF) {
        ESP_LOGW(TAG, "NEC: cmd complement mismatch (0x%02X vs 0x%02X)", cmd, cmd_inv);
        /* Still accept — some remotes don't use complement */
    }

    if (addr_lo == addr_hi) {
        /* Standard 8-bit address */
        frame->address   = addr_lo;
        frame->extended  = false;
    } else {
        /* Extended 16-bit address */
        frame->address   = (addr_hi << 8) | addr_lo;
        frame->extended  = true;
    }
    frame->command   = cmd;
    frame->is_repeat = false;

    ESP_LOGI(TAG, "NEC decoded: addr=0x%04X cmd=0x%02X %s",
             (unsigned)frame->address, (unsigned)frame->command,
             frame->extended ? "(extended)" : "(standard)");
    return ESP_OK;
}

/* ================================================================
 *  Transmit
 * ================================================================ */

/**
 * Convert us durations to RMT symbols and transmit.
 * TX resolution is 10MHz → 1 tick = 100ns, so us * 10 = ticks.
 *
 * raw[] format: even indices = marks (carrier ON), odd indices = spaces (carrier OFF).
 * When count is odd, the last entry is a standalone final mark (e.g. NEC repeat end pulse).
 */
static esp_err_t transmit_raw(const uint16_t *raw, size_t count)
{
    if (!s_init_ok) return ESP_ERR_INVALID_STATE;
    if (!raw || count < 2) return ESP_ERR_INVALID_ARG;

    /* Build RMT symbols.  Each symbol covers one mark+space pair.
     * For odd count the last mark has no following space. */
    size_t symbol_count = (count + 1) / 2;  /* round up for odd count */
    rmt_symbol_word_t *syms = malloc(symbol_count * sizeof(rmt_symbol_word_t));
    if (!syms) return ESP_ERR_NO_MEM;

    /* Pre-calculate total frame duration (us) for dynamic timeout */
    uint32_t total_us = 0;

    for (size_t i = 0; i < count; i += 2) {
        size_t sym_idx = i / 2;
        uint16_t pulse_us = raw[i];
        total_us += pulse_us;
        syms[sym_idx].duration0 = pulse_us * 10;  /* us → 100ns ticks */
        syms[sym_idx].level0    = 1;

        if (i + 1 < count) {
            uint16_t space_us = raw[i + 1];
            total_us += space_us;
            syms[sym_idx].duration1 = space_us * 10;
            syms[sym_idx].level1    = 0;
        } else {
            /* Standalone final mark (e.g. NEC repeat end pulse) */
            syms[sym_idx].duration1 = 0;
            syms[sym_idx].level1    = 0;
        }
    }

    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,
        .flags      = { .eot_level = 0 },
    };

    /* Half-duplex: switch pin from RX to TX mode */
    xSemaphoreTake(s_ir_mutex, portMAX_DELAY);
    esp_err_t err = ir_switch_to_tx();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "switch to tx: %s", esp_err_to_name(err));
        free(syms);
        xSemaphoreGive(s_ir_mutex);
        return err;
    }

    err = rmt_transmit(s_tx_channel, NULL,
                        syms, symbol_count * sizeof(rmt_symbol_word_t),
                        &tx_cfg);
    free(syms);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_transmit: %s", esp_err_to_name(err));
        ir_switch_to_rx();
        xSemaphoreGive(s_ir_mutex);
        return err;
    }

    /* Wait for transmission to complete.
     * Timeout = frame duration + 100ms margin, minimum 500ms.
     * NEC frames: ~67ms, Midea AC: ~124ms, Gree AC: ~139ms. */
    uint32_t timeout_ms = (total_us / 1000) + 100;
    if (timeout_ms < 500) timeout_ms = 500;
    err = rmt_tx_wait_all_done(s_tx_channel, pdMS_TO_TICKS(timeout_ms));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tx wait: %s (timeout=%lums)", esp_err_to_name(err),
                 (unsigned long)timeout_ms);
    }

    /* Half-duplex: restore RX mode */
    ir_switch_to_rx();
    xSemaphoreGive(s_ir_mutex);
    return err;
}

size_t ir_encode_nec(uint32_t address, uint32_t command,
                     uint16_t *out_raw, size_t out_max)
{
    /* Need: leader (2) + 32 bits (64) + end (1) = 67 entries */
    if (!out_raw || out_max < 67) return 0;

    size_t idx = 0;

    /* Leader: 9000us pulse + 4500us space */
    out_raw[idx++] = NEC_LEADER_PULSE_US;
    out_raw[idx++] = NEC_LEADER_SPACE_US;

    /* 32 data bits, LSB first: addr_lo, addr_hi, cmd, ~cmd */
    uint8_t addr_lo = address & 0xFF;
    uint8_t addr_hi = (address >> 8) & 0xFF;
    uint8_t cmd     = command & 0xFF;
    uint8_t cmd_inv = ~cmd;

    uint32_t data = ((uint32_t)cmd_inv << 24) |
                    ((uint32_t)cmd     << 16) |
                    ((uint32_t)addr_hi <<  8) |
                    ((uint32_t)addr_lo <<  0);

    for (int bit = 0; bit < 32; bit++) {
        out_raw[idx++] = NEC_BIT_PULSE_US;
        if (data & (1u << bit)) {
            out_raw[idx++] = NEC_BIT1_SPACE_US;
        } else {
            out_raw[idx++] = NEC_BIT0_SPACE_US;
        }
    }

    /* End burst */
    out_raw[idx++] = NEC_END_PULSE_US;

    return idx;
}

esp_err_t ir_send_nec(const ir_nec_frame_t *frame)
{
    if (!frame) return ESP_ERR_INVALID_ARG;

    uint16_t raw[67];
    size_t count;

    if (frame->is_repeat) {
        /* Repeat: leader pulse + 2250us space + end pulse */
        raw[0] = NEC_LEADER_PULSE_US;
        raw[1] = NEC_REPEAT_SPACE_US;
        raw[2] = NEC_END_PULSE_US;
        count = 3;
    } else {
        count = ir_encode_nec(frame->address, frame->command, raw, 67);
        if (count == 0) return ESP_FAIL;
    }

    ESP_LOGI(TAG, "send NEC: addr=0x%04X cmd=0x%02X %s",
             (unsigned)frame->address, (unsigned)frame->command,
             frame->is_repeat ? "(repeat)" : "");

    return transmit_raw(raw, count);
}

esp_err_t ir_send_raw(const uint16_t *raw, size_t count)
{
    if (!raw || count < 2) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "send raw: %d entries", (int)count);
    return transmit_raw(raw, count);
}
