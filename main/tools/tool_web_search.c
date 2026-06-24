#include "tool_web_search.h"
#include "mimi_config.h"
#include "proxy/http_proxy.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "cJSON.h"

static const char *TAG = "web_search";

/* 搜索提供商枚举 */
typedef enum {
    SEARCH_PROVIDER_NONE = 0,    /* 未配置 */
    SEARCH_PROVIDER_BRAVE,       /* Brave搜索 */
    SEARCH_PROVIDER_TAVILY,      /* Tavily搜索 */
} search_provider_t;

static char s_brave_key[128] = {0};     /* Brave API密钥 */
static char s_tavily_key[128] = {0};    /* Tavily API密钥 */
static search_provider_t s_provider = SEARCH_PROVIDER_NONE;  /* 当前搜索提供商 */

#define SEARCH_BUF_SIZE     (16 * 1024)  /* 搜索响应缓冲区大小：16KB */
#define SEARCH_RESULT_COUNT 5            /* 返回的搜索结果数量 */

/* ── Response accumulator ─────────────────────────────────────── */

/* 响应累积器结构体 */
typedef struct {
    char *data;      /* 响应数据缓冲区 */
    size_t len;      /* 当前数据长度 */
    size_t cap;      /* 缓冲区容量 */
} search_buf_t;

/**
 * 函数名: http_event_handler
 * 功能: HTTP事件处理器，累积接收HTTP响应数据
 * 参数:
 *      evt - HTTP事件结构体
 * 返回值: ESP_OK始终返回成功
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    search_buf_t *sb = (search_buf_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        size_t needed = sb->len + evt->data_len;
        if (needed < sb->cap) {
            memcpy(sb->data + sb->len, evt->data, evt->data_len);
            sb->len += evt->data_len;
            sb->data[sb->len] = '\0';
        }
    }
    return ESP_OK;
}

/* ── Init ─────────────────────────────────────────────────────── */

/**
 * 函数名: tool_web_search_init
 * 功能: 初始化web搜索工具，加载API密钥并选择搜索提供商
 * 参数: 无
 * 返回值: ESP_OK始终返回成功
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_web_search_init(void)
{
    /* Start with build-time defaults */
    if (MIMI_SECRET_SEARCH_KEY[0] != '\0') {
        strncpy(s_brave_key, MIMI_SECRET_SEARCH_KEY, sizeof(s_brave_key) - 1);
    }
    if (MIMI_SECRET_TAVILY_KEY[0] != '\0') {
        strncpy(s_tavily_key, MIMI_SECRET_TAVILY_KEY, sizeof(s_tavily_key) - 1);
    }

    /* NVS overrides take highest priority (set via CLI) */
    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_SEARCH, NVS_READONLY, &nvs) == ESP_OK) {
        char tmp[128] = {0};
        size_t len = sizeof(tmp);
        if (nvs_get_str(nvs, MIMI_NVS_KEY_API_KEY, tmp, &len) == ESP_OK && tmp[0]) {
            strncpy(s_brave_key, tmp, sizeof(s_brave_key) - 1);
        }
        memset(tmp, 0, sizeof(tmp));
        len = sizeof(tmp);
        if (nvs_get_str(nvs, MIMI_NVS_KEY_TAVILY_KEY, tmp, &len) == ESP_OK && tmp[0]) {
            strncpy(s_tavily_key, tmp, sizeof(s_tavily_key) - 1);
        }
        nvs_close(nvs);
    }

    if (s_tavily_key[0]) {
        s_provider = SEARCH_PROVIDER_TAVILY;
    } else if (s_brave_key[0]) {
        s_provider = SEARCH_PROVIDER_BRAVE;
    } else {
        s_provider = SEARCH_PROVIDER_NONE;
    }

    if (s_provider == SEARCH_PROVIDER_TAVILY) {
        ESP_LOGI(TAG, "Web search initialized (provider=tavily)");
    } else if (s_provider == SEARCH_PROVIDER_BRAVE) {
        ESP_LOGI(TAG, "Web search initialized (provider=brave)");
    } else {
        ESP_LOGW(TAG, "No search API key. Use CLI: set_search_key or set_tavily_key");
    }
    return ESP_OK;
}

