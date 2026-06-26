#include "esp_now_manager.h"                              // 包含ESP-NOW管理器头文件，引入函数声明和类型定义

#include "esp_log.h"                                      // 包含ESP-IDF日志库，提供ESP_LOGI/ESP_LOGW/ESP_LOGE等宏
#include "esp_wifi.h"                                     // 包含ESP-IDF WiFi库，提供wifi_tx_info_t等WiFi相关类型
#include "esp_now.h"                                      // 包含ESP-NOW协议库，提供esp_now_init/send/recv等核心API
#include <string.h>                                       // 包含C标准库字符串操作函数（memcpy等）

static const char *TAG = "espnow";                        // 定义模块日志标签常量，用于ESP_LOGx宏中标识日志来源
static bool s_ready = false;                              // 模块初始化就绪标志，true表示ESP-NOW已初始化完成

/* 广播地址，全 FF */
static const uint8_t s_broadcast_mac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};  // 定义广播MAC地址常量，6字节全0xFF表示向所有设备广播

/* 外部接收处理器 */
static esp_now_recv_handler_t s_recv_handler = NULL;     // 外部接收处理器函数指针，初始为NULL；由上层模块（如esp_now_device）注册

/**
 * 函数名: on_send
 * 功能: ESP-NOW数据发送完成回调函数，根据发送状态打印成功或失败日志
 * 参数:
 *      tx_info - WiFi发送信息结构体，包含目标MAC地址等
 *      status - 发送状态，ESP_NOW_SEND_SUCCESS表示成功，其他表示失败
 * 返回值: 无
 * 作者: Lskipktw
 * 日期: 2026-05-09  08:15:22
 */
static void on_send(const wifi_tx_info_t *tx_info, esp_now_send_status_t status)  // 发送完成回调函数定义，由ESP-NOW底层在发送完成后自动调用
{
    const uint8_t *mac_addr = tx_info->des_addr;          // 从发送信息结构体获取目标MAC地址指针
    char mac_str[18];                                     // 声明18字节的MAC地址字符串缓冲区（格式：XX:XX:XX:XX:XX:XX\0）
    esp_now_manager_mac_to_str(mac_addr, mac_str, sizeof(mac_str));  // 调用MAC转字符串函数，将目标MAC格式化为可读字符串

    if (status == ESP_NOW_SEND_SUCCESS) {                 // 判断发送状态是否为成功
        ESP_LOGD(TAG, "Send to %s success", mac_str);     // 发送成功：输出DEBUG级别日志，记录目标MAC地址
    } else {                                              // 发送状态为失败
        ESP_LOGW(TAG, "Send to %s failed", mac_str);      // 发送失败：输出WARNING级别日志，记录目标MAC地址
    }
}

/**
 * 函数名: on_recv
 * 功能: ESP-NOW数据接收回调函数，优先交给外部handler处理，否则执行默认打印逻辑
 * 参数:
 *      info - 接收信息结构体，包含源MAC地址等
 *      data - 接收到的数据缓冲区指针
 *      data_len - 接收到的数据长度（字节数）
 * 返回值: 无
 * 作者: Lskipktw
 * 日期: 2026-05-09  09:27:41
 */
static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int data_len)  // 接收回调函数定义，由ESP-NOW底层在收到数据时自动调用
{
    char mac_str[18];                                     // 声明18字节的MAC地址字符串缓冲区
    esp_now_manager_mac_to_str(info->src_addr, mac_str, sizeof(mac_str));  // 将发送方源MAC地址格式化为可读字符串

    /* 先交给外部 handler 处理（如 esp_now_device） */
    if (s_recv_handler && s_recv_handler(info->src_addr, data, data_len)) {  // 判断外部处理器是否存在，并尝试调用它处理数据
        return;  /* 外部已处理 */                       // 外部处理器返回true表示已处理，直接返回不再执行默认逻辑
    }

    /* 默认处理 */
    ESP_LOGI(TAG, "Recv %d bytes from %s", data_len, mac_str);  // 输出INFO级别日志，打印接收到的数据字节数和来源MAC

    if (data_len > 0 && data[data_len - 1] == '\0') {   // 判断数据长度大于0且最后一个字节是空终止符（即以\0结尾的字符串）
        ESP_LOGI(TAG, "  Data: %s", (const char *)data); // 数据是合法的C字符串，直接打印其内容
    }
}

/**
 * 函数名: esp_now_manager_init
 * 功能: 初始化ESP-NOW管理器，注册发送和接收回调，并添加广播对等节点
 * 参数: 无
 * 返回值: ESP_OK表示初始化成功，其他值表示失败
 * 作者: Lskipktw
 * 日期: 2026-05-09  10:03:55
 */
