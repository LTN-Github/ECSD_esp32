#include "tools/tool_files.h"
#include "mimi_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "tool_files";

#define MAX_FILE_SIZE (32 * 1024)  /* 最大文件大小限制：32KB */

/**
 * 函数名: validate_path
 * 功能: 验证文件路径的安全性，确保路径以SPIFFS基础目录开头且不包含目录遍历攻击
 * 参数:
 *      path - 要验证的文件路径
 * 返回值: true表示路径安全有效，false表示路径无效或不安全
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
static bool validate_path(const char *path)
{
    if (!path) return false;
    size_t base_len = strlen(MIMI_SPIFFS_BASE);
    if (strncmp(path, MIMI_SPIFFS_BASE, base_len) != 0) return false;
    /* Require a path separator after the base (unless base ends with '/') */
    if (base_len > 0 && MIMI_SPIFFS_BASE[base_len - 1] != '/') {
        if (path[base_len] != '/') return false;
    }
    if (strstr(path, "..") != NULL) return false;
    return true;
}

/* ── read_file ─────────────────────────────────────────────── */

/**
 * 函数名: tool_read_file_execute
 * 功能: 执行文件读取操作，读取指定路径的文件内容
 * 参数:
 *      input_json - JSON格式输入，包含path参数
 *      output - 结果输出缓冲区，存储读取的文件内容
 *      output_size - 输出缓冲区大小
 * 返回值: ESP_OK成功，ESP_ERR_NOT_FOUND文件不存在，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_read_file_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
    if (!validate_path(path)) {
        snprintf(output, output_size, "Error: path must start with %s/ and must not contain '..'", MIMI_SPIFFS_BASE);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(output, output_size, "Error: file not found: %s", path);
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    size_t max_read = output_size - 1;
    if (max_read > MAX_FILE_SIZE) max_read = MAX_FILE_SIZE;

    size_t n = fread(output, 1, max_read, f);
    output[n] = '\0';
    fclose(f);

    ESP_LOGI(TAG, "read_file: %s (%d bytes)", path, (int)n);
    cJSON_Delete(root);
    return ESP_OK;
}

/* ── write_file ────────────────────────────────────────────── */

/**
 * 函数名: tool_write_file_execute
 * 功能: 执行文件写入操作，将内容写入指定路径的文件
 * 参数:
 *      input_json - JSON格式输入，包含path和content参数
 *      output - 结果输出缓冲区
 *      output_size - 输出缓冲区大小
 * 返回值: ESP_OK成功，ESP_FAIL写入失败，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_write_file_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
    const char *content = cJSON_GetStringValue(cJSON_GetObjectItem(root, "content"));

    if (!validate_path(path)) {
        snprintf(output, output_size, "Error: path must start with %s/ and must not contain '..'", MIMI_SPIFFS_BASE);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    if (!content) {
        snprintf(output, output_size, "Error: missing 'content' field");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        snprintf(output, output_size, "Error: cannot open file for writing: %s", path);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    fclose(f);

    if (written != len) {
        snprintf(output, output_size, "Error: wrote %d of %d bytes to %s", (int)written, (int)len, path);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    snprintf(output, output_size, "OK: wrote %d bytes to %s", (int)written, path);
    ESP_LOGI(TAG, "write_file: %s (%d bytes)", path, (int)written);
    cJSON_Delete(root);
    return ESP_OK;
}

/* ── edit_file ─────────────────────────────────────────────── */

