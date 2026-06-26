#include "esp_now_device.h"                                // 包含ESP-NOW设备层头文件，引入协议类型、结构体和API声明
#include "esp_now_manager.h"                              // 包含ESP-NOW管理器头文件，引入广播/单播发送和管理器API
#include "slave/slave_executor.h"                         // 包含从机执行器头文件，从机接收命令后交由执行器处理

#include "esp_log.h"                                      // 包含ESP-IDF日志库，提供分级日志输出宏
#include "esp_wifi.h"                                     // 包含ESP-IDF WiFi库，提供WiFi模式相关定义
#include "esp_mac.h"                                      // 包含ESP-IDF MAC地址库，提供esp_read_mac读取本机MAC地址
#include "nvs_flash.h"                                    // 包含NVS闪存库初始化头文件
#include "nvs.h"                                          // 包含NVS非易失性存储库，提供键值对读写API
#include "freertos/FreeRTOS.h"                            // 包含FreeRTOS核心头文件，提供任务相关类型
#include "freertos/task.h"                                // 包含FreeRTOS任务头文件，提供vTaskDelay等任务控制API
#include <string.h>                                       // 包含C标准库字符串操作函数（strncpy, strcmp, memset, memcpy, strlen）
#include <stdio.h>                                        // 包含C标准库输入输出函数（snprintf）

/* 依赖 mimi_config.h 中的宏 */
#if __has_include("mimi_config.h")                        // 编译期检查mimi_config.h是否存在
#include "mimi_config.h"                                  // 条件包含设备配置文件，其中定义了MIMI_SECRET_DEVICE_ID等宏
#endif

static const char *TAG = "espnow-device";                 // 定义模块日志标签常量，用于ESP_LOGx宏中标识日志来源

/* ── NVS 命名空间和键 ──────────────────────────────────────────── */
#define NVS_NAMESPACE   "device_config"                   // 定义NVS命名空间为"device_config"，用于隔离不同模块的存储
#define NVS_KEY_ID      "device_id"                       // 定义NVS中存储设备ID的键名
#define NVS_KEY_ROLE    "device_role"                     // 定义NVS中存储设备角色的键名

/* ── 默认值（宏为空时使用） ────────────────────────────────────── */
#ifndef MIMI_SECRET_DEVICE_ID                              // 如果编译时未定义MIMI_SECRET_DEVICE_ID宏
#define MIMI_SECRET_DEVICE_ID ""                          // 则定义为空字符串作为默认值
#endif
#ifndef MIMI_SECRET_DEVICE_ROLE                            // 如果编译时未定义MIMI_SECRET_DEVICE_ROLE宏
#define MIMI_SECRET_DEVICE_ROLE ""                        // 则定义为空字符串作为默认值
#endif

/* ── 本机信息 ──────────────────────────────────────────────────── */
static char              s_device_id[ESPNOW_DEVICE_ID_MAX_LEN];  // 本机设备ID字符串缓冲区，最大32字节
static esp_now_device_role_t s_role = ESP_NOW_DEVICE_ROLE_SLAVE; // 本机设备角色，默认为从设备
static uint8_t           s_mac[6];                        // 本机MAC地址缓冲区，6字节

/* ── 从设备列表（仅主设备使用） ────────────────────────────────── */
static esp_now_slave_info_t s_slaves[ESPNOW_MAX_SLAVES];  // 已发现从设备信息数组，最大容量8个
static int                   s_slave_count = 0;           // 当前已发现的从设备数量，初始为0

/* ── 发现状态 ──────────────────────────────────────────────────── */
static bool s_discovering = false;                        // 设备发现进行中标志，true表示正在扫描从设备
static int  s_disco_rsp_received = 0;                     // 本次发现过程中收到的响应计数

/* ── 内部工具 ──────────────────────────────────────────────────── */

/**
 * 函数名: mac_str
 * 功能: MAC地址转字符串的内部封装函数，直接调用esp_now_manager_mac_to_str
 * 参数:
 *      mac - 源MAC地址缓冲区指针（6字节）
 *      out - 输出字符串缓冲区指针
 *      size - 输出缓冲区大小（需>=18字节）
 * 返回值: 无
 * 作者: Lskipktw
 * 日期: 2026-05-09  08:07:14
 */
static void mac_str(const uint8_t *mac, char *out, size_t size)  // MAC转字符串封装函数定义
{
    esp_now_manager_mac_to_str(mac, out, size);           // 直接委托给管理器层的MAC转字符串函数
}

/**
 * 函数名: nvs_read_str
 * 功能: 从NVS中读取字符串值
 * 参数:
 *      key - NVS存储键名
 *      out - 输出字符串缓冲区指针
 *      max_len - 输出缓冲区最大长度
 * 返回值: ESP_OK表示读取成功，其他值表示失败（如键不存在）
 * 作者: Lskipktw
 * 日期: 2026-05-09  08:33:28
 */
