#include "tool_ws2812.h"

#include <string.h>
#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "cJSON.h"

static const char *TAG = "ws2812";

/* ================================================================
 *  硬件参数
 * ================================================================
 *  RMT 分辨率 40MHz → 1 tick = 25ns
 *  WS2812 标准时序:
 *    T0H: 350ns → 14 tick   T0L: 800ns → 32 tick
 *    T1H: 700ns → 28 tick   T1L: 600ns → 24 tick
 *
 *  GPIO48, 1 LED, GRB 颜色顺序, MSB first
 * ================================================================ */
#define WS2812_GPIO         48           /* WS2812数据引脚（板载RGB LED） */
#define RMT_RESOLUTION_HZ   40000000    /* RMT分辨率：40MHz */
#define RMT_MEM_BLOCK_SYMS  64          /* RMT内存块符号数 */

#define WS_T0H  14   /* 0码高电平时间：350ns */
#define WS_T0L  32   /* 0码低电平时间：800ns */
#define WS_T1H  28   /* 1码高电平时间：700ns */
#define WS_T1L  24   /* 1码低电平时间：600ns */

/* ================================================================
 *  状态
 * ================================================================ */
static bool                 s_init_ok    = false;    /* RMT是否已初始化 */
static rmt_channel_handle_t s_tx_channel = NULL;     /* RMT发送通道句柄 */

/* 当前 LED 颜色 (便于后续扩展状态查询) */
static uint8_t s_cur_r = 0, s_cur_g = 0, s_cur_b = 0;  /* 当前RGB颜色值 */
static bool    s_is_on = false;                         /* LED是否开启 */

/* ================================================================
 *  初始化 RMT — 只做一次
 * ================================================================ */

/**
 * 函数名: ensure_rmt_init
 * 功能: 确保RMT驱动已初始化，只在首次调用时执行初始化
 * 参数: 无
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
static esp_err_t ensure_rmt_init(void)
{
    if (s_init_ok) return ESP_OK;

    /* 创建 TX 通道 */
    rmt_tx_channel_config_t ch_cfg = {
        .gpio_num          = WS2812_GPIO,
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = RMT_RESOLUTION_HZ,
        .mem_block_symbols = RMT_MEM_BLOCK_SYMS,
        .trans_queue_depth = 4,
    };

    esp_err_t err = rmt_new_tx_channel(&ch_cfg, &s_tx_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel: %s", esp_err_to_name(err));
        return err;
    }

    err = rmt_enable(s_tx_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_enable: %s", esp_err_to_name(err));
        rmt_del_channel(s_tx_channel);
        s_tx_channel = NULL;
        return err;
    }

    s_init_ok = true;
    ESP_LOGI(TAG, "init ok (GPIO%d, %dMHz)", WS2812_GPIO,
             RMT_RESOLUTION_HZ / 1000000);
    return ESP_OK;
}

/* ================================================================
 *  发送数据到 WS2812
 *
 *  手动构造 24 个 RMT symbol (3字节 × 8bit = 24),
 *  用 bytes_encoder 编码后发送.
 *
 *  24 symbols × ~1.25μs + 50μs reset ≈ 80μs,
 *  用 1ms 延时确保发送完成, 避免 wait_all_done 超时问题.
 * ================================================================ */

