#include "tool_registry.h"
#include "mimi_config.h"
#include "tools/tool_web_search.h"
#include "tools/tool_get_time.h"
#include "tools/tool_files.h"
#include "tools/tool_cron.h"
#include "tools/tool_gpio.h"
#include "tools/tool_ws2812.h"
#include "tools/tool_ir.h"

#include <string.h>
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "tools";

#define MAX_TOOLS 24  /* 最大工具数量 */

static mimi_tool_t s_tools[MAX_TOOLS];  /* 工具注册表 */
static int s_tool_count = 0;            /* 当前注册的工具数量 */
static char *s_tools_json = NULL;       /* 缓存的工具JSON数组字符串 */

/**
 * 函数名: register_tool
 * 功能: 注册一个新工具到工具注册表
 * 参数:
 *      tool - 要注册的工具结构体指针
 * 返回值: 无
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
static void register_tool(const mimi_tool_t *tool)
{
    if (s_tool_count >= MAX_TOOLS) {
        ESP_LOGE(TAG, "Tool registry full");
        return;
    }
    s_tools[s_tool_count++] = *tool;
    ESP_LOGI(TAG, "Registered tool: %s", tool->name);
}

/**
 * 函数名: build_tools_json
 * 功能: 构建所有注册工具的JSON数组字符串，用于API响应
 * 参数: 无
 * 返回值: 无
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
static void build_tools_json(void)
{
    cJSON *arr = cJSON_CreateArray();

    for (int i = 0; i < s_tool_count; i++) {
        cJSON *tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "name", s_tools[i].name);
        cJSON_AddStringToObject(tool, "description", s_tools[i].description);

        cJSON *schema = cJSON_Parse(s_tools[i].input_schema_json);
        if (schema) {
            cJSON_AddItemToObject(tool, "input_schema", schema);
        }

        cJSON_AddItemToArray(arr, tool);
    }

    free(s_tools_json);
    s_tools_json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    ESP_LOGI(TAG, "Tools JSON built (%d tools)", s_tool_count);
}

/**
 * 函数名: tool_registry_init
 * 功能: 初始化工具注册表，注册所有可用的工具
 * 参数: 无
 * 返回值: ESP_OK始终返回成功
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_registry_init(void)
{
    s_tool_count = 0;

    /* Register web_search */
    tool_web_search_init();

    mimi_tool_t ws = {
        .name = "web_search",
        .description = "Search the web for current information via Tavily (preferred) or Brave when configured.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"query\":{\"type\":\"string\",\"description\":\"The search query\"}},"
            "\"required\":[\"query\"]}",
        .execute = tool_web_search_execute,
    };
    register_tool(&ws);

    /* Register get_current_time */
    mimi_tool_t gt = {
        .name = "get_current_time",
        .description = "Get the current date and time. Also sets the system clock. Call this when you need to know what time or date it is.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{},"
            "\"required\":[]}",
        .execute = tool_get_time_execute,
    };
    register_tool(&gt);

    /* Register read_file */
    mimi_tool_t rf = {
        .name = "read_file",
        .description = "Read a file from SPIFFS storage. Path must start with " MIMI_SPIFFS_BASE "/.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Absolute path starting with " MIMI_SPIFFS_BASE "/\"}},"
            "\"required\":[\"path\"]}",
        .execute = tool_read_file_execute,
    };
    register_tool(&rf);

    /* Register write_file */
    mimi_tool_t wf = {
        .name = "write_file",
        .description = "Write or overwrite a file on SPIFFS storage. Path must start with " MIMI_SPIFFS_BASE "/.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Absolute path starting with " MIMI_SPIFFS_BASE "/\"},"
            "\"content\":{\"type\":\"string\",\"description\":\"File content to write\"}},"
            "\"required\":[\"path\",\"content\"]}",
        .execute = tool_write_file_execute,
    };
    register_tool(&wf);

    /* Register edit_file */
    mimi_tool_t ef = {
        .name = "edit_file",
        .description = "Find and replace text in a file on SPIFFS. Replaces first occurrence of old_string with new_string.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Absolute path starting with " MIMI_SPIFFS_BASE "/\"},"
            "\"old_string\":{\"type\":\"string\",\"description\":\"Text to find\"},"
            "\"new_string\":{\"type\":\"string\",\"description\":\"Replacement text\"}},"
            "\"required\":[\"path\",\"old_string\",\"new_string\"]}",
        .execute = tool_edit_file_execute,
    };
    register_tool(&ef);

    /* Register list_dir */
    mimi_tool_t ld = {
        .name = "list_dir",
        .description = "List files on SPIFFS storage, optionally filtered by path prefix.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"prefix\":{\"type\":\"string\",\"description\":\"Optional path prefix filter, e.g. " MIMI_SPIFFS_BASE "/memory/\"}},"
            "\"required\":[]}",
        .execute = tool_list_dir_execute,
    };
    register_tool(&ld);

    /* Register cron_add */
    mimi_tool_t ca = {
        .name = "cron_add",
        .description = "Schedule a recurring or one-shot task. The message will trigger an agent turn when the job fires.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{"
            "\"name\":{\"type\":\"string\",\"description\":\"Short name for the job\"},"
            "\"schedule_type\":{\"type\":\"string\",\"description\":\"'every' for recurring interval or 'at' for one-shot at a unix timestamp\"},"
            "\"interval_s\":{\"type\":\"integer\",\"description\":\"Interval in seconds (required for 'every')\"},"
            "\"at_epoch\":{\"type\":\"integer\",\"description\":\"Unix timestamp to fire at (required for 'at')\"},"
            "\"message\":{\"type\":\"string\",\"description\":\"Message to inject when the job fires, triggering an agent turn\"},"
            "\"channel\":{\"type\":\"string\",\"description\":\"Optional reply channel (e.g. 'telegram'). If omitted, current turn channel is used when available\"},"
            "\"chat_id\":{\"type\":\"string\",\"description\":\"Optional reply chat_id. Required when channel='telegram'. If omitted during a Telegram turn, current chat_id is used\"}"
            "},"
            "\"required\":[\"name\",\"schedule_type\",\"message\"]}",
        .execute = tool_cron_add_execute,
    };
    register_tool(&ca);

    /* Register cron_list */
    mimi_tool_t cl = {
        .name = "cron_list",
        .description = "List all scheduled cron jobs with their status, schedule, and IDs.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{},"
            "\"required\":[]}",
        .execute = tool_cron_list_execute,
    };
    register_tool(&cl);

    /* Register cron_remove */
    mimi_tool_t cr = {
        .name = "cron_remove",
        .description = "Remove a scheduled cron job by its ID.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"job_id\":{\"type\":\"string\",\"description\":\"The 8-character job ID to remove\"}},"
            "\"required\":[\"job_id\"]}",
        .execute = tool_cron_remove_execute,
    };
    register_tool(&cr);

    /* Register GPIO tools */
    tool_gpio_init();

    mimi_tool_t gw = {
        .name = "gpio_write",
        .description = "Set a GPIO pin HIGH or LOW. Controls LEDs, relays, and other digital outputs.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"pin\":{\"type\":\"integer\",\"description\":\"GPIO pin number\"},"
            "\"state\":{\"type\":\"integer\",\"description\":\"1 for HIGH, 0 for LOW\"}},"
            "\"required\":[\"pin\",\"state\"]}",
        .execute = tool_gpio_write_execute,
    };
    register_tool(&gw);

    mimi_tool_t gr = {
        .name = "gpio_read",
        .description = "Read a GPIO pin state. Returns HIGH or LOW. Use for checking switches, sensors, and digital inputs.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"pin\":{\"type\":\"integer\",\"description\":\"GPIO pin number\"}},"
            "\"required\":[\"pin\"]}",
        .execute = tool_gpio_read_execute,
    };
    register_tool(&gr);

    mimi_tool_t ga = {
        .name = "gpio_read_all",
        .description = "Read all allowed GPIO pin states in a single call. Returns each pin's HIGH/LOW state.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{},"
            "\"required\":[]}",
        .execute = tool_gpio_read_all_execute,
    };
    register_tool(&ga);

    /* WS2812 RGB LED */
    tool_ws2812_init();

    mimi_tool_t ws_set = {
        .name = "ws2812_set",
        .description = "Set WS2812 RGB LED color on GPIO48. Call this for any light/color command (like 'turn on red light', '关灯', 'blue light', etc).",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{"
            "\"red\":{\"type\":\"integer\",\"description\":\"Red 0-255\",\"default\":0},"
            "\"green\":{\"type\":\"integer\",\"description\":\"Green 0-255\",\"default\":0},"
            "\"blue\":{\"type\":\"integer\",\"description\":\"Blue 0-255\",\"default\":0}"
            "},"
            "\"required\":[]}",
        .execute = tool_ws2812_set_execute,
    };
    register_tool(&ws_set);

    mimi_tool_t ws_off = {
        .name = "ws2812_off",
        .description = "Turn off WS2812 RGB LED.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{},"
            "\"required\":[]}",
        .execute = tool_ws2812_off_execute,
    };
    register_tool(&ws_off);

    /* IR Remote Control */
    tool_ir_init();

    mimi_tool_t ir_recv = {
        .name = "ir_receive",
        .description = "Learn an IR remote code. Point the remote at the IR receiver, "
                       "then provide a name for the code. Captures one IR frame and stores it.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{"
            "\"name\":{\"type\":\"string\",\"description\":\"Name for the IR code (e.g. 'ac_power', 'ac_temp_up')\"}"
            "},"
            "\"required\":[\"name\"]}",
        .execute = tool_ir_receive_execute,
    };
    register_tool(&ir_recv);

    mimi_tool_t ir_send = {
        .name = "ir_send",
        .description = "Send a stored IR code. Use ir_list to see available codes.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{"
            "\"name\":{\"type\":\"string\",\"description\":\"Name of the IR code to send\"},"
            "\"repeat\":{\"type\":\"integer\",\"description\":\"Number of times to send (default 1, use 2-3 for AC)\",\"default\":1}"
            "},"
            "\"required\":[\"name\"]}",
        .execute = tool_ir_send_execute,
    };
    register_tool(&ir_send);

    mimi_tool_t ir_ls = {
        .name = "ir_list",
        .description = "List all learned IR remote codes with their protocol details.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{},"
            "\"required\":[]}",
        .execute = tool_ir_list_execute,
    };
    register_tool(&ir_ls);

    build_tools_json();

    ESP_LOGI(TAG, "Tool registry initialized");
    return ESP_OK;
}

/**
 * 函数名: tool_registry_get_tools_json
 * 功能: 获取所有注册工具的JSON数组字符串
 * 参数: 无
 * 返回值: JSON字符串指针，包含所有工具信息
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
const char *tool_registry_get_tools_json(void)
{
    return s_tools_json;
}

/**
 * 函数名: tool_registry_execute
 * 功能: 执行指定名称的工具
 * 参数:
 *      name - 要执行的工具名称
 *      input_json - JSON格式输入参数
 *      output - 结果输出缓冲区
 *      output_size - 输出缓冲区大小
 * 返回值: ESP_OK成功，ESP_ERR_NOT_FOUND工具不存在，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_registry_execute(const char *name, const char *input_json,
                                char *output, size_t output_size)
{
    for (int i = 0; i < s_tool_count; i++) {
        if (strcmp(s_tools[i].name, name) == 0) {
            ESP_LOGI(TAG, "Executing tool: %s", name);
            return s_tools[i].execute(input_json, output, output_size);
        }
    }

    ESP_LOGW(TAG, "Unknown tool: %s", name);
    snprintf(output, output_size, "Error: unknown tool '%s'", name);
    return ESP_ERR_NOT_FOUND;
}