static esp_err_t nvs_read_str(const char *key, char *out, size_t max_len)  // NVS读字符串函数定义
{
    nvs_handle_t handle;                                  // 声明NVS句柄变量
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);  // 以只读模式打开NVS命名空间，获取句柄
    if (ret != ESP_OK) return ret;                        // 打开失败则直接返回错误码

    size_t len = max_len;                                 // 设置读取缓冲区的最大长度
    ret = nvs_get_str(handle, key, out, &len);            // 从NVS中读取指定键的字符串值到out缓冲区
    nvs_close(handle);                                    // 关闭NVS句柄，释放资源
    return ret;                                           // 返回读取结果
}

/**
 * 函数名: nvs_write_str
 * 功能: 将字符串值写入NVS并提交
 * 参数:
 *      key - NVS存储键名
 *      value - 要写入的字符串值
 * 返回值: ESP_OK表示写入成功，其他值表示写入失败
 * 作者: Lskipktw
 * 日期: 2026-05-09  09:11:52
 */
static esp_err_t nvs_write_str(const char *key, const char *value)  // NVS写字符串函数定义
{
    nvs_handle_t handle;                                  // 声明NVS句柄变量
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);  // 以读写模式打开NVS命名空间，获取句柄
    if (ret != ESP_OK) return ret;                        // 打开失败则直接返回错误码

    ret = nvs_set_str(handle, key, value);                // 将字符串值写入NVS中指定键
    if (ret == ESP_OK) {                                  // 判断写入是否成功
        nvs_commit(handle);                               // 写入成功后提交更改，确保持久化到闪存
    }
    nvs_close(handle);                                    // 关闭NVS句柄
    return ret;                                           // 返回写入结果
}

/**
 * 函数名: nvs_write_u8
 * 功能: 将uint8_t值写入NVS并提交
 * 参数:
 *      key - NVS存储键名
 *      value - 要写入的uint8_t值
 * 返回值: ESP_OK表示写入成功，其他值表示写入失败
 * 作者: Lskipktw
 * 日期: 2026-05-09  09:45:06
 */
static esp_err_t nvs_write_u8(const char *key, uint8_t value)  // NVS写uint8_t函数定义
{
    nvs_handle_t handle;                                  // 声明NVS句柄变量
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);  // 以读写模式打开NVS命名空间
    if (ret != ESP_OK) return ret;                        // 打开失败则直接返回错误码

    ret = nvs_set_u8(handle, key, value);                 // 将uint8_t值写入NVS中指定键
    if (ret == ESP_OK) {                                  // 判断写入是否成功
        nvs_commit(handle);                               // 写入成功后提交更改到闪存
    }
    nvs_close(handle);                                    // 关闭NVS句柄
    return ret;                                           // 返回写入结果
}

/**
 * 函数名: nvs_read_u8
 * 功能: 从NVS中读取uint8_t值
 * 参数:
 *      key - NVS存储键名
 *      out - 输出uint8_t值指针
 * 返回值: ESP_OK表示读取成功，其他值表示读取失败（如键不存在）
 * 作者: Lskipktw
 * 日期: 2026-05-09  10:19:37
 */
static esp_err_t nvs_read_u8(const char *key, uint8_t *out)  // NVS读uint8_t函数定义
{
    nvs_handle_t handle;                                  // 声明NVS句柄变量
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);  // 以只读模式打开NVS命名空间
    if (ret != ESP_OK) return ret;                        // 打开失败则直接返回错误码

    ret = nvs_get_u8(handle, key, out);                   // 从NVS中读取指定键的uint8_t值到out指针
    nvs_close(handle);                                    // 关闭NVS句柄
    return ret;                                           // 返回读取结果
}

/**
 * 函数名: generate_default_id
 * 功能: 根据本机MAC地址后两字节生成默认设备ID，格式为"esp32-XXXX"
 * 参数:
 *      out - 输出设备ID字符串缓冲区指针
 *      size - 输出缓冲区大小
 * 返回值: 无
 * 作者: Lskipktw
 * 日期: 2026-05-09  10:52:19
 */
static void generate_default_id(char *out, size_t size)   // 生成默认设备ID函数定义
{
    snprintf(out, size, "esp32-%02X%02X", s_mac[4], s_mac[5]);  // 使用MAC地址第5、6字节（后两字节）生成"esp32-XXXX"格式ID
}

/* ── 发现响应处理（从设备端） ──────────────────────────────────── */

/**
 * 函数名: respond_discover
 * 功能: 从设备收到发现请求时，单播回复自己的设备ID、角色和MAC信息
 * 参数:
 *      requester_mac - 发起发现请求的主设备MAC地址（6字节）
 * 返回值: 无
 * 作者: Lskipktw
 * 日期: 2026-05-09  11:26:44
 */