esp_err_t esp_now_manager_init(void)                      // ESP-NOW管理器初始化函数，返回esp_err_t类型错误码
{
    if (s_ready) {                                        // 检查是否已经初始化过（幂等性保护）
        ESP_LOGW(TAG, "Already initialized");             // 已初始化：输出WARNING级别日志提示
        return ESP_OK;                                    // 返回ESP_OK表示成功（不重复初始化）
    }

    esp_err_t ret = esp_now_init();                       // 调用ESP-NOW底层初始化函数
    if (ret != ESP_OK) {                                  // 判断初始化是否失败
        ESP_LOGE(TAG, "esp_now_init failed: %s", esp_err_to_name(ret));  // 初始化失败：输出ERROR级别日志并显示具体错误名称
        return ret;                                       // 返回错误码给调用者
    }

    ret = esp_now_register_send_cb(on_send);              // 注册发送完成回调函数on_send
    if (ret != ESP_OK) {                                  // 判断注册发送回调是否失败
        ESP_LOGE(TAG, "Register send cb failed: %s", esp_err_to_name(ret));  // 注册失败：输出错误日志
        esp_now_deinit();                                 // 反初始化ESP-NOW，回滚已分配资源
        return ret;                                       // 返回错误码
    }

    ret = esp_now_register_recv_cb(on_recv);              // 注册接收回调函数on_recv
    if (ret != ESP_OK) {                                  // 判断注册接收回调是否失败
        ESP_LOGE(TAG, "Register recv cb failed: %s", esp_err_to_name(ret));  // 注册失败：输出错误日志
        esp_now_deinit();                                 // 反初始化ESP-NOW，回滚资源
        return ret;                                       // 返回错误码
    }

    /* 添加广播 peer */
    esp_now_peer_info_t peer = {                          // 声明并初始化对等节点信息结构体
        .channel = 0,                                     // 设置通信信道为0（使用当前WiFi信道）
        .ifidx = WIFI_IF_STA,                             // 设置网络接口为STA模式
        .encrypt = false,                                 // 设置不加密通信
    };
    memcpy(peer.peer_addr, s_broadcast_mac, ESP_NOW_ETH_ALEN);  // 将广播MAC地址（全FF）复制到peer结构体的地址字段

    ret = esp_now_add_peer(&peer);                        // 将广播peer添加到ESP-NOW对等列表中
    if (ret != ESP_OK && ret != ESP_ERR_ESPNOW_EXIST) {   // 判断添加失败且不是"已存在"错误（已存在是可接受的）
        ESP_LOGE(TAG, "Add broadcast peer failed: %s", esp_err_to_name(ret));  // 添加失败：输出错误日志
        esp_now_deinit();                                 // 反初始化ESP-NOW，回滚资源
        return ret;                                       // 返回错误码
    }

    s_ready = true;                                       // 设置就绪标志为true
    ESP_LOGI(TAG, "ESP-NOW manager initialized");         // 输出INFO日志，确认管理器初始化完成
    return ESP_OK;                                        // 返回ESP_OK表示初始化成功
}

/**
 * 函数名: esp_now_manager_broadcast
 * 功能: 通过ESP-NOW广播数据到所有设备
 * 参数:
 *      data - 要发送的数据缓冲区指针
 *      len - 数据长度（字节数）
 * 返回值: ESP_OK表示发送成功，ESP_ERR_INVALID_STATE表示未初始化，ESP_ERR_INVALID_ARG表示参数无效
 * 作者: Lskipktw
 * 日期: 2026-05-09  11:18:07
 */
esp_err_t esp_now_manager_broadcast(const uint8_t *data, size_t len)  // 广播发送函数定义
{
    if (!s_ready) {                                       // 检查ESP-NOW是否已初始化
        ESP_LOGE(TAG, "Not initialized");                 // 未初始化：输出ERROR日志
        return ESP_ERR_INVALID_STATE;                     // 返回"无效状态"错误码
    }

    if (data == NULL || len == 0) {                       // 参数校验：数据指针为空或长度为0
        return ESP_ERR_INVALID_ARG;                       // 返回"无效参数"错误码
    }

    return esp_now_send(s_broadcast_mac, data, len);      // 调用底层发送函数，目标地址为广播MAC，返回发送结果
}

/**
 * 函数名: esp_now_manager_send
 * 功能: 通过ESP-NOW向指定对等节点单播发送数据
 * 参数:
 *      peer_addr - 目标对等节点的MAC地址（6字节）
 *      data - 要发送的数据缓冲区指针
 *      len - 数据长度（字节数）
 * 返回值: ESP_OK表示发送成功，ESP_ERR_INVALID_STATE表示未初始化，ESP_ERR_INVALID_ARG表示参数无效
 * 作者: Lskipktw
 * 日期: 2026-05-09  13:44:29
 */
esp_err_t esp_now_manager_send(const uint8_t *peer_addr, const uint8_t *data, size_t len)  // 单播发送函数定义
{
    if (!s_ready) {                                       // 检查ESP-NOW是否已初始化
        ESP_LOGE(TAG, "Not initialized");                 // 未初始化：输出ERROR日志
        return ESP_ERR_INVALID_STATE;                     // 返回"无效状态"错误码
    }

    if (peer_addr == NULL || data == NULL || len == 0) {  // 参数校验：对等地址、数据指针为空或长度为0
        return ESP_ERR_INVALID_ARG;                       // 返回"无效参数"错误码
    }

    return esp_now_send(peer_addr, data, len);            // 调用底层发送函数，向指定MAC地址发送数据，返回发送结果
}

