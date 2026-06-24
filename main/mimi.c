/**
 * @file mimi.c
 * @brief MimiClaw 主入口 — ESP32-S3 AI Agent 固件启动与初始化
 *
 * MimiClaw 是一个运行在 ESP32-S3 上的 AI 助手固件，支持：
 *   - 飞书 / Telegram / WebSocket 多通道通信
 *   - LLM (DeepSeek v4 / Anthropic) 驱动的 ReAct Agent 循环
 *   - GPIO、WS2812 灯带、红外收发等硬件控制
 *   - 定时任务 (cron)、心跳检测、WiFi 配网
 *   - 持久化文件存储 (SPIFFS) 和短期记忆 (Session)
 *
 * app_main() 的启动流程：
 *   1. 核心基础设施：NVS → 事件循环 → SPIFFS
 *   2. 子系统初始化：消息总线、存储、技能、会话、WiFi、代理、通道、LLM、工具、定时器
 *   3. CLI 启动（可离线工作）
 *   4. WiFi 连接（如果未配置，进入配网模式，重启后重试）
 *   5. 网络服务启动：Agent 循环、Telegram/飞书/WebSocket、心跳
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"

#include "mimi_config.h"
#include "bus/message_bus.h"
#include "wifi/wifi_manager.h"
#include "channels/telegram/telegram_bot.h"
#include "channels/feishu/feishu_bot.h"
#include "llm/llm_proxy.h"
#include "agent/agent_loop.h"
#include "memory/memory_store.h"
#include "memory/session_mgr.h"
#include "gateway/ws_server.h"
#include "cli/serial_cli.h"
#include "proxy/http_proxy.h"
#include "tools/tool_registry.h"
#include "cron/cron_service.h"
#include "heartbeat/heartbeat.h"
#include "skills/skill_loader.h"
#include "onboard/wifi_onboard.h"
#include "espnow/esp_now_manager.h"
#include "espnow/esp_now_device.h"
#include "display/oled_display.h"
#include "slave/slave_executor.h"

static const char *TAG = "mimi";

/**
 * @brief 初始化 NVS（非易失性存储）
 *
 * NVS 用于保存 WiFi 凭据、API Key、LLM 配置等。
 * 如果分区损坏或版本不匹配，先擦除再重试。
 */
static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

/**
 * @brief 初始化 SPIFFS 文件系统
 *
 * SPIFFS 挂载到 /spiffs，用于存储：
 *   - 长期记忆 /spiffs/memory/MEMORY.md
 *   - 对话会话 /spiffs/sessions/tg_*.jsonl
 *   - 技能文件 /spiffs/skills/ (技能以 *.md 形式存储)
 *   - 定时任务 /spiffs/CRON.json
 *   - 心跳 /spiffs/HEARTBEAT.md
 */
static esp_err_t init_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = MIMI_SPIFFS_BASE,
        .partition_label = NULL,
        .max_files = 10,
        .format_if_mount_failed = true,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0, used = 0;
    esp_spiffs_info(NULL, &total, &used);
    ESP_LOGI(TAG, "SPIFFS: total=%d, used=%d", (int)total, (int)used);

    return ESP_OK;
}

/**
 * @brief 出站消息分发任务
 *
 * 从 outbound 消息队列中取出消息，根据 channel 字段路由到对应的发送接口：
 *   - telegram  → telegram_send_message()
 *   - feishu    → feishu_send_message()
 *   - websocket → ws_server_send()
 *   - system    → 仅日志记录
 *
 * 该任务在 Agent Loop 产生回复之前启动，避免竞争（先启动消费者再启动生产者）。
 */
