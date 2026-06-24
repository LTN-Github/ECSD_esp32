#include "tools/tool_ir.h"
#include "tools/tool_ir_ac.h"
#include "ir/ir_driver.h"
#include "mimi_config.h"

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "tool_ir";

/* ================================================================
 *  IR Code Entry
 * ================================================================ */
typedef struct {
    char          name[32];      /* IR代码名称 */
    ir_protocol_t protocol;      /* IR协议类型 */
    uint32_t      address;       /* 设备地址 */
    uint32_t      command;       /* 命令码 */
    uint16_t      raw[MIMI_IR_MAX_RAW_SYMBOLS];  /* 原始IR信号数据 */
    size_t        raw_count;     /* 原始数据数量 */
} ir_code_entry_t;

static ir_code_entry_t s_codes[MIMI_IR_MAX_CODES];  /* IR代码库 */
static int             s_code_count = 0;             /* 当前代码数量 */
static bool            s_lib_loaded = false;         /* 库是否已加载 */

/* ================================================================
 *  SPIFFS JSON Persistence (follows cron_service.c pattern)
 * ================================================================ */

/**
 * 函数名: ir_lib_save
 * 功能: 将IR代码库保存到SPIFFS文件系统
 * 参数: 无
 * 返回值: ESP_OK成功，ESP_FAIL写入失败，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
static esp_err_t ir_lib_save(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "preset_version", IR_AC_PRESET_VERSION);
    cJSON *arr  = cJSON_CreateArray();

    for (int i = 0; i < s_code_count; i++) {
        ir_code_entry_t *c = &s_codes[i];
        cJSON *obj = cJSON_CreateObject();

        cJSON_AddStringToObject(obj, "name", c->name);
        cJSON_AddNumberToObject(obj, "protocol", c->protocol);
        cJSON_AddNumberToObject(obj, "address", c->address);
        cJSON_AddNumberToObject(obj, "command", c->command);
        cJSON_AddNumberToObject(obj, "raw_count", c->raw_count);

        cJSON *raw_arr = cJSON_CreateArray();
        for (size_t j = 0; j < c->raw_count; j++) {
            cJSON_AddItemToArray(raw_arr, cJSON_CreateNumber(c->raw[j]));
        }
        cJSON_AddItemToObject(obj, "raw", raw_arr);

        cJSON_AddItemToArray(arr, obj);
    }

    cJSON_AddItemToObject(root, "codes", arr);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) {
        ESP_LOGE(TAG, "cJSON_Print failed");
        return ESP_FAIL;
    }

    FILE *f = fopen(MIMI_IR_CODE_FILE, "w");
    if (!f) {
        ESP_LOGE(TAG, "open %s for write failed", MIMI_IR_CODE_FILE);
        free(json_str);
        return ESP_FAIL;
    }

    fwrite(json_str, 1, strlen(json_str), f);
    fclose(f);
    free(json_str);

    ESP_LOGI(TAG, "saved %d codes to %s", s_code_count, MIMI_IR_CODE_FILE);
    return ESP_OK;
}

/**
 * 函数名: ir_lib_load
 * 功能: 从SPIFFS文件系统加载IR代码库
 * 参数: 无
 * 返回值: ESP_OK成功，ESP_ERR_NO_MEM内存不足，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
static esp_err_t ir_lib_load(void)
{
    FILE *f = fopen(MIMI_IR_CODE_FILE, "r");
    if (!f) {
        ESP_LOGI(TAG, "no IR code file found, starting fresh");
        s_code_count = 0;
        s_lib_loaded = true;
        return ESP_OK;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 32768) {
        ESP_LOGW(TAG, "IR code file invalid size: %ld", fsize);
        fclose(f);
        s_code_count = 0;
        s_lib_loaded = true;
        return ESP_OK;
    }

    char *buf = malloc(fsize + 1);
    if (!buf) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    fread(buf, 1, fsize, f);
    buf[fsize] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        ESP_LOGW(TAG, "IR code JSON parse failed");
        s_code_count = 0;
        s_lib_loaded = true;
        return ESP_OK;
    }

    cJSON *arr = cJSON_GetObjectItem(root, "codes");
    if (!cJSON_IsArray(arr)) {
        ESP_LOGW(TAG, "IR codes: missing 'codes' array");
        cJSON_Delete(root);
        s_code_count = 0;
        s_lib_loaded = true;
        return ESP_OK;
    }

    int cnt = cJSON_GetArraySize(arr);
    if (cnt > MIMI_IR_MAX_CODES) cnt = MIMI_IR_MAX_CODES;

    s_code_count = 0;
    for (int i = 0; i < cnt; i++) {
        cJSON *obj = cJSON_GetArrayItem(arr, i);
        ir_code_entry_t *c = &s_codes[s_code_count];

        cJSON *j_name     = cJSON_GetObjectItem(obj, "name");
        cJSON *j_proto    = cJSON_GetObjectItem(obj, "protocol");
        cJSON *j_addr     = cJSON_GetObjectItem(obj, "address");
        cJSON *j_cmd      = cJSON_GetObjectItem(obj, "command");
        cJSON *j_raw_cnt  = cJSON_GetObjectItem(obj, "raw_count");
        cJSON *j_raw      = cJSON_GetObjectItem(obj, "raw");

        if (!cJSON_IsString(j_name)) continue;

        memset(c, 0, sizeof(*c));
        strncpy(c->name, j_name->valuestring, sizeof(c->name) - 1);
        c->protocol = cJSON_IsNumber(j_proto) ? (ir_protocol_t)j_proto->valueint : IR_PROTO_RAW;
        c->address  = cJSON_IsNumber(j_addr)  ? (uint32_t)j_addr->valuedouble  : 0;
        c->command  = cJSON_IsNumber(j_cmd)   ? (uint32_t)j_cmd->valuedouble   : 0;

        if (cJSON_IsArray(j_raw)) {
            size_t rcnt = cJSON_GetArraySize(j_raw);
            if (rcnt > MIMI_IR_MAX_RAW_SYMBOLS) rcnt = MIMI_IR_MAX_RAW_SYMBOLS;
            c->raw_count = rcnt;
            for (size_t j = 0; j < rcnt; j++) {
                cJSON *v = cJSON_GetArrayItem(j_raw, j);
                c->raw[j] = cJSON_IsNumber(v) ? (uint16_t)v->valuedouble : 0;
            }
        } else if (cJSON_IsNumber(j_raw_cnt)) {
            c->raw_count = 0; /* raw array missing, reset */
        }

        s_code_count++;
    }

    cJSON_Delete(root);
    s_lib_loaded = true;
    ESP_LOGI(TAG, "loaded %d IR codes", s_code_count);
    return ESP_OK;
}

