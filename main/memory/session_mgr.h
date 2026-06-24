#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * 函数名: session_mgr_init
 * 功能: 初始化会话管理器
 * 参数: 无
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t session_mgr_init(void);

/**
 * 函数名: session_append
 * 功能: 追加消息到会话文件（JSONL格式）
 * 参数:
 *      chat_id - 会话标识符（如"12345"）
 *      role - 消息角色（"user"或"assistant"）
 *      content - 消息文本内容
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t session_append(const char *chat_id, const char *role, const char *content);

/**
 * 函数名: session_get_history_json
 * 功能: 加载会话历史为JSON数组字符串，适用于LLM消息格式
 * 参数:
 *      chat_id - 会话标识符
 *      buf - 输出缓冲区（调用者分配）
 *      size - 缓冲区大小
 *      max_msgs - 最大返回消息数量
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t session_get_history_json(const char *chat_id, char *buf, size_t size, int max_msgs);

/**
 * 函数名: session_clear
 * 功能: 清除会话（删除会话文件）
 * 参数:
 *      chat_id - 会话标识符
 * 返回值: ESP_OK成功，ESP_ERR_NOT_FOUND文件不存在
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t session_clear(const char *chat_id);

/**
 * 函数名: session_list
 * 功能: 列出所有会话文件（打印到日志）
 * 参数: 无
 * 返回值: 无
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
void session_list(void);
