#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * 函数名: tool_read_file_execute
 * 功能: 从SPIFFS文件系统读取指定路径的文件内容
 * 参数:
 *      input_json - JSON格式输入，包含path参数
 *      output - 结果输出缓冲区
 *      output_size - 输出缓冲区大小
 * 返回值: ESP_OK成功，ESP_ERR_NOT_FOUND文件不存在，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_read_file_execute(const char *input_json, char *output, size_t output_size);

/**
 * 函数名: tool_write_file_execute
 * 功能: 向SPIFFS文件系统写入或覆盖指定路径的文件
 * 参数:
 *      input_json - JSON格式输入，包含path和content参数
 *      output - 结果输出缓冲区
 *      output_size - 输出缓冲区大小
 * 返回值: ESP_OK成功，ESP_FAIL写入失败，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_write_file_execute(const char *input_json, char *output, size_t output_size);

/**
 * 函数名: tool_edit_file_execute
 * 功能: 编辑SPIFFS文件系统中的文件，查找并替换指定字符串
 * 参数:
 *      input_json - JSON格式输入，包含path、old_string和new_string参数
 *      output - 结果输出缓冲区
 *      output_size - 输出缓冲区大小
 * 返回值: ESP_OK成功，ESP_ERR_NOT_FOUND文件或字符串不存在，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_edit_file_execute(const char *input_json, char *output, size_t output_size);

/**
 * 函数名: tool_list_dir_execute
 * 功能: 列出SPIFFS文件系统中的文件，可选按路径前缀过滤
 * 参数:
 *      input_json - JSON格式输入，可选prefix参数
 *      output - 结果输出缓冲区
 *      output_size - 输出缓冲区大小
 * 返回值: ESP_OK成功，ESP_FAIL打开目录失败，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_list_dir_execute(const char *input_json, char *output, size_t output_size);