static void respond_discover(const uint8_t *requester_mac)  // 响应发现请求函数定义
{
    /* Ensure requester is in the peer table before replying */
    esp_now_manager_ensure_peer(requester_mac);

    esp_now_discover_rsp_t rsp;                           // 声明发现响应结构体变量
    memset(&rsp, 0, sizeof(rsp));                         // 将响应结构体内存全部清零，防止残留数据
    rsp.proto = ESPNOW_PROTO_DISCOVER_RSP;                // 设置协议类型为"发现响应"
    rsp.role  = (uint8_t)s_role;                          // 填入本机设备角色（转换枚举为uint8_t）
    strncpy(rsp.device_id, s_device_id, sizeof(rsp.device_id) - 1);  // 安全复制本机设备ID到响应结构体（保留末尾\0空间）
    memcpy(rsp.mac, s_mac, 6);                            // 将本机MAC地址（6字节）复制到响应结构体

    char macbuf[18];                                      // 声明18字节MAC字符串缓冲区
    mac_str(requester_mac, macbuf, sizeof(macbuf));       // 将请求方MAC地址转为可读字符串
    ESP_LOGI(TAG, "Responding discovery to %s as id=%s role=%d",  // 输出INFO日志
             macbuf, s_device_id, (int)s_role);           // 记录目标MAC、本机ID和角色

    esp_now_manager_send(requester_mac, (const uint8_t *)&rsp, sizeof(rsp));  // 单播发送发现响应给请求方
}

/* ── 接收 handler（注册到 esp_now_manager） ────────────────────── */

/**
 * 函数名: device_recv_handler
 * 功能: 设备级ESP-NOW接收处理器，根据协议类型分发处理：发现请求/响应、命令等
 * 参数:
 *      src_addr - 发送方MAC地址（6字节）
 *      data - 接收到的数据缓冲区指针
 *      data_len - 接收到的数据长度（字节数）
 * 返回值: true表示已被本处理器处理，false表示未处理需交给默认处理器
 * 作者: Lskipktw
 * 日期: 2026-05-09  13:05:58
 */
