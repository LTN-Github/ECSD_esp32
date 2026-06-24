#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * 函数名: tool_web_search_init
 * 功能: 初始化web搜索工具，加载API密钥并选择搜索提供商
 * 参数: 无
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_web_search_init(void);

/**
 * 函数名: tool_web_search_execute
 * 功能: 执行web搜索操作
 * 参数:
 *      input_json - JSON格式输入，包含query字段
 *      output - 格式化搜索结果的输出缓冲区
 *      output_size - 输出缓冲区大小
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_web_search_execute(const char *input_json, char *output, size_t output_size);

/**
 * 函数名: tool_web_search_set_key
 * 功能: 保存Brave搜索API密钥到NVS
 * 参数:
 *      api_key - Brave API密钥字符串
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_web_search_set_key(const char *api_key);

/**
 * 函数名: tool_web_search_set_tavily_key
 * 功能: 保存Tavily API密钥到NVS
 * 参数:
 *      api_key - Tavily API密钥字符串
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_web_search_set_tavily_key(const char *api_key);
