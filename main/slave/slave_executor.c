/**
 * @file slave_executor.c
 * @brief Slave executor — receives ESP-NOW commands from master and
 *        executes hardware tools locally (WS2812, GPIO, IR).
 */

#include "slave/slave_executor.h"
#include "mimi_config.h"

#include "tools/tool_ws2812.h"
#include "tools/tool_gpio.h"
#include "tools/tool_ir.h"
#include "espnow/esp_now_device.h"
#include "espnow/esp_now_manager.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "slave";

/* ── Tool dispatch table entry ──────────────────────────────────── */
typedef struct {
    const char *name;
    esp_err_t (*execute)(const char *input_json, char *output, size_t output_size);
} slave_tool_t;

/* ── All hardware tools the slave can execute ────────────────────── */
static const slave_tool_t s_tools[] = {
    { "ws2812_set",    tool_ws2812_set_execute    },
    { "ws2812_off",    tool_ws2812_off_execute    },
    { "gpio_write",    tool_gpio_write_execute    },
    { "gpio_read",     tool_gpio_read_execute     },
    { "gpio_read_all", tool_gpio_read_all_execute },
    { "ir_send",       tool_ir_send_execute       },
    { "ir_list",       tool_ir_list_execute       },
};

#define TOOL_COUNT (sizeof(s_tools) / sizeof(s_tools[0]))

static bool s_inited = false;

/* ── Public API ─────────────────────────────────────────────────── */

esp_err_t slave_executor_init(void)
{
    if (s_inited) return ESP_OK;

    tool_ws2812_init();   /* RMT TX on GPIO48, self-initializing */
    tool_gpio_init();     /* GPIO policy setup */
    tool_ir_init();       /* RMT + SPIFFS IR code library */

    s_inited = true;
    ESP_LOGI(TAG, "Slave executor ready (%d tools)", (int)TOOL_COUNT);
    return ESP_OK;
}

esp_err_t slave_executor_handle(const uint8_t *src_mac,
                                 const char *json, int json_len)
{
    if (!src_mac || !json || json_len <= 0) {
        ESP_LOGE(TAG, "Invalid command");
        return ESP_ERR_INVALID_ARG;
    }

    /* Lazy init (safety) */
    if (!s_inited) {
        slave_executor_init();
    }

    /* Parse command JSON */
    char *json_null_term = strndup(json, json_len);
    if (!json_null_term) {
        ESP_LOGE(TAG, "OOM copying command");
        return ESP_ERR_NO_MEM;
    }

    cJSON *root = cJSON_Parse(json_null_term);
    free(json_null_term);

    if (!root) {
        ESP_LOGE(TAG, "Invalid JSON command");
        return ESP_ERR_INVALID_ARG;
    }

    /* Extract fields */
    cJSON *tool_item   = cJSON_GetObjectItem(root, "tool");
    cJSON *args_item   = cJSON_GetObjectItem(root, "args");
    cJSON *callid_item = cJSON_GetObjectItem(root, "call_id");

    const char *tool_name = cJSON_IsString(tool_item) ? tool_item->valuestring : NULL;
    const char *call_id   = cJSON_IsString(callid_item) ? callid_item->valuestring : "";

    if (!tool_name) {
        ESP_LOGE(TAG, "Command missing 'tool' field");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Executing tool '%s' (call_id=%s) from master "
             "%02X:%02X:%02X:%02X:%02X:%02X",
             tool_name, call_id,
             src_mac[0], src_mac[1], src_mac[2],
             src_mac[3], src_mac[4], src_mac[5]);

    /* Lock the slave executor to execute one tool at a time */
    static SemaphoreHandle_t s_mutex = NULL;
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
    }
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10000)) != pdTRUE) {
        ESP_LOGW(TAG, "Tool execution busy, dropping command");
        cJSON_Delete(root);
        return ESP_ERR_TIMEOUT;
    }

    /* Find and execute the tool */
    char output[768];
    esp_err_t exec_err = ESP_ERR_NOT_FOUND;

    for (size_t i = 0; i < TOOL_COUNT; i++) {
        if (strcmp(s_tools[i].name, tool_name) == 0) {
            /* Serialize args back to JSON string for the execute function */
            char *args_json = args_item ? cJSON_PrintUnformatted(args_item) : strdup("{}");
            if (args_json) {
                output[0] = '\0';
                exec_err = s_tools[i].execute(args_json, output, sizeof(output));
                free(args_json);
            } else {
                exec_err = ESP_ERR_NO_MEM;
            }
            break;
        }
    }

    if (exec_err == ESP_ERR_NOT_FOUND) {
        snprintf(output, sizeof(output), "Unknown tool: %s", tool_name);
        ESP_LOGW(TAG, "Unknown tool: %s", tool_name);
    } else {
        ESP_LOGI(TAG, "Tool '%s' result: %s (err=%s)",
                 tool_name, output, esp_err_to_name(exec_err));
    }

    xSemaphoreGive(s_mutex);
    cJSON_Delete(root);

    /* ── Build response and send back to master ─────────────────── */
    cJSON *rsp = cJSON_CreateObject();
    cJSON_AddStringToObject(rsp, "call_id", call_id);
    cJSON_AddNumberToObject(rsp, "error", exec_err);
    cJSON_AddStringToObject(rsp, "result", output);
    char *rsp_json = cJSON_PrintUnformatted(rsp);
    cJSON_Delete(rsp);

    if (!rsp_json) {
        return ESP_ERR_NO_MEM;
    }

    /* Build packet: [ESPNOW_PROTO_CMD_RSP][source_device_id(32)][rsp_json] */
    size_t rsp_len = strlen(rsp_json);
    size_t pkt_len = 1 + ESPNOW_DEVICE_ID_MAX_LEN + rsp_len;
    uint8_t *pkt = malloc(pkt_len);
    if (!pkt) {
        free(rsp_json);
        return ESP_ERR_NO_MEM;
    }

    pkt[0] = ESPNOW_PROTO_CMD_RSP;
    const char *my_id = esp_now_device_get_id();
    memset(pkt + 1, 0, ESPNOW_DEVICE_ID_MAX_LEN);
    strncpy((char *)(pkt + 1), my_id, ESPNOW_DEVICE_ID_MAX_LEN - 1);
    memcpy(pkt + 1 + ESPNOW_DEVICE_ID_MAX_LEN, rsp_json, rsp_len);

    /* Add master as peer if not already known */
    esp_now_manager_add_peer(src_mac);

    esp_err_t send_err = esp_now_manager_send(src_mac, pkt, pkt_len);
    if (send_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send response: %s", esp_err_to_name(send_err));
    } else {
        ESP_LOGI(TAG, "Response sent to master (%d bytes)", (int)pkt_len);
    }

    free(pkt);
    free(rsp_json);
    return exec_err;
}