static bool device_recv_handler(const uint8_t *src_addr,  // 设备接收处理器函数定义
                                const uint8_t *data, int data_len)  // 接收源地址、数据指针和数据长度
{
    if (data_len < 1) return false;                       // 数据长度不足1字节（无协议头），返回false交给默认处理器

    uint8_t proto = data[0];                              // 读取第一个字节作为协议类型标识

    switch (proto) {                                      // 根据协议类型进行分发处理

    case ESPNOW_PROTO_DISCOVER_REQ: {                     // 协议类型：发现请求（广播）
        /* 任何设备收到发现请求都回复 */
        if (s_discovering) {                              // 检查本机是否正在执行发现流程
            ESP_LOGD(TAG, "Ignoring discover req during own discovery");  // 正在发现中：忽略收到的请求，避免嵌套
        } else {                                          // 本机未在发现中
            respond_discover(src_addr);                   // 调用响应函数，单播回复自己的设备信息
        }
        return true;                                      // 返回true表示已处理此消息
    }

    case ESPNOW_PROTO_DISCOVER_RSP: {                     // 协议类型：发现响应（单播）
        /* 主设备收集响应 */
        if (!s_discovering || s_role != ESP_NOW_DEVICE_ROLE_MASTER) {  // 检查是否正在发现中且本机是主设备
            return true;  /* 忽略 */                     // 不在发现中或非主设备，忽略此响应
        }

        if (data_len < (int)sizeof(esp_now_discover_rsp_t)) {  // 检查数据长度是否小于发现响应结构体最小长度
            ESP_LOGW(TAG, "Discovery response too short: %d bytes", data_len);  // 数据太短：输出WARNING日志
            return true;                                  // 返回true，已处理（丢弃异常数据）
        }

        const esp_now_discover_rsp_t *rsp =               // 声明指向发现响应结构体的常量指针
            (const esp_now_discover_rsp_t *)data;         // 将原始数据指针强制转换为发现响应结构体指针

        /* 检查是否已存在 */
        for (int i = 0; i < s_slave_count; i++) {         // 遍历当前已知的从设备列表
            if (memcmp(s_slaves[i].mac, rsp->mac, 6) == 0) {  // 比较响应中的MAC与已知从设备MAC是否相同
                ESP_LOGI(TAG, "Discovery rsp from known slave %s (online=true)",  // 已知MAC
                         rsp->device_id);                 // 输出INFO日志记录已知设备ID
                s_slaves[i].online = true;                // 将该从设备标记为在线状态
                s_disco_rsp_received++;                   // 本次发现响应计数
                return true;                              // 返回true，已处理
            }
        }

        /* 添加到列表 */
        if (s_slave_count >= ESPNOW_MAX_SLAVES) {         // 检查从设备列表是否已满（>=8个）
            ESP_LOGW(TAG, "Slave list full, ignoring %s", rsp->device_id);  // 列表已满：输出WARNING日志，忽略新设备
            return true;                                  // 返回true，已处理（丢弃）
        }

        strncpy(s_slaves[s_slave_count].device_id, rsp->device_id,  // 将响应中的设备ID安全复制到从设备列表
                sizeof(s_slaves[s_slave_count].device_id) - 1);  // 保留末尾一个字节给\0终止符
        memcpy(s_slaves[s_slave_count].mac, rsp->mac, 6);  // 将响应中的MAC地址复制到从设备列表
        s_slaves[s_slave_count].online = true;            // 标记该从设备为在线状态

        char macbuf[18];                                  // 声明MAC字符串缓冲区
        mac_str(rsp->mac, macbuf, sizeof(macbuf));        // 将响应中的MAC地址转为可读字符串
        ESP_LOGI(TAG, "Discovered slave #%d: id=%s, mac=%s",  // 输出INFO日志
                 s_slave_count, rsp->device_id, macbuf);  // 记录从设备序号、ID和MAC

        /* 添加为单播 peer */
        esp_now_manager_add_peer(rsp->mac);               // 将该从设备MAC添加为ESP-NOW单播对等节点

        s_slave_count++;                                  // 从设备计数加1
        s_disco_rsp_received++;                           // 本次发现响应计数加1
        return true;                                      // 返回true，已处理
    }

    case ESPNOW_PROTO_CMD: {                              // 协议类型：主设备命令（单播）
        /* 从设备收到主设备命令 */
        if (data_len < (int)sizeof(esp_now_cmd_header_t)) {  // 检查数据长度是否小于命令头部结构体最小长度
            ESP_LOGW(TAG, "Command header too short: %d bytes", data_len);  // 数据太短：输出WARNING
            return true;                                  // 返回true，已处理（丢弃异常数据）
        }

        const esp_now_cmd_header_t *cmd = (const esp_now_cmd_header_t *)data;  // 将数据指针转换为命令头部结构体指针

        /* 检查目标是否为自己 */
        if (strncmp(cmd->target_device_id, s_device_id,   // 比较命令中的目标设备ID与本机ID
                    ESPNOW_DEVICE_ID_MAX_LEN) != 0) {     // 如果不匹配（非本机）
            ESP_LOGD(TAG, "Command for %s, not for me (%s), ignoring",  // 输出DEBUG日志
                     cmd->target_device_id, s_device_id); // 记录命令目标ID和本机ID
            return true;                                  // 返回true，已处理（忽略非本机命令）
        }

        /* JSON 命令跟在 header 后面 */
        int json_len = data_len - sizeof(esp_now_cmd_header_t);  // 计算JSON负载长度 = 总长度 - 命令头部长度
        const char *json = (const char *)(data + sizeof(esp_now_cmd_header_t));  // 获取JSON字符串起始指针（跳过命令头部）

        char macbuf[18];                                  // 声明MAC字符串缓冲区
        mac_str(src_addr, macbuf, sizeof(macbuf));        // 将发送方MAC地址转为可读字符串
        ESP_LOGI(TAG, "Received command from master %s (%d bytes JSON): %.*s",  // 输出INFO日志
                 macbuf, json_len, json_len, json);       // 记录发送方MAC、JSON长度和JSON内容

        /* 如果是从机角色，交由从机执行器处理 */
        if (s_role == ESP_NOW_DEVICE_ROLE_SLAVE) {
            slave_executor_handle(src_addr, json, json_len);
        } else {
            ESP_LOGI(TAG, "Master received CMD (not a slave), ignoring");
        }
        return true;                                      // 返回true，命令已处理
    }

    case ESPNOW_PROTO_CMD_RSP: {                          // 协议类型：从机命令响应（单播）
        /* 主设备收到从机响应 */
        if (data_len < (int)sizeof(esp_now_cmd_rsp_header_t)) {
            ESP_LOGW(TAG, "CMD_RSP header too short: %d bytes", data_len);
            return true;
        }

        const esp_now_cmd_rsp_header_t *rsp =
            (const esp_now_cmd_rsp_header_t *)data;
        int json_len = data_len - sizeof(esp_now_cmd_rsp_header_t);
        const char *json = (const char *)(data + sizeof(esp_now_cmd_rsp_header_t));

        char macbuf[18];
        mac_str(src_addr, macbuf, sizeof(macbuf));
        ESP_LOGI(TAG, "Response from slave %s (MAC=%s): %.*s",
                 rsp->source_device_id, macbuf,
                 json_len > 0 ? json_len : 0,
                 json_len > 0 ? json : "");
        /* TODO: route response to agent loop for tool_result correlation */
        return true;
    }

    default:                                              // 未知协议类型或其他普通消息
        /* 普通消息或其他未知协议，交给默认 handler */
        return false;                                     // 返回false，交由esp_now_manager的默认处理器处理
    }
}

