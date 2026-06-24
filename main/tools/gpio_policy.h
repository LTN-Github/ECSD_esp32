#pragma once

#include <stdbool.h>
#include <stddef.h>

/* ESP32-S3-LCD-1.47B开发板安全用户可访问引脚的GPIO默认配置 */
#define MIMI_GPIO_MIN_PIN       1      /* 允许的最小GPIO引脚编号 */
#define MIMI_GPIO_MAX_PIN       21     /* 允许的最大GPIO引脚编号 */
#define MIMI_GPIO_ALLOWED_CSV   "1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,21,38,46"  /* CSV格式的允许引脚列表 */

/**
 * 函数名: gpio_policy_pin_is_allowed
 * 功能: 检查GPIO引脚是否被允许用于用户GPIO操作
 * 参数:
 *      pin - 要检查的GPIO引脚编号
 * 返回值: true表示引脚被允许使用，false表示被禁止
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
bool gpio_policy_pin_is_allowed(int pin);

/**
 * 函数名: gpio_policy_pin_forbidden_hint
 * 功能: 当引脚因已知原因被禁止时，生成人类可读的提示信息
 * 参数:
 *      pin - 要查询的GPIO引脚编号
 *      result - 输出缓冲区，用于存储提示信息
 *      result_len - 输出缓冲区的大小
 * 返回值: true表示已生成提示信息（调用者应返回错误），false表示无提示
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
bool gpio_policy_pin_forbidden_hint(int pin, char *result, size_t result_len);