/**
 * 函数名: send_pixel
 * 功能: 发送RGB颜色数据到WS2812 LED
 * 参数:
 *      r - 红色分量（0-255）
 *      g - 绿色分量（0-255）
 *      b - 蓝色分量（0-255）
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
static esp_err_t send_pixel(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_init_ok) return ESP_ERR_INVALID_STATE;

    /* WS2812 GRB 顺序, MSB first per byte */
    uint8_t grb[3] = { g, r, b };

    /* bit0 / bit1 时序定义 */
    rmt_symbol_word_t bit0 = {
        .duration0 = WS_T0H, .level0 = 1,
        .duration1 = WS_T0L, .level1 = 0,
    };
    rmt_symbol_word_t bit1 = {
        .duration0 = WS_T1H, .level0 = 1,
        .duration1 = WS_T1L, .level1 = 0,
    };

    rmt_bytes_encoder_config_t enc_cfg = {
        .bit0  = bit0,
        .bit1  = bit1,
        .flags = { .msb_first = 1 },
    };

    rmt_encoder_handle_t enc = NULL;
    esp_err_t err = rmt_new_bytes_encoder(&enc_cfg, &enc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "enc: %s", esp_err_to_name(err));
        return err;
    }

    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,
        .flags      = { .eot_level = 0 },
    };

    err = rmt_transmit(s_tx_channel, enc, grb, sizeof(grb), &tx_cfg);
    rmt_del_encoder(enc);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tx: %s", esp_err_to_name(err));
        return err;
    }

    /* 延时等待 RMT 发送完成 (24 symbols ≈ 30μs, 1ms 绰绰有余) */
    vTaskDelay(pdMS_TO_TICKS(1));
    return ESP_OK;
}

/* ================================================================
 *  公开接口
 * ================================================================ */

/**
 * 函数名: tool_ws2812_init
 * 功能: 初始化WS2812工具，确保RMT驱动已初始化
 * 参数: 无
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_ws2812_init(void)
{
    return ensure_rmt_init();
}

/**
 * 函数名: tool_ws2812_set_execute
 * 功能: 执行设置WS2812 LED颜色操作
 * 参数:
 *      json - JSON格式输入，包含red、green、blue参数
 *      output - 结果输出缓冲区
 *      output_size - 输出缓冲区大小
 * 返回值: ESP_OK成功，ESP_FAIL初始化失败，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_ws2812_set_execute(const char *json,
                                  char *output, size_t output_size)
{
    if (ensure_rmt_init() != ESP_OK) {
        snprintf(output, output_size, "ERR: init failed");
        return ESP_FAIL;
    }

    uint8_t r = 0, g = 0, b = 0;
    cJSON *root = cJSON_Parse(json);
    if (root) {
        cJSON *jr = cJSON_GetObjectItem(root, "red");
        cJSON *jg = cJSON_GetObjectItem(root, "green");
        cJSON *jb = cJSON_GetObjectItem(root, "blue");
        if (jr && cJSON_IsNumber(jr)) r = (uint8_t)jr->valueint;
        if (jg && cJSON_IsNumber(jg)) g = (uint8_t)jg->valueint;
        if (jb && cJSON_IsNumber(jb)) b = (uint8_t)jb->valueint;
        cJSON_Delete(root);
    }

    ESP_LOGI(TAG, "set R=%d G=%d B=%d", r, g, b);

    esp_err_t err = send_pixel(r, g, b);
    if (err != ESP_OK) {
        snprintf(output, output_size, "ERR: %s", esp_err_to_name(err));
        return err;
    }

    s_cur_r = r; s_cur_g = g; s_cur_b = b;
    s_is_on = true;

    snprintf(output, output_size, "OK R=%d G=%d B=%d", r, g, b);
    return ESP_OK;
}

/**
 * 函数名: tool_ws2812_off_execute
 * 功能: 执行关闭WS2812 LED操作
 * 参数:
 *      json - JSON格式输入（此函数未使用）
 *      output - 结果输出缓冲区
 *      output_size - 输出缓冲区大小
 * 返回值: ESP_OK始终返回成功
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_ws2812_off_execute(const char *json,
                                  char *output, size_t output_size)
{
    (void)json;

    if (!s_init_ok) {
        snprintf(output, output_size, "OK (not init)");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "off");
    send_pixel(0, 0, 0);

    s_cur_r = 0; s_cur_g = 0; s_cur_b = 0;
    s_is_on = false;

    snprintf(output, output_size, "OK");
    return ESP_OK;
}