/* ── 公开 API ──────────────────────────────────────────────────── */

/**
 * 函数名: esp_now_device_init
 * 功能: 初始化ESP-NOW设备层，包括获取MAC地址、确定设备ID和角色（优先级：NVS > 宏 > 默认），并注册接收处理器
 * 参数: 无
 * 返回值: ESP_OK表示初始化成功，其他值表示失败
 * 作者: Lskipktw
 * 日期: 2026-05-09  13:48:21
 */
esp_err_t esp_now_device_init(void)                       // 设备层初始化函数定义
{
    /* 1. 获取本机 MAC */
    esp_err_t ret = esp_read_mac(s_mac, ESP_MAC_WIFI_STA);  // 读取WiFi STA模式下的本机MAC地址到s_mac缓冲区
    if (ret != ESP_OK) {                                  // 判断读取MAC是否失败
        ESP_LOGE(TAG, "Failed to read MAC: %s", esp_err_to_name(ret));  // 读取失败：输出ERROR日志
        return ret;                                       // 返回错误码
    }
    char macbuf[18];                                      // 声明18字节MAC字符串缓冲区
    mac_str(s_mac, macbuf, sizeof(macbuf));               // 将本机MAC转为可读字符串
    ESP_LOGI(TAG, "Local MAC: %s", macbuf);               // 输出INFO日志记录本机MAC地址

    /* 2. 确定设备 ID（优先级：NVS > 宏 > 默认） */
    char nvs_id[ESPNOW_DEVICE_ID_MAX_LEN] = {0};          // 声明NVS读取缓冲区并初始化为全零
    bool nvs_has_id = (nvs_read_str(NVS_KEY_ID, nvs_id, sizeof(nvs_id)) == ESP_OK  // 尝试从NVS读取设备ID
                       && nvs_id[0] != '\0');             // 且读取到的字符串非空

    const char *macro_id = MIMI_SECRET_DEVICE_ID;         // 获取编译期宏定义的设备ID
    bool macro_has_id = (macro_id[0] != '\0');            // 检查宏定义ID是否非空

    if (nvs_has_id) {                                     // 优先级1：NVS中存在有效设备ID
        strncpy(s_device_id, nvs_id, sizeof(s_device_id) - 1);  // 使用NVS中的ID覆盖本机设备ID
        ESP_LOGI(TAG, "Device ID from NVS: %s", s_device_id);  // 输出INFO日志：ID来源为NVS
    } else if (macro_has_id) {                            // 优先级2：宏定义中存在有效设备ID
        strncpy(s_device_id, macro_id, sizeof(s_device_id) - 1);  // 使用宏定义的ID覆盖本机设备ID
        ESP_LOGI(TAG, "Device ID from macro: %s", s_device_id);  // 输出INFO日志：ID来源为编译宏
        /* 写回 NVS 作为持久化 */
        nvs_write_str(NVS_KEY_ID, s_device_id);           // 将宏定义的ID写入NVS，实现持久化
    } else {                                              // 优先级3：NVS和宏都为空，使用默认生成
        generate_default_id(s_device_id, sizeof(s_device_id));  // 基于MAC地址后两字节生成默认设备ID
        ESP_LOGI(TAG, "Device ID generated from MAC: %s", s_device_id);  // 输出INFO日志：ID来源为MAC生成
        nvs_write_str(NVS_KEY_ID, s_device_id);           // 将生成的默认ID写入NVS持久化
    }

    /* 3. 确定角色（优先级：NVS > 宏 > 默认 SLAVE） */
    uint8_t nvs_role_val;                                 // 声明用于接收NVS角色值的变量
    bool nvs_has_role = (nvs_read_u8(NVS_KEY_ROLE, &nvs_role_val) == ESP_OK);  // 尝试从NVS读取设备角色

    const char *macro_role = MIMI_SECRET_DEVICE_ROLE;     // 获取编译期宏定义的设备角色字符串
    // （注意：此处没有检查macro_role[0] != '\0'，下面用else if处理）

    if (nvs_has_role) {                                   // 优先级1：NVS中存在有效的角色值
        s_role = (esp_now_device_role_t)nvs_role_val;     // 将NVS读取值强制转换为设备角色枚举并设置
        ESP_LOGI(TAG, "Device role from NVS: %d", (int)s_role);  // 输出INFO日志：角色来源为NVS
    } else if (macro_role[0] != '\0') {                   // 优先级2：宏定义中角色字符串非空
        if (strcmp(macro_role, "master") == 0) {          // 判断宏定义字符串是否为"master"
            s_role = ESP_NOW_DEVICE_ROLE_MASTER;          // 设置角色为主设备
        } else {                                          // 宏定义为其他值（如"slave"或其他）
            s_role = ESP_NOW_DEVICE_ROLE_SLAVE;           // 设置角色为从设备
        }
        ESP_LOGI(TAG, "Device role from macro: %s → %d", macro_role, (int)s_role);  // 输出INFO日志：角色来源为宏
        nvs_write_u8(NVS_KEY_ROLE, (uint8_t)s_role);      // 将角色写入NVS持久化
    } else {                                              // 优先级3：NVS和宏都无有效角色
        s_role = ESP_NOW_DEVICE_ROLE_SLAVE;               // 使用默认角色：从设备
        ESP_LOGI(TAG, "Device role default: SLAVE");      // 输出INFO日志：使用默认SLAVE角色
        nvs_write_u8(NVS_KEY_ROLE, (uint8_t)s_role);      // 将默认角色写入NVS持久化
    }

    /* 4. 注册接收 handler */
    esp_now_manager_register_recv_handler(device_recv_handler);  // 将设备层接收处理器注册到ESP-NOW管理器

    ESP_LOGI(TAG, "Device initialized: id=%s, role=%s",   // 输出INFO日志：设备初始化完成
             s_device_id,                                 // 打印设备ID
             s_role == ESP_NOW_DEVICE_ROLE_MASTER ? "MASTER" : "SLAVE");  // 打印角色名称
    return ESP_OK;                                        // 返回ESP_OK表示初始化成功
}

