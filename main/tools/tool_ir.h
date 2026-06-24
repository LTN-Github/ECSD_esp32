#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * 函数名: tool_ir_init
 * 功能: 初始化IR子系统，包括RMT通道和从SPIFFS加载IR代码库
 * 参数: 无
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_ir_init(void);

/**
 * 函数名: tool_ir_receive_execute
 * 功能: 通过捕获一帧IR信号来学习遥控代码
 * 参数:
 *      input_json - JSON格式输入，包含name参数
 *      output - 结果输出缓冲区
 *      output_size - 输出缓冲区大小
 * 返回值: ESP_OK成功，ESP_ERR_TIMEOUT超时，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_ir_receive_execute(const char *input_json, char *output, size_t output_size);

/**
 * 函数名: tool_ir_send_execute
 * 功能: 发送已存储的IR代码
 * 参数:
 *      input_json - JSON格式输入，包含name和可选repeat参数
 *      output - 结果输出缓冲区
 *      output_size - 输出缓冲区大小
 * 返回值: ESP_OK成功，ESP_ERR_NOT_FOUND代码不存在，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_ir_send_execute(const char *input_json, char *output, size_t output_size);

/**
 * 函数名: tool_ir_list_execute
 * 功能: 列出所有已存储的IR代码
 * 参数:
 *      input_json - JSON格式输入（此函数未使用）
 *      output - 结果输出缓冲区
 *      output_size - 输出缓冲区大小
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_ir_list_execute(const char *input_json, char *output, size_t output_size);