/* ── URL-encode a query string ────────────────────────────────── */

/**
 * 函数名: url_encode
 * 功能: 对查询字符串进行URL编码
 * 参数:
 *      src - 源字符串
 *      dst - 目标缓冲区
 *      dst_size - 目标缓冲区大小
 * 返回值: 编码后的字符串长度
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
static size_t url_encode(const char *src, char *dst, size_t dst_size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t pos = 0;

    for (; *src && pos < dst_size - 3; src++) {
        unsigned char c = (unsigned char)*src;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[pos++] = c;
        } else if (c == ' ') {
            dst[pos++] = '+';
        } else {
            dst[pos++] = '%';
            dst[pos++] = hex[c >> 4];
            dst[pos++] = hex[c & 0x0F];
        }
    }
    dst[pos] = '\0';
    return pos;
}

/* ── Format results as readable text ──────────────────────────── */

/**
 * 函数名: format_results
 * 功能: 格式化Brave搜索结果为可读文本
 * 参数:
 *      root - JSON根对象
 *      output - 输出缓冲区
 *      output_size - 输出缓冲区大小
 * 返回值: 无
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
static void format_results(cJSON *root, char *output, size_t output_size)
{
    cJSON *web = cJSON_GetObjectItem(root, "web");
    if (!web) {
        snprintf(output, output_size, "No web results found.");
        return;
    }

    cJSON *results = cJSON_GetObjectItem(web, "results");
    if (!results || !cJSON_IsArray(results) || cJSON_GetArraySize(results) == 0) {
        snprintf(output, output_size, "No web results found.");
        return;
    }

    size_t off = 0;
    int idx = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, results) {
        if (idx >= SEARCH_RESULT_COUNT) break;
        if (off >= output_size - 1) break;

        cJSON *title = cJSON_GetObjectItem(item, "title");
        cJSON *url = cJSON_GetObjectItem(item, "url");
        cJSON *desc = cJSON_GetObjectItem(item, "description");

        int written = snprintf(output + off, output_size - off,
            "%d. %s\n   %s\n   %s\n\n",
            idx + 1,
            (title && cJSON_IsString(title)) ? title->valuestring : "(no title)",
            (url && cJSON_IsString(url)) ? url->valuestring : "",
            (desc && cJSON_IsString(desc)) ? desc->valuestring : "");

        if (written < 0) break;
        if ((size_t)written >= output_size - off) {
            off = output_size - 1;
            break;
        }
        off += (size_t)written;
        idx++;
    }
}

/**
 * 函数名: format_tavily_results
 * 功能: 格式化Tavily搜索结果为可读文本
 * 参数:
 *      root - JSON根对象
 *      output - 输出缓冲区
 *      output_size - 输出缓冲区大小
 * 返回值: 无
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
static void format_tavily_results(cJSON *root, char *output, size_t output_size)
{
    cJSON *results = cJSON_GetObjectItem(root, "results");
    if (!results || !cJSON_IsArray(results) || cJSON_GetArraySize(results) == 0) {
        snprintf(output, output_size, "No web results found.");
        return;
    }

    size_t off = 0;
    int idx = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, results) {
        if (idx >= SEARCH_RESULT_COUNT) break;
        if (off >= output_size - 1) break;

        cJSON *title = cJSON_GetObjectItem(item, "title");
        cJSON *url = cJSON_GetObjectItem(item, "url");
        cJSON *content = cJSON_GetObjectItem(item, "content");

        int written = snprintf(output + off, output_size - off,
            "%d. %s\n   %s\n   %s\n\n",
            idx + 1,
            (title && cJSON_IsString(title)) ? title->valuestring : "(no title)",
            (url && cJSON_IsString(url)) ? url->valuestring : "",
            (content && cJSON_IsString(content)) ? content->valuestring : "");

        if (written < 0) break;
        if ((size_t)written >= output_size - off) {
            off = output_size - 1;
            break;
        }
        off += (size_t)written;
        idx++;
    }
}

/**
 * 函数名: build_tavily_payload
 * 功能: 构建Tavily搜索API的请求负载JSON
 * 参数:
 *      query - 搜索查询字符串
 * 返回值: JSON字符串指针，需要调用者释放，失败返回NULL
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
static char *build_tavily_payload(const char *query)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON_AddStringToObject(root, "query", query);
    cJSON_AddNumberToObject(root, "max_results", SEARCH_RESULT_COUNT);
    cJSON_AddBoolToObject(root, "include_answer", false);
    cJSON_AddStringToObject(root, "search_depth", "basic");
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return payload;
}

/* ── Direct HTTPS request ─────────────────────────────────────── */

