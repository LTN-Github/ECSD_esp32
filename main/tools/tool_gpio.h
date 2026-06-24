#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * 函数名: tool_gpio_init
 * 功能: 初始化GPIO工具，配置允许的引脚和方向
 * 参数: 无
 * 返回值: ESP_OK始终返回成功
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_gpio_init(void);

/**
 * 函数名: tool_gpio_write_execute
 * 功能: 写入GPIO引脚，设置为高电平或低电平
 * 参数:
 *      input_json - JSON格式输入，包含pin和state参数
 *      output - 结果输出缓冲区
 *      output_size - 输出缓冲区大小
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_gpio_write_execute(const char *input_json, char *output, size_t output_size);

/**
 * 函数名: tool_gpio_read_execute
 * 功能: 读取单个GPIO引脚的电平状态
 * 参数:
 *      input_json - JSON格式输入，包含pin参数
 *      output - 结果输出缓冲区
 *      output_size - 输出缓冲区大小
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_gpio_read_execute(const char *input_json, char *output, size_t output_size);

/**
 * 函数名: tool_gpio_read_all_execute
 * 功能: 一次性读取所有允许的GPIO引脚状态
 * 参数:
 *      input_json - JSON格式输入（此函数未使用）
 *      output - 结果输出缓冲区
 *      output_size - 输出缓冲区大小
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_gpio_read_all_execute(const char *input_json, char *output, size_t output_size);