static void outbound_dispatch_task(void *arg)
{
    ESP_LOGI(TAG, "Outbound dispatch started");

    while (1) {
        mimi_msg_t msg;
        if (message_bus_pop_outbound(&msg, UINT32_MAX) != ESP_OK) continue;

        ESP_LOGI(TAG, "Dispatching response to %s:%s", msg.channel, msg.chat_id);

        /* 更新 OLED 最新消息显示 */
        oled_display_set_last_msg(msg.content);

        if (strcmp(msg.channel, MIMI_CHAN_TELEGRAM) == 0) {
            esp_err_t send_err = telegram_send_message(msg.chat_id, msg.content);
            if (send_err != ESP_OK) {
                ESP_LOGE(TAG, "Telegram send failed for %s: %s", msg.chat_id, esp_err_to_name(send_err));
            } else {
                ESP_LOGI(TAG, "Telegram send success for %s (%d bytes)", msg.chat_id, (int)strlen(msg.content));
            }
        } else if (strcmp(msg.channel, MIMI_CHAN_FEISHU) == 0) {
            esp_err_t send_err = feishu_send_message(msg.chat_id, msg.content);
            if (send_err != ESP_OK) {
                ESP_LOGE(TAG, "Feishu send failed for %s: %s", msg.chat_id, esp_err_to_name(send_err));
            } else {
                ESP_LOGI(TAG, "Feishu send success for %s (%d bytes)", msg.chat_id, (int)strlen(msg.content));
            }
        } else if (strcmp(msg.channel, MIMI_CHAN_WEBSOCKET) == 0) {
            esp_err_t ws_err = ws_server_send(msg.chat_id, msg.content);
            if (ws_err != ESP_OK) {
                ESP_LOGW(TAG, "WS send failed for %s: %s", msg.chat_id, esp_err_to_name(ws_err));
            }
        } else if (strcmp(msg.channel, MIMI_CHAN_SYSTEM) == 0) {
            ESP_LOGI(TAG, "System message [%s]: %.128s", msg.chat_id, msg.content);
        } else {
            ESP_LOGW(TAG, "Unknown channel: %s", msg.channel);
        }

        free(msg.content);
    }
}

/**
 * @brief 主入口函数
 *
 * 启动顺序（严格按依赖关系排列）：
 *
 *   Phase 1 — 核心基础设施
 *     NVS  → 事件循环 → SPIFFS
 *     （NVS 提供 WiFi/API 凭据，SPIFFS 提供文件存储）
 *
 *   Phase 2 — 子系统初始化（不依赖网络）
 *     消息总线 → 存储 → 技能加载器 → 会话管理器 → WiFi → HTTP代理
 *     → Telegram/飞书/LLM → 工具注册 → 定时器 → 心跳 → Agent循环
 *
 *   Phase 3 — CLI（可离线工作，不依赖网络）
 *
 *   Phase 4 — WiFi 连接
 *     已有凭据 → 尝试连接，超时 30s
 *     无凭据   → 进入配网模式（Captive Portal），重启后重试
 *
 *   Phase 5 — 网络服务（WiFi 连接后才启动）
 *     出站分发任务 → Agent Loop → Telegram/飞书轮询 → 定时器 → 心跳 → WebSocketidf.py build
 */
