#include "tools/gpio_policy.h"

#include "driver/gpio.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifndef GPIO_IS_VALID_GPIO
#define GPIO_IS_VALID_GPIO(pin) ((pin) >= 0)
#endif

/**
 * 函数名: pin_in_allowlist
 * 功能: 检查指定引脚是否在CSV格式的允许列表中
 * 参数:
 *      pin - 要检查的GPIO引脚编号
 *      csv - CSV格式的允许引脚列表字符串，如"1,2,3,4"
 * 返回值: true表示引脚在允许列表中，false表示不在
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
static bool pin_in_allowlist(int pin, const char *csv)
{
    const char *cursor;

    if (!csv || csv[0] == '\0') {
        return false;
    }

    cursor = csv;
    while (*cursor != '\0') {
        char *endptr = NULL;
        long value;

        while (*cursor == ' ' || *cursor == '\t' || *cursor == ',') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }

        value = strtol(cursor, &endptr, 10);
        if (endptr == cursor) {
            while (*cursor != '\0' && *cursor != ',') {
                cursor++;
            }
            continue;
        }

        if ((int)value == pin) {
            return true;
        }
        cursor = endptr;
    }

    return false;
}

/**
 * 函数名: pin_is_allowed_impl
 * 功能: GPIO引脚允许检查的核心实现，根据策略判断引脚是否可用
 * 参数:
 *      pin - 要检查的GPIO引脚编号
 *      allowlist_csv - CSV格式的允许引脚列表，为NULL时使用范围检查
 *      min_pin - 允许的最小引脚编号（范围检查模式）
 *      max_pin - 允许的最大引脚编号（范围检查模式）
 *      block_esp32_flash_pins - 是否阻止ESP32的Flash/PSRAM引脚(GPIO 6-11)
 *      block_esp32s3_usb_pins - 是否阻止ESP32-S3的USB Serial/JTAG引脚(GPIO 19/20)
 * 返回值: true表示引脚被允许使用，false表示被禁止
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
static bool pin_is_allowed_impl(int pin,
                                const char *allowlist_csv,
                                int min_pin,
                                int max_pin,
                                bool block_esp32_flash_pins,
                                bool block_esp32s3_usb_pins)
{
    bool in_policy;

    if (pin < 0) {
        return false;
    }

    /* Block ESP32 flash/PSRAM pins (GPIO 6-11) */
    if (block_esp32_flash_pins && pin >= 6 && pin <= 11) {
        return false;
    }

    /* USB Serial/JTAG uses GPIO19/20 on ESP32-S3 */
    if (block_esp32s3_usb_pins && (pin == 19 || pin == 20)) {
        return false;
    }

    if (allowlist_csv && allowlist_csv[0] != '\0') {
        in_policy = pin_in_allowlist(pin, allowlist_csv);
    } else {
        in_policy = pin >= min_pin && pin <= max_pin;
    }

    if (!in_policy) {
        return false;
    }

    return GPIO_IS_VALID_GPIO((gpio_num_t)pin);
}

/**
 * 函数名: gpio_policy_pin_is_allowed
 * 功能: 检查GPIO引脚是否被允许使用，根据不同的ESP32芯片型号应用不同策略
 * 参数:
 *      pin - 要检查的GPIO引脚编号
 * 返回值: true表示引脚被允许使用，false表示被禁止
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
bool gpio_policy_pin_is_allowed(int pin)
{
#if defined(CONFIG_IDF_TARGET_ESP32)
    return pin_is_allowed_impl(pin, MIMI_GPIO_ALLOWED_CSV,
                               MIMI_GPIO_MIN_PIN, MIMI_GPIO_MAX_PIN, true, false);
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    return pin_is_allowed_impl(pin, MIMI_GPIO_ALLOWED_CSV,
                               MIMI_GPIO_MIN_PIN, MIMI_GPIO_MAX_PIN, false, true);
#else
    return pin_is_allowed_impl(pin, MIMI_GPIO_ALLOWED_CSV,
                               MIMI_GPIO_MIN_PIN, MIMI_GPIO_MAX_PIN, false, false);
#endif
}

/**
 * 函数名: gpio_policy_pin_forbidden_hint
 * 功能: 获取GPIO引脚被禁止使用的详细提示信息
 * 参数:
 *      pin - 要查询的GPIO引脚编号
 *      result - 输出缓冲区，用于存储提示信息
 *      result_len - 输出缓冲区的大小
 * 返回值: true表示该引脚被禁止且已生成提示信息，false表示无特殊提示
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
bool gpio_policy_pin_forbidden_hint(int pin, char *result, size_t result_len)
{
#if defined(CONFIG_IDF_TARGET_ESP32)
    if (pin >= 6 && pin <= 11) {
        snprintf(result, result_len,
                 "Error: pin %d is reserved for ESP32 flash/PSRAM (GPIO6-11); choose a different pin",
                 pin);
        return true;
    }
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    if (pin == 19 || pin == 20) {
        snprintf(result, result_len,
                 "Error: pin %d is reserved for ESP32-S3 USB Serial/JTAG (GPIO19/20); choose a different pin",
                 pin);
        return true;
    }
#else
    (void)pin;
    (void)result;
    (void)result_len;
#endif

    return false;
}
