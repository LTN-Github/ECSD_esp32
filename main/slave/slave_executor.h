#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the slave executor.
 *
 * Registers all locally-available hardware tools (WS2812, GPIO, IR).
 * Must be called after SPIFFS mount (IR code library) and
 * esp_now_device_init().
 *
 * @return ESP_OK on success.
 */
esp_err_t slave_executor_init(void);

/**
 * @brief Handle an incoming ESP-NOW command from the master.
 *
 * Parses the JSON command, dispatches to the correct tool, executes it,
 * and sends the result back to the master via ESP-NOW.
 *
 * Command JSON format:
 *   { "tool": "gpio_write", "args": {...}, "call_id": "req_001" }
 *
 * Response (proto=0x05) sent back:
 *   { "call_id": "req_001", "error": 0, "result": "..." }
 *
 * @param src_mac   MAC address of the master (command sender)
 * @param json      JSON command string (may not be null-terminated)
 * @param json_len  Length of the JSON string
 * @return ESP_OK if handled and response sent, otherwise error.
 */
esp_err_t slave_executor_handle(const uint8_t *src_mac,
                                 const char *json, int json_len);

#ifdef __cplusplus
}
#endif