/**
 * 函数名: esp_now_manager_add_peer
 * 功能: 向ESP-NOW对等列表中添加一个对等节点，若已存在则静默返回成功
 * 参数:
 *      peer_addr - 要添加的对等节点的MAC地址（6字节）
 * 返回值: ESP_OK表示添加成功或已存在，其他值表示添加失败
 * 作者: Lskipktw
 * 日期: 2026-05-09  14:51:33
 */
esp_err_t esp_now_manager_add_peer(const uint8_t *peer_addr)  // 添加对等节点函数定义
{
    if (!s_ready) {                                       // 检查ESP-NOW是否已初始化
        ESP_LOGE(TAG, "Not initialized");                 // 未初始化：输出ERROR日志
        return ESP_ERR_INVALID_STATE;                     // 返回"无效状态"错误码
    }

    if (peer_addr == NULL) {                              // 参数校验：对等地址指针为空
        return ESP_ERR_INVALID_ARG;                       // 返回"无效参数"错误码
    }

    esp_now_peer_info_t peer = {                          // 声明并初始化对等节点信息结构体
        .channel = 0,                                     // 设置通信信道为0（使用当前WiFi信道）
        .ifidx = WIFI_IF_STA,                             // 设置网络接口为STA模式
        .encrypt = false,                                 // 设置不加密通信
    };
    memcpy(peer.peer_addr, peer_addr, ESP_NOW_ETH_ALEN);  // 将目标MAC地址复制到peer结构体的地址字段（6字节）

    esp_err_t ret = esp_now_add_peer(&peer);              // 调用底层API将对等节点添加到ESP-NOW对等表
    if (ret == ESP_ERR_ESPNOW_EXIST) {                    // 判断返回错误是否为"对等节点已存在"
        return ESP_OK;                                    // 已存在不是错误，返回ESP_OK静默处理
    }
    if (ret != ESP_OK) {                                  // 判断是否发生了其他类型的添加失败
        char mac_str[18];                                 // 声明18字节的MAC字符串缓冲区
        esp_now_manager_mac_to_str(peer_addr, mac_str, sizeof(mac_str));  // 将MAC地址转换为可读字符串
        ESP_LOGE(TAG, "Add peer %s failed: %s", mac_str, esp_err_to_name(ret));  // 输出ERROR日志，记录失败的对等节点MAC和错误名称
    }
    return ret;                                           // 返回底层API的原始结果
}

esp_err_t esp_now_manager_ensure_peer(const uint8_t *peer_addr)
{
    esp_err_t ret = esp_now_manager_add_peer(peer_addr);
    if (ret == ESP_ERR_ESPNOW_EXIST) {
        esp_now_peer_info_t peer = {
            .channel = 0,
            .ifidx = WIFI_IF_STA,
            .encrypt = false,
        };
        memcpy(peer.peer_addr, peer_addr, ESP_NOW_ETH_ALEN);
        ret = esp_now_mod_peer(&peer);
    }
    return ret;
}

/**
 * 函数名: esp_now_manager_register_recv_handler
 * 功能: 注册外部接收处理器，当收到ESP-NOW数据时优先调用此处理器
 * 参数:
 *      handler - 接收处理回调函数指针，传入NULL可取消注册
 * 返回值: 无
 * 作者: Lskipktw
 * 日期: 2026-05-09  15:09:18
 */
void esp_now_manager_register_recv_handler(esp_now_recv_handler_t handler)  // 注册外部接收处理器函数
{
    s_recv_handler = handler;                             // 将传入的回调函数指针保存到静态变量中
}

/**
 * 函数名: esp_now_manager_mac_to_str
 * 功能: 将6字节MAC地址转换为"XX:XX:XX:XX:XX:XX"格式的字符串
 * 参数:
 *      mac - 源MAC地址缓冲区指针（6字节）
 *      out_str - 输出字符串缓冲区指针
 *      size - 输出缓冲区大小（需>=18字节）
 * 返回值: 无
 * 作者: Lskipktw
 * 日期: 2026-05-09  16:22:46
 */
void esp_now_manager_mac_to_str(const uint8_t *mac, char *out_str, size_t size)  // MAC地址转字符串函数定义
{
    if (mac && out_str && size >= 18) {                   // 安全检查：三个参数均有效，且输出缓冲区足够容纳18字节
        snprintf(out_str, size, "%02X:%02X:%02X:%02X:%02X:%02X",  // 格式化MAC地址为"XX:XX:XX:XX:XX:XX"大写十六进制字符串
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);  // 依次输出MAC的6个字节
    }
}

/**
 * 函数名: esp_now_manager_is_ready
 * 功能: 查询ESP-NOW管理器是否已完成初始化
 * 参数: 无
 * 返回值: true表示已初始化就绪，false表示未初始化
 * 作者: Lskipktw
 * 日期: 2026-05-09  17:35:11
 */
bool esp_now_manager_is_ready(void)                       // 查询就绪状态函数定义
{
    return s_ready;                                       // 返回静态就绪标志的当前值
}
