#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * 函数名: memory_store_init
 * 功能: 初始化记忆存储系统，确保SPIFFS目录存在
 * 参数: 无
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t memory_store_init(void);

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
esp_err_t memory_read_long_term(char *buf, size_t size);

/**
 * 函数名: memory_write_long_term
 * 功能: 写入内容到长期记忆文件（MEMORY.md）
 * 参数:
 *      content - 要写入的内容字符串
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t memory_write_long_term(const char *content);

/**
 * 函数名: memory_append_today
 * 功能: 追加笔记到今日的记忆文件（YYYY-MM-DD.md）
 * 参数:
 *      note - 要追加的笔记内容
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t memory_append_today(const char *note);

/**
 * 函数名: memory_read_recent
 * 功能: 读取最近几天的记忆文件
 * 参数:
 *      buf - 输出缓冲区
 *      size - 缓冲区大小
 *      days - 要读取的天数（默认3天）
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t memory_read_recent(char *buf, size_t size, int days);
