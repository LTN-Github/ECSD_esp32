#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * 函数名: tool_ws2812_init
 * 功能: 初始化WS2812 LED（GPIO48，1个LED），重复调用安全
 * 参数: 无
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_ws2812_init(void);

/**
 * 函数名: tool_ws2812_set_execute
 * 功能: 设置LED颜色并立即刷新
 * 参数:
 *      json - JSON格式输入，包含red、green、blue参数（0-255）
 *      output - 结果输出缓冲区
 *      output_size - 输出缓冲区大小
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_ws2812_set_execute(const char *json, char *output, size_t output_size);

/**
 * 函数名: tool_ws2812_off_execute
 * 功能: 关闭LED
 * 参数:
 *      json - JSON格式输入（此函数未使用）
 *      output - 结果输出缓冲区
 *      output_size - 输出缓冲区大小
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_ws2812_off_execute(const char *json, char *output, size_t output_size);
