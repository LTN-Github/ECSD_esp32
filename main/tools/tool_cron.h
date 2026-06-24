#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * 函数名: tool_cron_add_execute
 * 功能: 添加定时任务，支持周期性执行和一次性定时执行
 * 参数:
 *      input_json - JSON格式输入，包含name、schedule_type、interval_s/at_epoch、message等参数
 *      output - 结果输出缓冲区
 *      output_size - 输出缓冲区大小
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_cron_add_execute(const char *input_json, char *output, size_t output_size);

/**
 * 函数名: tool_cron_list_execute
 * 功能: 列出所有已调度的定时任务
 * 参数:
 *      input_json - JSON格式输入（此函数未使用）
 *      output - 结果输出缓冲区
 *      output_size - 输出缓冲区大小
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_cron_list_execute(const char *input_json, char *output, size_t output_size);

/**
 * 函数名: tool_cron_remove_execute
 * 功能: 根据任务ID删除指定的定时任务
 * 参数:
 *      input_json - JSON格式输入，包含job_id参数
 *      output - 结果输出缓冲区
 *      output_size - 输出缓冲区大小
 * 返回值: ESP_OK成功，ESP_ERR_NOT_FOUND任务不存在，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_cron_remove_execute(const char *input_json, char *output, size_t output_size);