/**
 * 函数名: esp_now_device_get_id
 * 功能: 获取本设备的ID字符串
 * 参数: 无
 * 返回值: 设备ID字符串指针（静态缓冲区，不可释放）
 * 作者: Lskipktw
 * 日期: 2026-05-09  14:14:39
 */
const char *esp_now_device_get_id(void)                   // 获取设备ID函数定义
{
    return s_device_id;                                   // 返回静态设备ID缓冲区的指针
}

/**
 * 函数名: esp_now_device_get_role
 * 功能: 获取本设备的角色（主设备或从设备）
 * 参数: 无
 * 返回值: 设备角色枚举值
 * 作者: Lskipktw
 * 日期: 2026-05-09  14:37:52
 */
esp_now_device_role_t esp_now_device_get_role(void)       // 获取设备角色函数定义
{
    return s_role;                                        // 返回静态角色变量的当前值
}

/**
 * 函数名: esp_now_device_get_mac
 * 功能: 获取本设备的MAC地址
 * 参数: 无
 * 返回值: MAC地址缓冲区指针（6字节，静态缓冲区，不可释放）
 * 作者: Lskipktw
 * 日期: 2026-05-09  15:03:14
 */
const uint8_t *esp_now_device_get_mac(void)               // 获取MAC地址函数定义
{
    return s_mac;                                         // 返回静态MAC地址缓冲区的指针
}

/**
 * 函数名: esp_now_device_set_id
 * 功能: 设置设备ID并持久化到NVS，新ID在下次重启后生效
 * 参数:
 *      device_id - 新的设备ID字符串
 * 返回值: ESP_OK表示设置成功，ESP_ERR_INVALID_ARG表示参数无效
 * 作者: Lskipktw
 * 日期: 2026-05-09  15:25:47
 */
esp_err_t esp_now_device_set_id(const char *device_id)    // 设置设备ID函数定义
{
    if (device_id == NULL || device_id[0] == '\0') {      // 参数校验：指针为空或字符串为空
        return ESP_ERR_INVALID_ARG;                       // 返回"无效参数"错误码
    }
    strncpy(s_device_id, device_id, sizeof(s_device_id) - 1);  // 安全复制新ID到静态缓冲区（保留\0空间）
    ESP_LOGI(TAG, "Device ID set to: %s (takes effect after reboot)", s_device_id);  // 输出INFO日志，提示新ID将在重启后生效
    return nvs_write_str(NVS_KEY_ID, device_id);          // 将新ID写入NVS持久化，并返回写入结果
}

/**
 * 函数名: esp_now_device_set_role
 * 功能: 设置设备角色并持久化到NVS，新角色在下次重启后生效
 * 参数:
 *      role - 设备角色枚举值（主设备或从设备）
 * 返回值: ESP_OK表示设置成功
 * 作者: Lskipktw
 * 日期: 2026-05-09  16:08:33
 */
