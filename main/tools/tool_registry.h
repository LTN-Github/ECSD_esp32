#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * 结构体名: mimi_tool_t
 * 功能: 定义工具结构体，包含工具的元信息和执行函数
 * 成员:
 *      name - 工具名称
 *      description - 工具描述
 *      input_schema_json - 输入参数的JSON Schema字符串
 *      execute - 工具执行函数指针
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
typedef struct {
    const char *name;                /* 工具名称 */
    const char *description;         /* 工具描述 */
    const char *input_schema_json;   /* 输入参数的JSON Schema字符串 */
    esp_err_t (*execute)(const char *input_json, char *output, size_t output_size);  /* 工具执行函数 */
} mimi_tool_t;

/**
 * 函数名: tool_registry_init
 * 功能: 初始化工具注册表并注册所有内置工具
 * 参数: 无
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_registry_init(void);

/**
 * 函数名: tool_registry_get_tools_json
 * 功能: 获取预构建的工具JSON数组字符串，用于API请求
 * 参数: 无
 * 返回值: JSON字符串指针，无工具时返回NULL
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
const char *tool_registry_get_tools_json(void);

/**
 * 函数名: tool_registry_execute
 * 功能: 根据名称执行指定工具
 * 参数:
 *      name - 工具名称（如"web_search"）
 *      input_json - JSON格式的工具输入
 *      output - 工具结果文本的输出缓冲区
 *      output_size - 输出缓冲区大小
 * 返回值: ESP_OK成功，ESP_ERR_NOT_FOUND工具未知，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_registry_execute(const char *name, const char *input_json,
                                char *output, size_t output_size);
