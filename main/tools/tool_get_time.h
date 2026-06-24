#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * 函数名: tool_get_time_execute
 * 功能: 执行获取当前时间工具，通过HTTP Date头获取时间并设置系统时钟
 * 参数:
 *      input_json - JSON格式输入（此函数未使用）
 *      output - 结果输出缓冲区，存储格式化的本地时间
 *      output_size - 输出缓冲区大小
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_get_time_execute(const char *input_json, char *output, size_t output_size);