/**
 * 函数名: ir_lib_find
 * 功能: 在IR代码库中查找指定名称的代码
 * 参数:
 *      name - 要查找的IR代码名称
 * 返回值: 找到的IR代码条目指针，未找到返回NULL
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
static ir_code_entry_t *ir_lib_find(const char *name)
{
    for (int i = 0; i < s_code_count; i++) {
        if (strcasecmp(s_codes[i].name, name) == 0) {
            return &s_codes[i];
        }
    }
    return NULL;
}

/**
 * 函数名: ir_lib_add
 * 功能: 添加新的IR代码到代码库
 * 参数:
 *      entry - 要添加的IR代码条目
 * 返回值: ESP_OK成功，ESP_ERR_NO_MEM库已满，ESP_ERR_INVALID_STATE代码已存在
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
static esp_err_t ir_lib_add(const ir_code_entry_t *entry)
{
    if (s_code_count >= MIMI_IR_MAX_CODES) {
        ESP_LOGW(TAG, "IR code library full (%d)", MIMI_IR_MAX_CODES);
        return ESP_ERR_NO_MEM;
    }
    if (ir_lib_find(entry->name)) {
        ESP_LOGW(TAG, "IR code '%s' already exists", entry->name);
        return ESP_ERR_INVALID_STATE;
    }

    s_codes[s_code_count] = *entry;
    s_code_count++;
    return ir_lib_save();
}

/* ================================================================
 *  Init
 * ================================================================ */

/**
 * 函数名: tool_ir_init
 * 功能: 初始化IR工具，初始化IR驱动并加载代码库
 * 参数: 无
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_ir_init(void)
{
    esp_err_t err = ir_driver_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ir_driver_init failed: %s", esp_err_to_name(err));
        return err;
    }

    if (!s_lib_loaded) {
        ir_lib_load();
    }

    /* 如果库为空，尝试加载预置的 AC 码 */
    if (s_code_count == 0) {
        tool_ir_ac_load_presets();
        ir_lib_load();  /* 重新加载刚写入的预置码 */
    }

    ESP_LOGI(TAG, "IR tool init ok (%d codes loaded)", s_code_count);
    return ESP_OK;
}

/* ================================================================
 *  tool_ir_receive_execute
 * ================================================================ */

