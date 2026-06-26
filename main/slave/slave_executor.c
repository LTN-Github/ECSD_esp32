/**
 * @file slave_executor.c
 * @brief Slave executor — receives ESP-NOW commands from master and
 *        executes hardware tools locally (WS2812, GPIO, IR).
 */

#include "slave/slave_executor.h"
#include "mimi_config.h"

#include "tools/tool_ws2812.h"
#include "tools/tool_gpio.h"
#include "tools/tool_ir.h"
#include "espnow/esp_now_device.h"
#include "espnow/esp_now_manager.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "slave";

/* ── Tool dispatch table entry ──────────────────────────────────── */
/**
 * 回调类型: slave_tool_t
 * 功能: 从设备工具调度表条目类型，包含工具名称和对应的执行函数指针
 * 参数:
 *      name - 工具名称字符串
 *      execute - 工具执行回调函数，参数为输入JSON、输出缓冲区和输出大小
 * 返回值: execute回调返回ESP_OK表示成功，其他值表示失败
 * 作者: Lskipktw
 * 日期: 2026-06-24  14:28:15
 */
typedef struct {
    const char *name;
    esp_err_t (*execute)(const char *input_json, char *output, size_t output_size);
} slave_tool_t;

/* ── All hardware tools the slave can execute ────────────────────── */
static const slave_tool_t s_tools[] = {
    { "ws2812_set",    tool_ws2812_set_execute    },
    { "ws2812_off",    tool_ws2812_off_execute    },
    { "gpio_write",    tool_gpio_write_execute    },
    { "gpio_read",     tool_gpio_read_execute     },
    { "gpio_read_all", tool_gpio_read_all_execute },
    { "ir_send",       tool_ir_send_execute       },
    { "ir_list",       tool_ir_list_execute       },
};

#define TOOL_COUNT (sizeof(s_tools) / sizeof(s_tools[0]))

static bool s_inited = false;

/* ── Public API ─────────────────────────────────────────────────── */

/**
 * 函数名: slave_executor_init
 * 功能: 初始化从设备执行器，依次初始化WS2812、GPIO和IR三个硬件工具模块
 * 参数: 无
 * 返回值: ESP_OK表示初始化成功
 * 作者: Lskipktw
 * 日期: 2026-06-24  14:28:15
 */
esp_err_t slave_executor_init(void)
{
    if (s_inited) return ESP_OK;

    tool_ws2812_init();   /* RMT通道，GPIO48，自初始化 */
    tool_gpio_init();     /* GPIO策略设置 */
    tool_ir_init();       /* RMT + SPIFFS 红外码库 */

    s_inited = true;
    ESP_LOGI(TAG, "Slave executor ready (%d tools)", (int)TOOL_COUNT);
    return ESP_OK;
}

/**
 * 函数名: slave_executor_handle
 * 功能: 接收并处理主设备发来的JSON命令，解析tool/args/call_id字段，通过互斥锁保护查找并执行对应硬件工具，最后构建响应包单播回主设备
 * 参数:
 *      src_mac - 主设备MAC地址（6字节），用于回传响应
 *      json - 主设备发来的JSON命令字符串（非\0终止）
 *      json_len - JSON字符串的长度（字节数）
 * 返回值: ESP_OK表示命令执行成功；ESP_ERR_INVALID_ARG表示参数或JSON无效；ESP_ERR_NO_MEM表示内存不足；ESP_ERR_NOT_FOUND表示工具名未知；ESP_ERR_TIMEOUT表示执行器正忙
 * 作者: Lskipktw
 * 日期: 2026-06-24  14:28:15
 */
