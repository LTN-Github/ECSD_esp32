#pragma once

#include "esp_err.h"

/* Bump this version whenever AC preset encoding logic changes
 * (e.g. checksum fixes), so existing SPIFFS files are regenerated. */
#define IR_AC_PRESET_VERSION  2

/**
 * @brief 将预置的美的/格力空调 IR 码写入 SPIFFS IR 代码库
 *
 * 只在 IR 代码文件不存在或版本过旧时写入，不覆盖用户学习的内容。
 *
 * @return ESP_OK 成功写入或文件已存在无需写入
 */
esp_err_t tool_ir_ac_load_presets(void);