esp_err_t esp_now_device_set_role(esp_now_device_role_t role)  // 设置设备角色函数定义
{
    s_role = role;                                        // 更新静态角色变量
    ESP_LOGI(TAG, "Device role set to: %d (takes effect after reboot)", (int)role);  // 输出INFO日志，提示新角色将在重启后生效
    return nvs_write_u8(NVS_KEY_ROLE, (uint8_t)role);    // 将新角色写入NVS持久化，并返回写入结果
}

/**
 * 函数名: esp_now_device_discover_peers
 * 功能: 主设备广播发现请求并等待从设备响应，收集在线从设备列表
 * 参数:
 *      timeout_ms - 等待响应的超时时间（毫秒）
 * 返回值: >=0表示发现的从设备总数，-1表示失败（未就绪或非主设备）
 * 作者: Lskipktw
 * 日期: 2026-05-09  16:41:59
 */
int esp_now_device_discover_peers(uint32_t timeout_ms)    // 发现对等设备函数定义
{
    if (!esp_now_manager_is_ready()) {                    // 检查ESP-NOW管理器是否已初始化就绪
        ESP_LOGE(TAG, "ESP-NOW not ready");               // 未就绪：输出ERROR日志
        return -1;                                        // 返回-1表示失败
    }

    if (s_role != ESP_NOW_DEVICE_ROLE_MASTER) {           // 检查本机是否为主设备角色
        ESP_LOGW(TAG, "Only master can discover peers");  // 非主设备：输出WARNING日志
        return -1;                                        // 返回-1表示失败（只有主设备才能发起发现）
    }

    /* 重置状态 */
    s_discovering = true;                                 // 设置发现进行中标志为true
    s_disco_rsp_received = 0;                             // 重置本次发现的响应计数为0

    /* 标记所有已知从设备为离线 */
    for (int i = 0; i < s_slave_count; i++) {             // 遍历所有已知从设备
        s_slaves[i].online = false;                       // 将每个从设备标记为离线状态
    }

    /* 广播发现请求 */
    uint8_t req = ESPNOW_PROTO_DISCOVER_REQ;              // 构建发现请求：单字节协议标识
    ESP_LOGI(TAG, "Broadcasting discovery request...");   // 输出INFO日志：开始广播发现
    esp_err_t ret = esp_now_manager_broadcast(&req, sizeof(req));  // 通过ESP-NOW广播发现请求（1字节）
    if (ret != ESP_OK) {                                  // 判断广播是否失败
        ESP_LOGE(TAG, "Broadcast discovery failed: %s", esp_err_to_name(ret));  // 广播失败：输出ERROR日志
        s_discovering = false;                            // 重置发现标志
        return -1;                                        // 返回-1表示失败
    }

    /* 等待收集响应 */
    uint32_t waited = 0;                                  // 已等待时间计数，初始0毫秒
    uint32_t tick = 100;  /* 每 100ms 检查一次 */        // 定义轮询间隔为100毫秒
    while (waited < timeout_ms) {                         // 循环等待直到超时
        vTaskDelay(pdMS_TO_TICKS(tick));                  // FreeRTOS延时100毫秒，让出CPU
        waited += tick;                                   // 累计已等待时间

        /* 期间可能还会收到更多响应，由回调处理 */
    }

    s_discovering = false;                                // 超时后清除发现进行中标志

    ESP_LOGI(TAG, "Discovery complete: %d responses, %d total known slaves",  // 输出INFO日志：发现完成
             s_disco_rsp_received, s_slave_count);        // 记录本次响应数和已知从设备总数

    return s_slave_count;                                 // 返回当前已知从设备总数
}

/**
 * 函数名: esp_now_device_get_slaves
 * 功能: 获取已发现的从设备列表副本
 * 参数:
 *      out_slaves - 输出从设备信息数组缓冲区指针
 *      max_count - 输出数组最大容量
 * 返回值: 已知从设备总数（可能大于max_count）；若out_slaves为NULL或max_count<=0，仅返回总数不拷贝
 * 作者: Lskipktw
 * 日期: 2026-05-09  17:12:25
 */
int esp_now_device_get_slaves(esp_now_slave_info_t *out_slaves, int max_count)  // 获取从设备列表函数定义
{
    if (out_slaves == NULL || max_count <= 0) return s_slave_count;  // 参数无效或仅查询总数：直接返回总数，不拷贝数据

    int n = (s_slave_count < max_count) ? s_slave_count : max_count;  // 计算实际可拷贝数量 = min(已知总数, 缓冲区容量)
    memcpy(out_slaves, s_slaves, n * sizeof(esp_now_slave_info_t));  // 将从设备列表数据拷贝到用户提供的缓冲区
    return n;                                             // 返回实际拷贝的从设备数量
}