/**
 * 函数名: tool_edit_file_execute
 * 功能: 执行文件编辑操作，查找并替换文件中的指定字符串
 * 参数:
 *      input_json - JSON格式输入，包含path、old_string和new_string参数
 *      output - 结果输出缓冲区
 *      output_size - 输出缓冲区大小
 * 返回值: ESP_OK成功，ESP_ERR_NOT_FOUND文件或字符串不存在，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_edit_file_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
    const char *old_str = cJSON_GetStringValue(cJSON_GetObjectItem(root, "old_string"));
    const char *new_str = cJSON_GetStringValue(cJSON_GetObjectItem(root, "new_string"));

    if (!validate_path(path)) {
        snprintf(output, output_size, "Error: path must start with %s/ and must not contain '..'", MIMI_SPIFFS_BASE);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    if (!old_str || !new_str) {
        snprintf(output, output_size, "Error: missing 'old_string' or 'new_string' field");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    /* Read existing file */
    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(output, output_size, "Error: file not found: %s", path);
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0 || file_size > MAX_FILE_SIZE) {
        snprintf(output, output_size, "Error: file too large or empty (%ld bytes)", file_size);
        fclose(f);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_SIZE;
    }

    /* Allocate buffer for the result (old content + possible expansion) */
    size_t old_len = strlen(old_str);
    size_t new_len = strlen(new_str);
    size_t max_result = file_size + (new_len > old_len ? new_len - old_len : 0) + 1;
    char *buf = malloc(file_size + 1);
    char *result = malloc(max_result);
    if (!buf || !result) {
        free(buf);
        free(result);
        fclose(f);
        snprintf(output, output_size, "Error: out of memory");
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    size_t n = fread(buf, 1, file_size, f);
    buf[n] = '\0';
    fclose(f);

    /* Find and replace first occurrence */
    char *pos = strstr(buf, old_str);
    if (!pos) {
        snprintf(output, output_size, "Error: old_string not found in %s", path);
        free(buf);
        free(result);
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    size_t prefix_len = pos - buf;
    memcpy(result, buf, prefix_len);
    memcpy(result + prefix_len, new_str, new_len);
    size_t suffix_start = prefix_len + old_len;
    size_t suffix_len = n - suffix_start;
    memcpy(result + prefix_len + new_len, buf + suffix_start, suffix_len);
    size_t total = prefix_len + new_len + suffix_len;
    result[total] = '\0';

    free(buf);

    /* Write back */
    f = fopen(path, "w");
    if (!f) {
        snprintf(output, output_size, "Error: cannot open file for writing: %s", path);
        free(result);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    fwrite(result, 1, total, f);
    fclose(f);
    free(result);

    snprintf(output, output_size, "OK: edited %s (replaced %d bytes with %d bytes)", path, (int)old_len, (int)new_len);
    ESP_LOGI(TAG, "edit_file: %s", path);
    cJSON_Delete(root);
    return ESP_OK;
}

/* ── list_dir ──────────────────────────────────────────────── */

/**
 * 函数名: tool_list_dir_execute
 * 功能: 执行目录列表操作，列出SPIFFS文件系统中的所有文件
 * 参数:
 *      input_json - JSON格式输入，可选prefix参数用于过滤文件
 *      output - 结果输出缓冲区
 *      output_size - 输出缓冲区大小
 * 返回值: ESP_OK成功，ESP_FAIL打开目录失败，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t tool_list_dir_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    const char *prefix = NULL;
    if (root) {
        cJSON *pfx = cJSON_GetObjectItem(root, "prefix");
        if (pfx && cJSON_IsString(pfx)) {
            prefix = pfx->valuestring;
        }
    }

    DIR *dir = opendir(MIMI_SPIFFS_BASE);
    if (!dir) {
        snprintf(output, output_size, "Error: cannot open %s directory", MIMI_SPIFFS_BASE);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    size_t off = 0;
    struct dirent *ent;
    int count = 0;

    while ((ent = readdir(dir)) != NULL && off < output_size - 1) {
        /* Build full path: SPIFFS entries are just filenames with embedded slashes */
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", MIMI_SPIFFS_BASE, ent->d_name);

        if (prefix && strncmp(full_path, prefix, strlen(prefix)) != 0) {
            continue;
        }

        off += snprintf(output + off, output_size - off, "%s\n", full_path);
        count++;
    }

    closedir(dir);

    if (count == 0) {
        snprintf(output, output_size, "(no files found)");
    }

    ESP_LOGI(TAG, "list_dir: %d files (prefix=%s)", count, prefix ? prefix : "(none)");
    cJSON_Delete(root);
    return ESP_OK;
}
