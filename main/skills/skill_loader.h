#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * 函数名: skill_loader_init
 * 功能: 初始化技能系统，扫描SPIFFS中可用的技能markdown文件
 * 参数: 无
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t skill_loader_init(void);

/**
 * 函数名: skill_loader_build_summary
 * 功能: 构建所有可用技能的摘要，用于系统提示
 * 参数:
 *      buf - 输出缓冲区
 *      size - 缓冲区大小
 * 返回值: 写入的字节数，无技能时返回0
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
size_t skill_loader_build_summary(char *buf, size_t size);