/**
 * 函数名: esp_now_device_send_command
 * 功能: 主设备向指定从设备发送JSON格式的命令
 * 参数:
 *      target_device_id - 目标从设备的ID字符串
 *      command_json - 要发送的JSON命令字符串
 * 返回值: ESP_OK表示发送成功，ESP_ERR_INVALID_STATE表示未就绪或非主设备，ESP_ERR_INVALID_ARG表示参数无效，ESP_ERR_NOT_FOUND表示目标设备未找到
 * 作者: Lskipktw
 * 日期: 2026-05-09  17:54:08
 */
esp_err_t esp_now_device_send_command(const char *target_device_id,  // 发送命令函数定义
                                      const char *command_json)     // 目标设备ID和JSON命令字符串
{
    if (!esp_now_manager_is_ready()) {                    // 检查ESP-NOW管理器是否已初始化
        ESP_LOGE(TAG, "ESP-NOW not ready");               // 未就绪：输出ERROR日志
        return ESP_ERR_INVALID_STATE;                     // 返回"无效状态"错误码
    }

    if (s_role != ESP_NOW_DEVICE_ROLE_MASTER) {           // 检查本机是否为主设备
        ESP_LOGW(TAG, "Only master can send commands");   // 非主设备：输出WARNING日志
        return ESP_ERR_INVALID_STATE;                     // 返回"无效状态"错误码（仅主设备可发命令）
    }

    if (target_device_id == NULL || command_json == NULL) {  // 参数校验：目标ID或JSON命令为空
        return ESP_ERR_INVALID_ARG;                       // 返回"无效参数"错误码
    }

    /* 查找目标从设备的 MAC */
    uint8_t *target_mac = NULL;                           // 目标MAC指针，初始为NULL表示未找到
    for (int i = 0; i < s_slave_count; i++) {             // 遍历所有已知从设备
        if (strcmp(s_slaves[i].device_id, target_device_id) == 0) {  // 比较从设备ID是否与目标ID匹配
            target_mac = s_slaves[i].mac;                 // 匹配成功：获取该从设备的MAC地址指针
            break;                                        // 找到目标，跳出循环
        }
    }

    if (target_mac == NULL) {                             // 检查是否找到目标从设备
        ESP_LOGE(TAG, "Slave '%s' not found", target_device_id);  // 未找到：输出ERROR日志
        return ESP_ERR_NOT_FOUND;                         // 返回"未找到"错误码
    }

    /* 构造命令包 */
    int json_len = strlen(command_json);                  // 计算JSON命令字符串的长度（不含\0）
    int total_len = sizeof(esp_now_cmd_header_t) + json_len;  // 计算总包长度 = 命令头大小 + JSON长度

    uint8_t *packet = malloc(total_len);                  // 在堆上分配命令包内存
    if (packet == NULL) {                                 // 检查内存分配是否失败
        ESP_LOGE(TAG, "Failed to allocate command packet (%d bytes)", total_len);  // 分配失败：输出ERROR日志
        return ESP_ERR_NO_MEM;                            // 返回"内存不足"错误码
    }

    esp_now_cmd_header_t *header = (esp_now_cmd_header_t *)packet;  // 将包内存起始地址转换为命令头部指针
    memset(header, 0, sizeof(*header));                   // 将命令头部内存清零
    header->proto = ESPNOW_PROTO_CMD;                     // 设置协议类型为"命令"
    strncpy(header->target_device_id, target_device_id,   // 将目标设备ID安全复制到命令头部
            sizeof(header->target_device_id) - 1);        // 保留末尾\0空间

    memcpy(packet + sizeof(esp_now_cmd_header_t), command_json, json_len);  // 将JSON命令字符串复制到头部之后的内存区域

    char macbuf[18];                                      // 声明MAC字符串缓冲区
    mac_str(target_mac, macbuf, sizeof(macbuf));          // 将目标MAC地址转为可读字符串
    ESP_LOGI(TAG, "Sending command to %s (mac=%s, %d bytes)",  // 输出INFO日志
             target_device_id, macbuf, total_len);        // 记录目标ID、MAC和总包长

    esp_err_t ret = esp_now_manager_send(target_mac, packet, total_len);  // 通过ESP-NOW单播发送命令包
    free(packet);                                         // 释放堆上分配的命令包内存

    if (ret != ESP_OK) {                                  // 判断发送是否失败
        ESP_LOGE(TAG, "Send command failed: %s", esp_err_to_name(ret));  // 发送失败：输出ERROR日志
    }
    return ret;                                           // 返回发送结果
}

/**
 * 函数名: esp_now_device_is_master
 * 功能: 判断本设备是否为主设备角色
 * 参数: 无
 * 返回值: true表示主设备，false表示从设备
 * 作者: Lskipktw
 * 日期: 2026-05-09  18:22:41
 */
bool esp_now_device_is_master(void)                       // 判断是否主设备函数定义
{
    return s_role == ESP_NOW_DEVICE_ROLE_MASTER;          // 比较当前角色枚举值是否等于主设备
}
