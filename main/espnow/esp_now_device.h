#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 设备角色 ──────────────────────────────────────────────────── */
typedef enum {
    ESP_NOW_DEVICE_ROLE_SLAVE  = 0,
    ESP_NOW_DEVICE_ROLE_MASTER = 1,
} esp_now_device_role_t;

/* ── ESP-NOW 协议类型（1 byte header） ─────────────────────────── */
#define ESPNOW_PROTO_MSG           0x01  /* 普通消息（现有逻辑） */
#define ESPNOW_PROTO_DISCOVER_REQ  0x02  /* 设备发现请求（广播） */
#define ESPNOW_PROTO_DISCOVER_RSP  0x03  /* 设备发现响应（单播） */
#define ESPNOW_PROTO_CMD           0x04  /* 主设备命令（单播） */
#define ESPNOW_PROTO_CMD_RSP       0x05  /* 从机命令响应（单播） */

/* 发现响应 payload 结构 */
#define ESPNOW_DEVICE_ID_MAX_LEN    32
#define ESPNOW_MAX_SLAVES           8

typedef struct {
    uint8_t proto;                                /* 协议类型 */
    char    device_id[ESPNOW_DEVICE_ID_MAX_LEN];  /* 设备编号 */
    uint8_t role;                                 /* esp_now_device_role_t */
    uint8_t mac[6];                               /* 源 MAC */
} __attribute__((packed)) esp_now_discover_rsp_t;

/* 主设备命令 payload */
typedef struct {
    uint8_t proto;                                /* 协议类型 = 0x04 */
    char    target_device_id[ESPNOW_DEVICE_ID_MAX_LEN];
    /* 后面跟 JSON 命令字符串 */
} __attribute__((packed)) esp_now_cmd_header_t;

/* 从机命令响应 payload */
typedef struct {
    uint8_t proto;                                     /* 协议类型 = 0x05 */
    char    source_device_id[ESPNOW_DEVICE_ID_MAX_LEN]; /* 从机的 device ID */
    /* 后面跟 JSON 响应字符串: {"call_id":"...","error":0,"result":"..."} */
} __attribute__((packed)) esp_now_cmd_rsp_header_t;

/* ── 从设备信息（主设备内存列表项） ────────────────────────────── */
typedef struct {
    char    device_id[ESPNOW_DEVICE_ID_MAX_LEN];
    uint8_t mac[6];
    bool    online;
} esp_now_slave_info_t;

/* ── API ───────────────────────────────────────────────────────── */

/**
 * 函数名: esp_now_device_init
 * 功能: 初始化ESP-NOW设备层：获取MAC地址、确定设备ID和角色（优先级：NVS > 宏 > 默认），并注册接收处理器，结果写回NVS持久化
 * 参数: 无
 * 返回值: ESP_OK表示初始化成功，其他值表示失败
 * 作者: Lskipktw
 * 日期: 2026-05-09  08:12:37
 */
esp_err_t esp_now_device_init(void);

/**
 * 函数名: esp_now_device_get_id
 * 功能: 获取本设备的ID字符串
 * 参数: 无
 * 返回值: 设备ID字符串指针（静态缓冲区，不可释放）
 * 作者: Lskipktw
 * 日期: 2026-05-09  09:24:51
 */
const char *esp_now_device_get_id(void);

/**
 * 函数名: esp_now_device_get_role
 * 功能: 获取本设备的角色（主设备或从设备）
 * 参数: 无
 * 返回值: 设备角色枚举值
 * 作者: Lskipktw
 * 日期: 2026-05-09  10:36:18
 */
esp_now_device_role_t esp_now_device_get_role(void);

/**
 * 函数名: esp_now_device_get_mac
 * 功能: 获取本设备的MAC地址
 * 参数: 无
 * 返回值: MAC地址缓冲区指针（6字节，静态缓冲区，不可释放）
 * 作者: Lskipktw
 * 日期: 2026-05-09  11:47:29
 */
const uint8_t *esp_now_device_get_mac(void);

/**
 * 函数名: esp_now_device_set_id
 * 功能: 设置设备ID并持久化到NVS，新ID在下次重启后生效
 * 参数:
 *      device_id - 新的设备ID字符串
 * 返回值: ESP_OK表示设置成功，ESP_ERR_INVALID_ARG表示参数无效
 * 作者: Lskipktw
 * 日期: 2026-05-09  13:58:42
 */
esp_err_t esp_now_device_set_id(const char *device_id);

/**
 * 函数名: esp_now_device_set_role
 * 功能: 设置设备角色并持久化到NVS，新角色在下次重启后生效
 * 参数:
 *      role - 设备角色枚举值（主设备或从设备）
 * 返回值: ESP_OK表示设置成功
 * 作者: Lskipktw
 * 日期: 2026-05-09  14:09:53
 */
esp_err_t esp_now_device_set_role(esp_now_device_role_t role);

/**
 * 函数名: esp_now_device_discover_peers
 * 功能: 主设备广播发现请求并等待从设备响应，收集在线从设备列表，完成后自动添加peer
 * 参数:
 *      timeout_ms - 等待响应的超时时间（毫秒）
 * 返回值: >=0表示发现的从设备总数，-1表示失败
 * 作者: Lskipktw
 * 日期: 2026-05-09  15:21:06
 */
int esp_now_device_discover_peers(uint32_t timeout_ms);

/**
 * 函数名: esp_now_device_get_slaves
 * 功能: 获取已发现的从设备列表副本
 * 参数:
 *      out_slaves - 输出从设备信息数组缓冲区指针
 *      max_count - 输出数组最大容量
 * 返回值: 已知从设备总数（可能大于max_count）；若out_slaves为NULL或max_count<=0，仅返回总数不拷贝
 * 作者: Lskipktw
 * 日期: 2026-05-09  16:33:19
 */
int esp_now_device_get_slaves(esp_now_slave_info_t *out_slaves, int max_count);

/**
 * 函数名: esp_now_device_send_command
 * 功能: 主设备向指定从设备发送JSON格式的命令
 * 参数:
 *      target_device_id - 目标从设备的ID字符串
 *      command_json - 要发送的JSON命令字符串
 * 返回值: ESP_OK表示发送成功，ESP_ERR_INVALID_STATE表示未就绪或非主设备，ESP_ERR_INVALID_ARG表示参数无效，ESP_ERR_NOT_FOUND表示目标设备未找到
 * 作者: Lskipktw
 * 日期: 2026-05-09  17:44:32
 */
esp_err_t esp_now_device_send_command(const char *target_device_id,
                                      const char *command_json);

/**
 * 函数名: esp_now_device_is_master
 * 功能: 判断本设备是否为主设备角色
 * 参数: 无
 * 返回值: true表示主设备，false表示从设备
 * 作者: Lskipktw
 * 日期: 2026-05-09  18:55:45
 */
bool esp_now_device_is_master(void);

#ifdef __cplusplus
}
#endif