/**
 * 函数名: brave_search_direct
 * 功能: 通过直接HTTPS请求执行Brave搜索
 * 参数:
 *      url - 完整的搜索URL
 *      sb - 响应缓冲区
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
static esp_err_t brave_search_direct(const char *url, search_buf_t *sb)
{
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = sb,
        .timeout_ms = 15000,
        .buffer_size = 4096,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;

    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "X-Subscription-Token", s_brave_key);

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) return err;
    if (status != 200) {
        ESP_LOGE(TAG, "Search API returned %d", status);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ── Proxy HTTPS request ──────────────────────────────────────── */

/**
 * 函数名: brave_search_via_proxy
 * 功能: 通过HTTP代理执行Brave搜索
 * 参数:
 *      path - 搜索路径（包含查询参数）
 *      sb - 响应缓冲区
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
static esp_err_t brave_search_via_proxy(const char *path, search_buf_t *sb)
{
    proxy_conn_t *conn = proxy_conn_open("api.search.brave.com", 443, 15000);
    if (!conn) return ESP_ERR_HTTP_CONNECT;

    char header[512];
    int hlen = snprintf(header, sizeof(header),
        "GET %s HTTP/1.1\r\n"
        "Host: api.search.brave.com\r\n"
        "Accept: application/json\r\n"
        "X-Subscription-Token: %s\r\n"
        "Connection: close\r\n\r\n",
        path, s_brave_key);

    if (proxy_conn_write(conn, header, hlen) < 0) {
        proxy_conn_close(conn);
        return ESP_ERR_HTTP_WRITE_DATA;
    }

    /* Read full response */
    char tmp[4096];
    size_t total = 0;
    while (1) {
        int n = proxy_conn_read(conn, tmp, sizeof(tmp), 15000);
        if (n <= 0) break;
        size_t copy = (total + n < sb->cap - 1) ? (size_t)n : sb->cap - 1 - total;
        if (copy > 0) {
            memcpy(sb->data + total, tmp, copy);
            total += copy;
        }
    }
    sb->data[total] = '\0';
    sb->len = total;
    proxy_conn_close(conn);

    /* Check status */
    int status = 0;
    if (total > 5 && strncmp(sb->data, "HTTP/", 5) == 0) {
        const char *sp = strchr(sb->data, ' ');
        if (sp) status = atoi(sp + 1);
    }

    /* Strip headers */
    char *body = strstr(sb->data, "\r\n\r\n");
    if (body) {
        body += 4;
        size_t blen = total - (body - sb->data);
        memmove(sb->data, body, blen);
        sb->len = blen;
        sb->data[sb->len] = '\0';
    }

    if (status != 200) {
        ESP_LOGE(TAG, "Search API returned %d via proxy", status);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * 函数名: tavily_search_direct
 * 功能: 通过直接HTTPS请求执行Tavily搜索
 * 参数:
 *      query - 搜索查询字符串
 *      sb - 响应缓冲区
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
static esp_err_t tavily_search_direct(const char *query, search_buf_t *sb)
{
    char *payload = build_tavily_payload(query);
    if (!payload) return ESP_ERR_NO_MEM;

    esp_http_client_config_t config = {
        .url = "https://api.tavily.com/search",
        .event_handler = http_event_handler,
        .user_data = sb,
        .timeout_ms = 15000,
        .buffer_size = 4096,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(payload);
        return ESP_FAIL;
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "Content-Type", "application/json");
    char auth[192];
    snprintf(auth, sizeof(auth), "Bearer %s", s_tavily_key);
    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_post_field(client, payload, strlen(payload));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(payload);

    if (err != ESP_OK) return err;
    if (status != 200) {
        ESP_LOGE(TAG, "Tavily API returned %d", status);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * 函数名: tavily_search_via_proxy
 * 功能: 通过HTTP代理执行Tavily搜索
 * 参数:
 *      query - 搜索查询字符串
 *      sb - 响应缓冲区
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
static esp_err_t tavily_search_via_proxy(const char *query, search_buf_t *sb)
{
    proxy_conn_t *conn = proxy_conn_open("api.tavily.com", 443, 15000);
    if (!conn) return ESP_ERR_HTTP_CONNECT;

    char *payload = build_tavily_payload(query);
    if (!payload) {
        proxy_conn_close(conn);
        return ESP_ERR_NO_MEM;
    }

    char header[768];
    int hlen = snprintf(header, sizeof(header),
        "POST /search HTTP/1.1\r\n"
        "Host: api.tavily.com\r\n"
        "Accept: application/json\r\n"
        "Content-Type: application/json\r\n"
        "Authorization: Bearer %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        s_tavily_key, (int)strlen(payload));

    if (proxy_conn_write(conn, header, hlen) < 0 ||
        proxy_conn_write(conn, payload, strlen(payload)) < 0) {
        free(payload);
        proxy_conn_close(conn);
        return ESP_ERR_HTTP_WRITE_DATA;
    }
    free(payload);

    char tmp[4096];
    size_t total = 0;
    while (1) {
        int n = proxy_conn_read(conn, tmp, sizeof(tmp), 15000);
        if (n <= 0) break;
        size_t copy = (total + n < sb->cap - 1) ? (size_t)n : sb->cap - 1 - total;
        if (copy > 0) {
            memcpy(sb->data + total, tmp, copy);
            total += copy;
        }
    }
    sb->data[total] = '\0';
    sb->len = total;
    proxy_conn_close(conn);

    int status = 0;
    if (total > 5 && strncmp(sb->data, "HTTP/", 5) == 0) {
        const char *sp = strchr(sb->data, ' ');
        if (sp) status = atoi(sp + 1);
    }

    char *body = strstr(sb->data, "\r\n\r\n");
    if (body) {
        body += 4;
        size_t blen = total - (body - sb->data);
        memmove(sb->data, body, blen);
        sb->len = blen;
        sb->data[sb->len] = '\0';
    }

    if (status != 200) {
        ESP_LOGE(TAG, "Tavily API returned %d via proxy", status);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ── Execute ──────────────────────────────────────────────────── */

/**
 * 函数名: tool_web_search_execute
 * 功能: 执行web搜索操作，支持Brave和Tavily两种搜索提供商
 * 参数:
 *      input_json - JSON格式输入，包含query参数
 *      output - 结果输出缓冲区
 *      output_size - 输出缓冲区大小
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_web_search_execute(const char *input_json, char *output, size_t output_size)
{
    if (s_provider == SEARCH_PROVIDER_NONE) {
        snprintf(output, output_size,
                 "Error: No search API key configured. Set MIMI_SECRET_TAVILY_KEY or MIMI_SECRET_SEARCH_KEY in mimi_secrets.h");
        return ESP_ERR_INVALID_STATE;
    }

    /* Parse input to get query */
    cJSON *input = cJSON_Parse(input_json);
    if (!input) {
        snprintf(output, output_size, "Error: Invalid input JSON");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *query = cJSON_GetObjectItem(input, "query");
    if (!query || !cJSON_IsString(query) || query->valuestring[0] == '\0') {
        cJSON_Delete(input);
        snprintf(output, output_size, "Error: Missing 'query' field");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Searching: %s", query->valuestring);

    /* Build URL/query fields */
    char encoded_query[256];
    url_encode(query->valuestring, encoded_query, sizeof(encoded_query));
    char query_copy[256];
    snprintf(query_copy, sizeof(query_copy), "%s", query->valuestring);
    cJSON_Delete(input);

    /* Allocate response buffer from PSRAM */
    search_buf_t sb = {0};
    sb.data = heap_caps_calloc(1, SEARCH_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!sb.data) {
        snprintf(output, output_size, "Error: Out of memory");
        return ESP_ERR_NO_MEM;
    }
    sb.cap = SEARCH_BUF_SIZE;

    /* Make HTTP request */
    esp_err_t err;
    if (s_provider == SEARCH_PROVIDER_TAVILY) {
        if (http_proxy_is_enabled()) {
            err = tavily_search_via_proxy(query_copy, &sb);
        } else {
            err = tavily_search_direct(query_copy, &sb);
        }
    } else {
        char path[384];
        snprintf(path, sizeof(path),
                 "/res/v1/web/search?q=%s&count=%d", encoded_query, SEARCH_RESULT_COUNT);
        if (http_proxy_is_enabled()) {
            err = brave_search_via_proxy(path, &sb);
        } else {
            char url[512];
            snprintf(url, sizeof(url), "https://api.search.brave.com%s", path);
            err = brave_search_direct(url, &sb);
        }
    }

    if (err != ESP_OK) {
        free(sb.data);
        snprintf(output, output_size, "Error: Search request failed");
        return err;
    }

    /* Parse and format results */
    cJSON *root = cJSON_Parse(sb.data);
    free(sb.data);

    if (!root) {
        snprintf(output, output_size, "Error: Failed to parse search results");
        return ESP_FAIL;
    }

    if (s_provider == SEARCH_PROVIDER_TAVILY) {
        format_tavily_results(root, output, output_size);
    } else {
        format_results(root, output, output_size);
    }
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Search complete, %d bytes result", (int)strlen(output));
    return ESP_OK;
}

/**
 * 函数名: tool_web_search_set_key
 * 功能: 设置Brave搜索API密钥并保存到NVS
 * 参数:
 *      api_key - Brave API密钥字符串
 * 返回值: ESP_OK始终返回成功
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_web_search_set_key(const char *api_key)
{
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(MIMI_NVS_SEARCH, NVS_READWRITE, &nvs));
    ESP_ERROR_CHECK(nvs_set_str(nvs, MIMI_NVS_KEY_API_KEY, api_key));
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);

    strncpy(s_brave_key, api_key, sizeof(s_brave_key) - 1);
    if (s_provider == SEARCH_PROVIDER_NONE) {
        s_provider = SEARCH_PROVIDER_BRAVE;
    }
    ESP_LOGI(TAG, "Search API key saved");
    return ESP_OK;
}

/**
 * 函数名: tool_web_search_set_tavily_key
 * 功能: 设置Tavily搜索API密钥并保存到NVS
 * 参数:
 *      api_key - Tavily API密钥字符串
 * 返回值: ESP_OK始终返回成功
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_web_search_set_tavily_key(const char *api_key)
{
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(MIMI_NVS_SEARCH, NVS_READWRITE, &nvs));
    ESP_ERROR_CHECK(nvs_set_str(nvs, MIMI_NVS_KEY_TAVILY_KEY, api_key));
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);

    strncpy(s_tavily_key, api_key, sizeof(s_tavily_key) - 1);
    s_provider = SEARCH_PROVIDER_TAVILY;
    ESP_LOGI(TAG, "Tavily API key saved");
    return ESP_OK;
}
