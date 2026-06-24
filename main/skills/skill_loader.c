#include "skills/skill_loader.h"
#include "mimi_config.h"

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include "esp_log.h"

static const char *TAG = "skills";

/*
 * Skills are stored as markdown files in spiffs_data/skills/
 * and flashed into the SPIFFS partition at build time.
 */

/**
 * 函数名: skill_loader_init
 * 功能: 初始化技能系统，扫描SPIFFS中可用的技能markdown文件
 * 参数: 无
 * 返回值: ESP_OK成功，其他为错误码
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
esp_err_t skill_loader_init(void)
{
    ESP_LOGI(TAG, "Initializing skills system");

    DIR *dir = opendir(MIMI_SPIFFS_BASE);
    if (!dir) {
        ESP_LOGW(TAG, "Cannot open SPIFFS — skills may not be available");
        return ESP_OK;
    }

    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        const char *name = ent->d_name;
        size_t len = strlen(name);
        if (strncmp(name, "skills/", 7) == 0 && len > 10 &&
            strcmp(name + len - 3, ".md") == 0) {
            count++;
        }
    }
    closedir(dir);

    ESP_LOGI(TAG, "Skills system ready (%d skills on SPIFFS)", count);
    return ESP_OK;
}

/* ── Build skills summary for system prompt ──────────────────── */

/**
 * 函数名: extract_title
 * 功能: 解析第一行作为标题，期望格式为"# Title"
 * 参数:
 *      line - 输入行字符串
 *      len - 输入行长度
 *      out - 输出缓冲区，存储提取的标题（不含"# "前缀）
 *      out_size - 输出缓冲区大小
 * 返回值: 无
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
static void extract_title(const char *line, size_t len, char *out, size_t out_size)
{
    const char *start = line;
    if (len >= 2 && line[0] == '#' && line[1] == ' ') {
        start = line + 2;
        len -= 2;
    }

    /* Trim trailing whitespace/newline */
    while (len > 0 && (start[len - 1] == '\n' || start[len - 1] == '\r' || start[len - 1] == ' ')) {
        len--;
    }

    size_t copy = len < out_size - 1 ? len : out_size - 1;
    memcpy(out, start, copy);
    out[copy] = '\0';
}

/**
 * 函数名: extract_description
 * 功能: 提取描述文本，即第一行和第一个空行之间的文本
 * 参数:
 *      f - 文件指针
 *      out - 输出缓冲区，存储提取的描述
 *      out_size - 输出缓冲区大小
 * 返回值: 无
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
static void extract_description(FILE *f, char *out, size_t out_size)
{
    size_t off = 0;
    char line[256];

    while (fgets(line, sizeof(line), f) && off < out_size - 1) {
        size_t len = strlen(line);

        /* Stop at blank line or section header */
        if (len == 0 || (len == 1 && line[0] == '\n') ||
            (len >= 2 && line[0] == '#' && line[1] == '#')) {
            break;
        }

        /* Skip leading blank lines */
        if (off == 0 && line[0] == '\n') continue;

        /* Trim trailing newline for concatenation */
        if (line[len - 1] == '\n') {
            line[len - 1] = ' ';
        }

        size_t copy = len < out_size - off - 1 ? len : out_size - off - 1;
        memcpy(out + off, line, copy);
        off += copy;
    }

    /* Trim trailing space */
    while (off > 0 && out[off - 1] == ' ') off--;
    out[off] = '\0';
}

/**
 * 函数名: skill_loader_build_summary
 * 功能: 构建所有可用技能的摘要，用于系统提示
 * 参数:
 *      buf - 输出缓冲区
 *      size - 缓冲区大小
 * 返回值: 写入的字节数，无技能时返回0
 * 作者: Lskipktw
 * 日期: 2026-04-16  23:36:00
 */
size_t skill_loader_build_summary(char *buf, size_t size)
{
    DIR *dir = opendir(MIMI_SPIFFS_BASE);
    if (!dir) {
        ESP_LOGW(TAG, "Cannot open SPIFFS for skill enumeration");
        buf[0] = '\0';
        return 0;
    }

    size_t off = 0;
    struct dirent *ent;
    /* SPIFFS readdir returns filenames relative to the mount point (e.g. "skills/weather.md").
       We match entries that start with "skills/" and end with ".md". */
    const char *skills_subdir = "skills/";
    const size_t subdir_len = strlen(skills_subdir);

    while ((ent = readdir(dir)) != NULL && off < size - 1) {
        const char *name = ent->d_name;

        /* Match files under skills/ with .md extension */
        if (strncmp(name, skills_subdir, subdir_len) != 0) continue;

        size_t name_len = strlen(name);
        if (name_len < subdir_len + 4) continue;  /* at least "skills/x.md" */
        if (strcmp(name + name_len - 3, ".md") != 0) continue;

        /* Build full path */
        char full_path[296];
        snprintf(full_path, sizeof(full_path), "%s/%s", MIMI_SPIFFS_BASE, name);

        FILE *f = fopen(full_path, "r");
        if (!f) continue;

        /* Read first line for title */
        char first_line[128];
        if (!fgets(first_line, sizeof(first_line), f)) {
            fclose(f);
            continue;
        }

        char title[64];
        extract_title(first_line, strlen(first_line), title, sizeof(title));

        /* Read description (until blank line) */
        char desc[256];
        extract_description(f, desc, sizeof(desc));
        fclose(f);

        /* Append to summary */
        off += snprintf(buf + off, size - off,
            "- **%s**: %s (read with: read_file %s)\n",
            title, desc, full_path);
    }

    closedir(dir);

    buf[off] = '\0';
    ESP_LOGI(TAG, "Skills summary: %d bytes", (int)off);
    return off;
}
