#include "memory_store.h"
#include "mimi_config.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "esp_log.h"

static const char *TAG = "memory";

/**
 * 函数名: get_date_str
 * 功能: 获取指定天数前的日期字符串
 * 参数:
 *      buf - 输出缓冲区，存储日期字符串
 *      size - 缓冲区大小
 *      days_ago - 天数偏移量（0表示今天，1表示昨天）
 * 返回值: 无
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
static void get_date_str(char *buf, size_t size, int days_ago)
{
    time_t now;
    time(&now);
    now -= days_ago * 86400;
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(buf, size, "%Y-%m-%d", &tm);
}

/**
 * 函数名: memory_store_init
 * 功能: 初始化记忆存储系统
 * 参数: 无
 * 返回值: ESP_OK始终返回成功
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t memory_store_init(void)
{
    /* SPIFFS is flat — no real directory creation needed.
       Just verify we can open the base path. */
    ESP_LOGI(TAG, "Memory store initialized at %s", MIMI_SPIFFS_BASE);
    return ESP_OK;
}

/**
 * 函数名: memory_read_long_term
 * 功能: 读取长期记忆文件（MEMORY.md）到缓冲区
 * 参数:
 *      buf - 输出缓冲区
 *      size - 缓冲区大小
 * 返回值: ESP_OK成功，ESP_ERR_NOT_FOUND文件不存在
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t memory_read_long_term(char *buf, size_t size)
{
    FILE *f = fopen(MIMI_MEMORY_FILE, "r");
    if (!f) {
        buf[0] = '\0';
        return ESP_ERR_NOT_FOUND;
    }

    size_t n = fread(buf, 1, size - 1, f);
    buf[n] = '\0';
    fclose(f);
    return ESP_OK;
}

/**
 * 函数名: memory_write_long_term
 * 功能: 写入内容到长期记忆文件（MEMORY.md）
 * 参数:
 *      content - 要写入的内容字符串
 * 返回值: ESP_OK成功，ESP_FAIL写入失败
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t memory_write_long_term(const char *content)
{
    FILE *f = fopen(MIMI_MEMORY_FILE, "w");
    if (!f) {
        ESP_LOGE(TAG, "Cannot write %s", MIMI_MEMORY_FILE);
        return ESP_FAIL;
    }
    fputs(content, f);
    fclose(f);
    ESP_LOGI(TAG, "Long-term memory updated (%d bytes)", (int)strlen(content));
    return ESP_OK;
}

/**
 * 函数名: memory_append_today
 * 功能: 追加笔记到今日的记忆文件（YYYY-MM-DD.md）
 * 参数:
 *      note - 要追加的笔记内容
 * 返回值: ESP_OK成功，ESP_FAIL操作失败
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t memory_append_today(const char *note)
{
    char date_str[16];
    get_date_str(date_str, sizeof(date_str), 0);

    char path[64];
    snprintf(path, sizeof(path), "%s/%s.md", MIMI_SPIFFS_MEMORY_DIR, date_str);

    FILE *f = fopen(path, "a");
    if (!f) {
        /* Try creating — if file doesn't exist yet, write header */
        f = fopen(path, "w");
        if (!f) {
            ESP_LOGE(TAG, "Cannot open %s", path);
            return ESP_FAIL;
        }
        fprintf(f, "# %s\n\n", date_str);
    }

    fprintf(f, "%s\n", note);
    fclose(f);
    return ESP_OK;
}

/**
 * 函数名: memory_read_recent
 * 功能: 读取最近几天的记忆文件
 * 参数:
 *      buf - 输出缓冲区
 *      size - 缓冲区大小
 *      days - 要读取的天数
 * 返回值: ESP_OK始终返回成功
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t memory_read_recent(char *buf, size_t size, int days)
{
    size_t offset = 0;
    buf[0] = '\0';

    for (int i = 0; i < days && offset < size - 1; i++) {
        char date_str[16];
        get_date_str(date_str, sizeof(date_str), i);

        char path[64];
        snprintf(path, sizeof(path), "%s/%s.md", MIMI_SPIFFS_MEMORY_DIR, date_str);

        FILE *f = fopen(path, "r");
        if (!f) continue;

        if (offset > 0 && offset < size - 4) {
            offset += snprintf(buf + offset, size - offset, "\n---\n");
        }

        size_t n = fread(buf + offset, 1, size - offset - 1, f);
        offset += n;
        buf[offset] = '\0';
        fclose(f);
    }

    return ESP_OK;
}
