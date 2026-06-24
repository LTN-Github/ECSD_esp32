#include "session_mgr.h"
#include "mimi_config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <time.h>
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "session";

/**
 * 函数名: session_path
 * 功能: 构建会话文件的完整路径
 * 参数:
 *      chat_id - 会话标识符（如"12345"）
 *      buf - 输出缓冲区，存储文件路径
 *      size - 缓冲区大小
 * 返回值: 无
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
static void session_path(const char *chat_id, char *buf, size_t size)
{
    snprintf(buf, size, "%s/tg_%s.jsonl", MIMI_SPIFFS_SESSION_DIR, chat_id);
}

/**
 * 函数名: session_mgr_init
 * 功能: 初始化会话管理器
 * 参数: 无
 * 返回值: ESP_OK始终返回成功
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t session_mgr_init(void)
{
    ESP_LOGI(TAG, "Session manager initialized at %s", MIMI_SPIFFS_SESSION_DIR);
    return ESP_OK;
}

/**
 * 函数名: session_append
 * 功能: 追加消息到会话文件（JSONL格式）
 * 参数:
 *      chat_id - 会话标识符（如"12345"）
 *      role - 消息角色（"user"或"assistant"）
 *      content - 消息文本内容
 * 返回值: ESP_OK成功，ESP_FAIL操作失败
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t session_append(const char *chat_id, const char *role, const char *content)
{
    char path[64];
    session_path(chat_id, path, sizeof(path));

    FILE *f = fopen(path, "a");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open session file %s", path);
        return ESP_FAIL;
    }

    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "role", role);
    cJSON_AddStringToObject(obj, "content", content);
    cJSON_AddNumberToObject(obj, "ts", (double)time(NULL));

    char *line = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);

    if (line) {
        fprintf(f, "%s\n", line);
        free(line);
    }

    fclose(f);
    return ESP_OK;
}

/**
 * 函数名: session_get_history_json
 * 功能: 加载会话历史为JSON数组字符串，适用于LLM消息格式
 * 参数:
 *      chat_id - 会话标识符
 *      buf - 输出缓冲区（调用者分配）
 *      size - 缓冲区大小
 *      max_msgs - 最大返回消息数量
 * 返回值: ESP_OK成功，ESP_ERR_NO_MEM内存不足
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t session_get_history_json(const char *chat_id, char *buf, size_t size, int max_msgs)
{
    char path[64];
    session_path(chat_id, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) {
        /* No history yet */
        snprintf(buf, size, "[]");
        return ESP_OK;
    }

    /* Read at most max_msgs lines, keep only newest */
    int ring_size = max_msgs > 0 ? max_msgs : 20;
    cJSON **messages = calloc(ring_size, sizeof(cJSON *));
    if (!messages) {
        fclose(f);
        snprintf(buf, size, "[]");
        return ESP_ERR_NO_MEM;
    }

    int count = 0;
    int write_idx = 0;

    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (line[0] == '\0') continue;

        cJSON *obj = cJSON_Parse(line);
        if (!obj) continue;

        /* Ring buffer: delete overwritten slot before replacing */
        if (count >= ring_size) {
            cJSON_Delete(messages[write_idx]);
            messages[write_idx] = NULL;
        }
        messages[write_idx] = obj;
        write_idx = (write_idx + 1) % ring_size;
        if (count < ring_size) count++;
    }
    fclose(f);

    /* Build JSON array */
    cJSON *arr = cJSON_CreateArray();
    int start = (count < ring_size) ? 0 : write_idx;
    for (int i = 0; i < count; i++) {
        int idx = (start + i) % ring_size;
        cJSON *src = messages[idx];
        if (!src) continue;

        cJSON *entry = cJSON_CreateObject();
        cJSON *role = cJSON_GetObjectItem(src, "role");
        cJSON *content = cJSON_GetObjectItem(src, "content");
        if (role && content) {
            cJSON_AddStringToObject(entry, "role", role->valuestring);
            cJSON_AddStringToObject(entry, "content", content->valuestring);
        }
        cJSON_AddItemToArray(arr, entry);
    }

    /* Cleanup */
    for (int i = 0; i < ring_size; i++) {
        if (messages[i]) cJSON_Delete(messages[i]);
    }
    free(messages);

    char *json_str = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    if (json_str) {
        strncpy(buf, json_str, size - 1);
        buf[size - 1] = '\0';
        free(json_str);
    } else {
        snprintf(buf, size, "[]");
    }

    return ESP_OK;
}

/**
 * 函数名: session_clear
 * 功能: 清除会话（删除会话文件）
 * 参数:
 *      chat_id - 会话标识符
 * 返回值: ESP_OK成功，ESP_ERR_NOT_FOUND文件不存在
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t session_clear(const char *chat_id)
{
    char path[64];
    session_path(chat_id, path, sizeof(path));

    if (remove(path) == 0) {
        ESP_LOGI(TAG, "Session %s cleared", chat_id);
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

/**
 * 函数名: session_list
 * 功能: 列出所有会话文件（打印到日志）
 * 参数: 无
 * 返回值: 无
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
void session_list(void)
{
    DIR *dir = opendir(MIMI_SPIFFS_SESSION_DIR);
    if (!dir) {
        /* SPIFFS is flat, so list all files matching pattern */
        dir = opendir(MIMI_SPIFFS_BASE);
        if (!dir) {
            ESP_LOGW(TAG, "Cannot open SPIFFS directory");
            return;
        }
    }

    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, "tg_") && strstr(entry->d_name, ".jsonl")) {
            ESP_LOGI(TAG, "  Session: %s", entry->d_name);
            count++;
        }
    }
    closedir(dir);

    if (count == 0) {
        ESP_LOGI(TAG, "  No sessions found");
    }
}
