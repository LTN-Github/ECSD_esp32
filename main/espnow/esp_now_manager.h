#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ESP-NOW 广播地址（全 FF 表示广播给所有设备） */
#define ESPNOW_BROADCAST_MAC_LEN 6

/**
 * 回调类型: esp_now_recv_handler_t
 * 功能: ESP-NOW接收回调函数指针类型，返回true表示已处理，不再走默认逻辑
 * 参数:
 *      src_addr - 发送方MAC地址
 *      data - 接收到的数据指针
 *      data_len - 数据长度
 * 返回值: true表示已处理，false表示交由默认处理器
 * 作者: Lskipktw
 * 日期: 2026-05-09  07:25:36
 */
typedef bool (*esp_now_recv_handler_t)(const uint8_t *src_addr,
                                       const uint8_t *data, int data_len);

/**
 * 函数名: esp_now_manager_init
 * 功能: 初始化ESP-NOW管理器，注册发送和接收回调，并添加广播对等节点
 * 参数: 无
 * 返回值: ESP_OK表示初始化成功，其他值表示失败
 * 作者: Lskipktw
 * 日期: 2026-05-09  08:41:12
 */
esp_err_t esp_now_manager_init(void);

/**
 * 函数名: esp_now_manager_broadcast
 * 功能: 通过ESP-NOW广播数据到所有设备
 * 参数:
 *      data - 要发送的数据缓冲区指针
 *      len - 数据长度（字节数）
 * 返回值: ESP_OK表示发送成功，ESP_ERR_INVALID_STATE表示未初始化，ESP_ERR_INVALID_ARG表示参数无效
 * 作者: Lskipktw
 * 日期: 2026-05-09  09:55:28
 */
esp_err_t esp_now_manager_broadcast(const uint8_t *data, size_t len);

/**
 * 函数名: esp_now_manager_send
 * 功能: 通过ESP-NOW向指定对等节点单播发送数据
 * 参数:
 *      peer_addr - 目标对等节点的MAC地址（6字节）
 *      data - 要发送的数据缓冲区指针
 *      len - 数据长度（字节数）
 * 返回值: ESP_OK表示发送成功，ESP_ERR_INVALID_STATE表示未初始化，ESP_ERR_INVALID_ARG表示参数无效
 * 作者: Lskipktw
 * 日期: 2026-05-09  11:08:43
 */
esp_err_t esp_now_manager_send(const uint8_t *peer_addr, const uint8_t *data, size_t len);

/**
 * 函数名: esp_now_manager_add_peer
 * 功能: 向ESP-NOW对等列表中添加一个对等节点，若已存在则静默返回成功
 * 参数:
 *      peer_addr - 要添加的对等节点的MAC地址（6字节）
 * 返回值: ESP_OK表示添加成功或已存在，其他值表示添加失败
 * 作者: Lskipktw
 * 日期: 2026-05-09  13:21:57
 */
esp_err_t esp_now_manager_add_peer(const uint8_t *peer_addr);

/**
 * 函数名: esp_now_manager_register_recv_handler
 * 功能: 注册外部接收处理器，当收到ESP-NOW数据时优先调用此处理器
 * 参数:
 *      handler - 接收处理回调函数指针，传入NULL可取消注册
 * 返回值: 无
 * 作者: Lskipktw
 * 日期: 2026-05-09  14:37:15
 */
void esp_now_manager_register_recv_handler(esp_now_recv_handler_t handler);

/**
 * 函数名: esp_now_manager_mac_to_str
 * 功能: 将6字节MAC地址转换为"XX:XX:XX:XX:XX:XX"格式的字符串
 * 参数:
 *      mac - 源MAC地址缓冲区指针（6字节）
 *      out_str - 输出字符串缓冲区指针
 *      size - 输出缓冲区大小（需>=18字节）
 * 返回值: 无
 * 作者: Lskipktw
 * 日期: 2026-05-09  15:49:31
 */
void esp_now_manager_mac_to_str(const uint8_t *mac, char *out_str, size_t size);

/**
 * 函数名: esp_now_manager_is_ready
 * 功能: 查询ESP-NOW管理器是否已完成初始化
 * 参数: 无
 * 返回值: true表示已初始化就绪，false表示未初始化
 * 作者: Lskipktw
 * 日期: 2026-05-09  17:03:44
 */
bool esp_now_manager_is_ready(void);

#ifdef __cplusplus
}
#endif