void app_main(void)
{
    /* 降低不相关组件的日志级别，减少串口输出 */
    esp_log_level_set("esp-x509-crt-bundle", ESP_LOG_WARN);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  MimiClaw - ESP32-S3 AI Agent");
    ESP_LOGI(TAG, "========================================");

    /* 打印内存信息，方便调试内存不足问题 */
    ESP_LOGI(TAG, "Internal free: %d bytes",
             (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "PSRAM free:    %d bytes",
             (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    /* ── Phase 1: 核心基础设施（公共） ──────────────────────────── */
    ESP_ERROR_CHECK(init_nvs());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(init_spiffs());

    /* ── Phase 2: WiFi 管理器（公共，ESP-NOW 依赖 WiFi PHY） ──── */
    ESP_ERROR_CHECK(wifi_manager_init());

    /* ── Phase 3: OLED（公共，调试/状态显示） ──────────────────── */
    ESP_LOGI(TAG, "Initializing OLED display (SDA=GPIO%d, SCL=GPIO%d)...",
             MIMI_OLED_SDA_GPIO, MIMI_OLED_SCL_GPIO);
    esp_err_t oled_err = oled_display_init();
    if (oled_err != ESP_OK) {
        ESP_LOGW(TAG, "OLED init failed: %s (non-fatal)", esp_err_to_name(oled_err));
    }

    /* ── Phase 4: WiFi 连接 + 角色识别 ──────────────────────────── */
    esp_err_t wifi_err = wifi_manager_start();
    bool wifi_ok = false;
    bool is_master = false;
    if (wifi_err == ESP_OK) {
        ESP_LOGI(TAG, "Scanning nearby APs on boot...");
        wifi_manager_scan_and_print();
        ESP_LOGI(TAG, "Waiting for WiFi connection...");
        if (wifi_manager_wait_connected(30000) == ESP_OK) {
            wifi_ok = true;
            ESP_LOGI(TAG, "WiFi connected: %s", wifi_manager_get_ip());

            /* 初始化 ESP-NOW（公共） */
            esp_err_t espnow_err = esp_now_manager_init();
            if (espnow_err != ESP_OK) {
                ESP_LOGW(TAG, "ESP-NOW init failed: %s", esp_err_to_name(espnow_err));
            } else {
                esp_now_device_init();
                is_master = esp_now_device_is_master();

                if (is_master) {
                    /* 主设备：发现从设备 */
                    int found = esp_now_device_discover_peers(5000);
                    ESP_LOGI(TAG, "ESP-NOW device discovery: found %d slave(s)", found);
                    ESP_LOGI(TAG, "Role: MASTER — AI Agent mode");
                } else {
                    /* 从设备：初始化硬件执行器 */
                    esp_err_t slv_err = slave_executor_init();
                    if (slv_err != ESP_OK) {
                        ESP_LOGE(TAG, "Slave executor init failed: %s",
                                 esp_err_to_name(slv_err));
                    }
                    ESP_LOGI(TAG, "Role: SLAVE — hardware executor mode");
                }
            }
        } else {
            ESP_LOGW(TAG, "WiFi connection timeout");
        }
    } else {
        ESP_LOGW(TAG, "No WiFi credentials configured");
    }

    /* 如果 WiFi 连不上，进入配网模式（公共） */
    if (!wifi_ok) {
        ESP_LOGW(TAG, "Entering WiFi onboarding mode...");
        wifi_onboard_start(WIFI_ONBOARD_MODE_CAPTIVE);
        return;
    }

    /* 启动管理 Portal（公共） */
    if (wifi_onboard_start(WIFI_ONBOARD_MODE_ADMIN) != ESP_OK) {
        ESP_LOGW(TAG, "Local admin portal unavailable; continuing without config hotspot");
    }

    /* ── Phase 5: Master-only 子系统（不依赖网络） ──────────────── */
    if (is_master) {
        ESP_ERROR_CHECK(message_bus_init());
        ESP_ERROR_CHECK(memory_store_init());
        ESP_ERROR_CHECK(skill_loader_init());
        ESP_ERROR_CHECK(session_mgr_init());
        ESP_ERROR_CHECK(http_proxy_init());
        ESP_ERROR_CHECK(telegram_bot_init());
        ESP_ERROR_CHECK(feishu_bot_init());
        ESP_ERROR_CHECK(llm_proxy_init());
        ESP_ERROR_CHECK(tool_registry_init());
        ESP_ERROR_CHECK(cron_service_init());
        ESP_ERROR_CHECK(heartbeat_init());
        ESP_ERROR_CHECK(agent_loop_init());
    }

    /* ── Phase 6: CLI（公共，调试用） ────────────────────────────── */
    ESP_ERROR_CHECK(serial_cli_init());

    /* ── Phase 7: Master-only 网络服务 ──────────────────────────── */
    if (is_master) {
        ESP_ERROR_CHECK((xTaskCreatePinnedToCore(
            outbound_dispatch_task, "outbound",
            MIMI_OUTBOUND_STACK, NULL,
            MIMI_OUTBOUND_PRIO, NULL, MIMI_OUTBOUND_CORE) == pdPASS)
            ? ESP_OK : ESP_FAIL);

        ESP_ERROR_CHECK(agent_loop_start());
        ESP_ERROR_CHECK(telegram_bot_start());
        ESP_ERROR_CHECK(feishu_bot_start());
        cron_service_start();
        heartbeat_start();
        ESP_ERROR_CHECK(ws_server_start());

        ESP_LOGI(TAG, "Master: all services started!");
        ESP_LOGI(TAG, "MimiClaw ready. Type 'help' for CLI commands.");
    } else {
        ESP_LOGI(TAG, "Slave: ready, waiting for ESP-NOW commands. Type 'help' for CLI.");
        /* 从机空闲循环 — 等待 ESP-NOW 命令 */
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(10000));
            ESP_LOGI(TAG, "Slave alive: free heap=%d",
                     (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        }
    }
}