esp_err_t slave_executor_handle(const uint8_t *src_mac,    // 参数1：主设备MAC地址（6字节），用于回传响应
                                 const char *json, int json_len)  // 参数2：命令JSON字符串（可能非\0终止），参数3：JSON长度
{
    if (!src_mac || !json || json_len <= 0) {             // 参数合法性校验：源MAC为空、JSON为空或长度<=0均非法
        ESP_LOGE(TAG, "Invalid command");                 // 输出ERROR日志：无效命令
        return ESP_ERR_INVALID_ARG;                       // 返回"无效参数"错误码
    }

    /* 延迟初始化（安全兜底） */
    if (!s_inited) {                                      // 检查执行器是否已初始化（延迟初始化安全机制）
        slave_executor_init();                            // 尚未初始化则自动调用初始化函数
    }

    /* 解析命令JSON */
    char *json_null_term = strndup(json, json_len);       // 将非\0终止的JSON字符串拷贝为\0终止的C字符串
    if (!json_null_term) {                                // 检查strndup是否分配失败（内存不足）
        ESP_LOGE(TAG, "OOM copying command");             // 输出ERROR日志：内存不足
        return ESP_ERR_NO_MEM;                            // 返回"内存不足"错误码
    }

    cJSON *root = cJSON_Parse(json_null_term);            // 使用cJSON库解析JSON字符串为JSON对象树
    free(json_null_term);                                 // 及时释放已解析的临时JSON字符串，防止内存泄漏

    if (!root) {                                          // 检查JSON解析是否失败（格式不合法）
        ESP_LOGE(TAG, "Invalid JSON command");            // 输出ERROR日志：无效的JSON命令
        return ESP_ERR_INVALID_ARG;                       // 返回"无效参数"错误码
    }

    /* 提取JSON字段 */
    cJSON *tool_item   = cJSON_GetObjectItem(root, "tool");   // 从JSON对象中提取"tool"字段（工具名称）
    cJSON *args_item   = cJSON_GetObjectItem(root, "args");   // 从JSON对象中提取"args"字段（工具参数，可选）
    cJSON *callid_item = cJSON_GetObjectItem(root, "call_id");// 从JSON对象中提取"call_id"字段（请求追踪ID）

    const char *tool_name = cJSON_IsString(tool_item) ? tool_item->valuestring : NULL;  // 安全获取工具名称字符串，非字符串类型则置NULL
    const char *call_id   = cJSON_IsString(callid_item) ? callid_item->valuestring : ""; // 安全获取请求ID字符串，非字符串类型则置空串

    if (!tool_name) {                                     // 检查工具名称是否为空（JSON缺少"tool"字段或类型不匹配）
        ESP_LOGE(TAG, "Command missing 'tool' field");    // 输出ERROR日志：缺少tool字段
        cJSON_Delete(root);                               // 释放cJSON对象树内存
        return ESP_ERR_INVALID_ARG;                       // 返回"无效参数"错误码
    }

    ESP_LOGI(TAG, "Executing tool '%s' (call_id=%s) from master "  // 输出INFO日志：开始执行工具
             "%02X:%02X:%02X:%02X:%02X:%02X",            // MAC地址格式化占位符
             tool_name, call_id,                          // 打印工具名和请求ID
             src_mac[0], src_mac[1], src_mac[2],         // 打印源MAC第1-3字节
             src_mac[3], src_mac[4], src_mac[5]);        // 打印源MAC第4-6字节

    /* 互斥锁保护，同一时刻只执行一个工具 */
    static SemaphoreHandle_t s_mutex = NULL;              // 静态互斥锁句柄，首次调用时创建（static保证只创建一次）
    if (!s_mutex) {                                       // 检查互斥锁是否已创建
        s_mutex = xSemaphoreCreateMutex();                // 调用FreeRTOS API创建二值互斥锁
    }
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10000)) != pdTRUE) {  // 尝试获取互斥锁，超时时间10秒
        ESP_LOGW(TAG, "Tool execution busy, dropping command"); // 获取锁失败（超时）：说明正在执行其他工具
        cJSON_Delete(root);                               // 释放cJSON对象树
        return ESP_ERR_TIMEOUT;                           // 返回"超时"错误码，丢弃当前命令
    }

    /* 查找并执行工具 */
    char output[768];                                     // 声明输出缓冲区（768字节），用于存放工具执行结果
    esp_err_t exec_err = ESP_ERR_NOT_FOUND;               // 初始化执行错误码为"未找到"（默认失败状态）

    for (size_t i = 0; i < TOOL_COUNT; i++) {             // 遍历工具调度表（7个工具）
        if (strcmp(s_tools[i].name, tool_name) == 0) {    // 比较调度表中的工具名是否与请求的工具名匹配
            /* 将args序列化为JSON字符串传给执行函数 */
            char *args_json = args_item ? cJSON_PrintUnformatted(args_item) : strdup("{}");  // 将args对象序列化为JSON字符串；若无args则使用空对象"{}"
            if (args_json) {                              // 检查序列化是否成功
                output[0] = '\0';                         // 初始化输出缓冲区为空字符串
                exec_err = s_tools[i].execute(args_json, output, sizeof(output));  // 调用匹配的工具执行函数，传入JSON参数和输出缓冲区
                free(args_json);                          // 释放序列化生成的临时JSON字符串
            } else {                                      // 序列化失败（内存不足）
                exec_err = ESP_ERR_NO_MEM;                // 设置错误码为"内存不足"
            }
            break;                                        // 已找到并执行工具，跳出循环
        }
    }

    if (exec_err == ESP_ERR_NOT_FOUND) {                  // 检查是否未匹配到任何工具（仍为初始默认值）
        snprintf(output, sizeof(output), "Unknown tool: %s", tool_name);  // 将"未知工具"错误信息写入输出缓冲区
        ESP_LOGW(TAG, "Unknown tool: %s", tool_name);     // 输出WARNING日志：未知工具名
    } else {                                              // 工具已找到并执行（成功或失败）
        ESP_LOGI(TAG, "Tool '%s' result: %s (err=%s)",    // 输出INFO日志：工具执行结果
                 tool_name, output, esp_err_to_name(exec_err));  // 打印工具名、输出内容和错误码名称
    }

    xSemaphoreGive(s_mutex);                              // 释放互斥锁，允许后续命令执行
    cJSON_Delete(root);                                   // 释放cJSON对象树内存

    /* ── 构建响应并回传给主设备 ──────────────────────────────────── */
    cJSON *rsp = cJSON_CreateObject();                    // 创建新的cJSON对象用于构建响应
    cJSON_AddStringToObject(rsp, "call_id", call_id);     // 向响应对象添加"call_id"字段（回显请求ID）
    cJSON_AddNumberToObject(rsp, "error", exec_err);      // 向响应对象添加"error"字段（错误码数值）
    cJSON_AddStringToObject(rsp, "result", output);       // 向响应对象添加"result"字段（工具输出字符串或错误信息）
    char *rsp_json = cJSON_PrintUnformatted(rsp);         // 将响应对象序列化为紧凑JSON字符串（无格式化）
    cJSON_Delete(rsp);                                    // 释放响应cJSON对象

    if (!rsp_json) {                                      // 检查JSON序列化是否失败
        return ESP_ERR_NO_MEM;                            // 返回"内存不足"错误码
    }

    /* 构建响应包：[ESPNOW_PROTO_CMD_RSP][source_device_id(32)][rsp_json] */
    size_t rsp_len = strlen(rsp_json);                    // 计算响应JSON字符串的长度（不含\0）
    size_t pkt_len = 1 + ESPNOW_DEVICE_ID_MAX_LEN + rsp_len;  // 计算总包长度 = 1B协议头 + 32B设备ID + 变长JSON
    uint8_t *pkt = malloc(pkt_len);                       // 在堆上分配包缓冲区
    if (!pkt) {                                           // 检查分配是否失败
        free(rsp_json);                                   // 释放已序列化的JSON字符串
        return ESP_ERR_NO_MEM;                            // 返回"内存不足"错误码
    }

    pkt[0] = ESPNOW_PROTO_CMD_RSP;                        // 包首字节填入协议类型：命令响应（0x05）
    const char *my_id = esp_now_device_get_id();          // 获取本机设备ID字符串
    memset(pkt + 1, 0, ESPNOW_DEVICE_ID_MAX_LEN);         // 将32字节设备ID区域清零（确保无残留数据）
    strncpy((char *)(pkt + 1), my_id, ESPNOW_DEVICE_ID_MAX_LEN - 1);  // 将设备ID安全复制到包的第2-33字节位置
    memcpy(pkt + 1 + ESPNOW_DEVICE_ID_MAX_LEN, rsp_json, rsp_len);    // 将响应JSON复制到包的第34字节之后

    /* 将主设备添加为对等节点（幂等操作） */
    esp_now_manager_add_peer(src_mac);                    // 确保主设备MAC在ESP-NOW对等列表中（幂等操作，已存在则静默成功）

    esp_err_t send_err = esp_now_manager_send(src_mac, pkt, pkt_len);  // 通过ESP-NOW单播发送响应包给主设备
    if (send_err != ESP_OK) {                             // 判断发送是否失败
        ESP_LOGE(TAG, "Failed to send response: %s", esp_err_to_name(send_err));  // 发送失败：输出ERROR日志
    } else {                                              // 发送成功
        ESP_LOGI(TAG, "Response sent to master (%d bytes)", (int)pkt_len);  // 输出INFO日志：记录发送字节数
    }

    free(pkt);                                            // 释放包缓冲区内存
    free(rsp_json);                                       // 释放响应JSON字符串内存
    return exec_err;                                      // 返回工具执行结果（而非发送结果）给上层调用者
}