/**
 * 函数名: tool_ir_receive_execute
 * 功能: 执行IR接收操作，学习并存储新的IR遥控代码
 * 参数:
 *      input_json - JSON格式输入，包含name参数
 *      output - 结果输出缓冲区
 *      output_size - 输出缓冲区大小
 * 返回值: ESP_OK成功，ESP_ERR_TIMEOUT超时，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_ir_receive_execute(const char *input_json, char *output, size_t output_size)
{
    /* Parse name */
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *j_name = cJSON_GetObjectItem(root, "name");
    if (!cJSON_IsString(j_name) || j_name->valuestring[0] == '\0') {
        snprintf(output, output_size, "Error: 'name' is required (string)");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    const char *name = j_name->valuestring;
    if (strlen(name) >= sizeof(((ir_code_entry_t *)0)->name)) {
        snprintf(output, output_size, "Error: name too long (max %d chars)",
                 (int)(sizeof(((ir_code_entry_t *)0)->name) - 1));
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    if (ir_lib_find(name)) {
        snprintf(output, output_size, "Error: IR code '%s' already exists", name);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_STATE;
    }

    cJSON_Delete(root);

    /* Receive IR signal */
    ESP_LOGI(TAG, "waiting for IR signal (name='%s', timeout=%dms)",
             name, MIMI_IR_CAPTURE_TIMEOUT_MS);

    ir_raw_capture_t capture = {0};
    esp_err_t err = ir_receive_raw(&capture, MIMI_IR_CAPTURE_TIMEOUT_MS);
    if (err == ESP_ERR_TIMEOUT) {
        snprintf(output, output_size,
                 "Error: No IR signal detected within %d seconds. "
                 "Point the remote at the IR receiver and try again.",
                 MIMI_IR_CAPTURE_TIMEOUT_MS / 1000);
        return ESP_ERR_TIMEOUT;
    }
    if (err != ESP_OK) {
        snprintf(output, output_size, "Error: IR receive failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    /* Try NEC decode */
    ir_nec_frame_t nec_frame = {0};
    bool nec_ok = (ir_decode_nec(&capture, &nec_frame) == ESP_OK);

    /* Build code entry */
    ir_code_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    strncpy(entry.name, name, sizeof(entry.name) - 1);

    if (nec_ok && !nec_frame.is_repeat) {
        entry.protocol = IR_PROTO_NEC;
        entry.address  = nec_frame.address;
        entry.command  = nec_frame.command;
    } else {
        entry.protocol = IR_PROTO_RAW;
        entry.address  = 0;
        entry.command  = 0;
    }

    /* Store raw data (always, for AC replay) */
    size_t store_count = capture.count;
    if (store_count > MIMI_IR_MAX_RAW_SYMBOLS) {
        store_count = MIMI_IR_MAX_RAW_SYMBOLS;
    }
    memcpy(entry.raw, capture.raw, store_count * sizeof(uint16_t));
    entry.raw_count = store_count;

    free(capture.raw);

    /* Add to library */
    err = ir_lib_add(&entry);
    if (err != ESP_OK) {
        snprintf(output, output_size, "Error: failed to store IR code: %s",
                 esp_err_to_name(err));
        return err;
    }

    /* Format output */
    if (nec_ok && !nec_frame.is_repeat) {
        snprintf(output, output_size,
                 "OK: Learned '%s' -- NEC addr=0x%04X cmd=0x%02X%s, %d raw symbols stored",
                 name,
                 (unsigned)nec_frame.address,
                 (unsigned)nec_frame.command,
                 nec_frame.extended ? " (extended)" : "",
                 (int)store_count);
    } else if (nec_ok && nec_frame.is_repeat) {
        snprintf(output, output_size,
                 "OK: Learned '%s' -- repeat signal detected, %d raw symbols stored",
                 name, (int)store_count);
    } else {
        /* Calculate approximate duration */
        uint32_t total_us = 0;
        for (size_t i = 0; i < store_count; i++) {
            total_us += entry.raw[i];
        }
        snprintf(output, output_size,
                 "OK: Learned '%s' -- raw capture (%d symbols, %lums), stored for replay",
                 name, (int)store_count, (unsigned long)(total_us / 1000));
    }

    ESP_LOGI(TAG, "learned '%s' (%d symbols)", name, (int)store_count);
    return ESP_OK;
}

/* ================================================================
 *  tool_ir_send_execute
 * ================================================================ */

/**
 * 函数名: tool_ir_send_execute
 * 功能: 执行IR发送操作，发送已存储的IR遥控代码
 * 参数:
 *      input_json - JSON格式输入，包含name和可选repeat参数
 *      output - 结果输出缓冲区
 *      output_size - 输出缓冲区大小
 * 返回值: ESP_OK成功，ESP_ERR_NOT_FOUND代码不存在，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_ir_send_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *j_name   = cJSON_GetObjectItem(root, "name");
    cJSON *j_repeat = cJSON_GetObjectItem(root, "repeat");

    if (!cJSON_IsString(j_name) || j_name->valuestring[0] == '\0') {
        snprintf(output, output_size, "Error: 'name' is required (string)");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    int repeat = 1;
    if (cJSON_IsNumber(j_repeat) && j_repeat->valueint > 0) {
        repeat = j_repeat->valueint;
        if (repeat > 10) repeat = 10;  /* safety cap */
    }

    const char *name = j_name->valuestring;
    ir_code_entry_t *entry = ir_lib_find(name);
    if (!entry) {
        snprintf(output, output_size,
                 "Error: IR code '%s' not found. Use ir_list to see available codes.",
                 name);
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    cJSON_Delete(root);

    /* Send the code */
    esp_err_t last_err = ESP_OK;
    for (int i = 0; i < repeat; i++) {
        if (i > 0) {
            vTaskDelay(pdMS_TO_TICKS(110));  /* NEC repeat gap */
        }

        if (entry->protocol == IR_PROTO_NEC && entry->raw_count == 0) {
            /* Pure NEC code (no raw stored) */
            ir_nec_frame_t frame = {
                .address   = entry->address,
                .command   = entry->command,
                .is_repeat = false,
                .extended  = (entry->address > 0xFF),
            };
            last_err = ir_send_nec(&frame);
        } else {
            /* Raw replay (works for all protocols including AC) */
            last_err = ir_send_raw(entry->raw, entry->raw_count);
        }

        if (last_err != ESP_OK) {
            snprintf(output, output_size, "Error: IR send failed on attempt %d: %s",
                     i + 1, esp_err_to_name(last_err));
            return last_err;
        }
    }

    snprintf(output, output_size, "OK: Sent '%s' (%s, %d time%s)",
             name,
             entry->protocol == IR_PROTO_NEC ? "NEC" : "raw",
             repeat, repeat > 1 ? "s" : "");

    ESP_LOGI(TAG, "sent '%s' x%d", name, repeat);
    return ESP_OK;
}

/* ================================================================
 *  tool_ir_list_execute
 * ================================================================ */

/**
 * 函数名: tool_ir_list_execute
 * 功能: 列出所有已存储的IR代码
 * 参数:
 *      input_json - JSON格式输入（此函数未使用）
 *      output - 结果输出缓冲区
 *      output_size - 输出缓冲区大小
 * 返回值: ESP_OK成功，ESP_ERR_NO_MEM缓冲区不足，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_ir_list_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;

    if (s_code_count == 0) {
        snprintf(output, output_size, "No IR codes stored. Use ir_receive to learn codes.");
        return ESP_OK;
    }

    char *cursor    = output;
    size_t remaining = output_size;
    int written;

    written = snprintf(cursor, remaining, "Stored IR codes (%d):\n", s_code_count);
    if (written < 0 || (size_t)written >= remaining) goto overflow;
    cursor += written;
    remaining -= written;

    for (int i = 0; i < s_code_count; i++) {
        ir_code_entry_t *c = &s_codes[i];

        if (c->protocol == IR_PROTO_NEC) {
            written = snprintf(cursor, remaining,
                               "  %d. %s -- NEC addr=0x%04X cmd=0x%02X (%d raw symbols)\n",
                               i + 1, c->name,
                               (unsigned)c->address, (unsigned)c->command,
                               (int)c->raw_count);
        } else {
            /* Calculate approximate duration for raw codes */
            uint32_t total_us = 0;
            for (size_t j = 0; j < c->raw_count; j++) {
                total_us += c->raw[j];
            }
            written = snprintf(cursor, remaining,
                               "  %d. %s -- raw (%d symbols, %lums)\n",
                               i + 1, c->name,
                               (int)c->raw_count,
                               (unsigned long)(total_us / 1000));
        }

        if (written < 0 || (size_t)written >= remaining) goto overflow;
        cursor += written;
        remaining -= written;
    }

    return ESP_OK;

overflow:
    snprintf(output, output_size, "Error: output buffer too small for %d codes", s_code_count);
    return ESP_ERR_NO_MEM;
}
