/**
 * @file tool_ir_ac.c
 * @brief 预置美的(Midea)/格力(Gree)空调红外码
 *
 * 协议参数来源于 IRremoteESP8266 库。
 * 在 ESP32 端离线编码 raw 波形，无需完整的 AC 协议状态机。
 *
 * 美的 (Midea) 协议:
 *   Carrier: 38kHz, Tick: 80us
 *   Header: 4480us mark + 4480us space
 *   Bit: 560us mark, 1680us space(1) / 560us space(0)
 *   Gap: 5600us
 *   48 bits, LSB first, 发送正常码后紧接收发取反码
 *
 * 格力 (Gree) 协议:
 *   Carrier: 38kHz
 *   Header: 9000us mark + 4500us space
 *   Bit: 620us mark, 1600us space(1) / 540us space(0)
 *   Gap: 19980us
 *   64 bits (8 bytes), 分为两个 32-bit block, block间有3-bit footer(010)+gap
 *
 * 作者: Lskipktw
 * 日期: 2026-06-04
 */

#include "tool_ir_ac.h"
#include "mimi_config.h"

#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include "cJSON.h"

static const char *TAG = "tool_ir_ac";

/* ── 美的时间参数 (us) ─────────────────────────────────────────── */
#define MIDEA_HDR_MARK   4480
#define MIDEA_HDR_SPACE  4480
#define MIDEA_BIT_MARK   560
#define MIDEA_ONE_SPACE  1680
#define MIDEA_ZERO_SPACE 560
#define MIDEA_GAP        5600
#define MIDEA_BITS       48

/* ── 格力时间参数 (us) ─────────────────────────────────────────── */
#define GREE_HDR_MARK    9000
#define GREE_HDR_SPACE   4500
#define GREE_BIT_MARK    620
#define GREE_ONE_SPACE   1600
#define GREE_ZERO_SPACE  540
#define GREE_GAP         19980
#define GREE_BITS        64

/* ── preset version is defined in tool_ir_ac.h ── */

/* ── 预置码条目 ────────────────────────────────────────────────── */
/* 每个 preset 包含 name, raw 数组, raw 数量 */
typedef struct {
    const char   *name;
    const uint16_t *raw;
    size_t         raw_count;
} ac_preset_t;

/* ================================================================
 *  协议编码函数（生成 raw pulse/space 数组）
 * ================================================================ */

/**
 * @brief 将一个字节按 LSB first 写入 raw 数组
 * @return 写入的 entry 数
 */
static size_t encode_byte(uint16_t *raw, uint8_t byte,
                          uint16_t mark, uint16_t one, uint16_t zero)
{
    size_t count = 0;
    for (int i = 0; i < 8; i++) {
        raw[count++] = mark;
        raw[count++] = (byte & (1u << i)) ? one : zero;
    }
    return count;
}

/**
 * @brief 美的 checksum: 前5字节位反转求和，256减，结果位反转
 */
static uint8_t midea_checksum(const uint8_t *state)
{
    uint32_t sum = 0;
    for (int i = 0; i < 5; i++) {
        uint8_t b = state[i];
        /* 位反转 */
        uint8_t rev = (uint8_t)(((b * 0x0802UL & 0x22110UL) |
                                  (b * 0x8020UL & 0x88440UL)) * 0x10101UL >> 16);
        sum += rev;
    }
    sum = (256 - (sum & 0xFF)) & 0xFF;
    /* 位反转结果 */
    uint8_t result = (uint8_t)(((sum * 0x0802UL & 0x22110UL) |
                                 (sum * 0x8020UL & 0x88440UL)) * 0x10101UL >> 16);
    return result;
}

/**
 * @brief 格力 checksum: XOR of all nibbles in bytes 0-6
 *
 * Matches IRremoteESP8266 IRGreeAC::calcChecksum:
 *   nibble_xor = (sum >> 4) ^ (sum & 0x0F)
 * Result is placed in byte 7 bits [7:4].
 */
static uint8_t gree_checksum(const uint8_t *state)
{
    uint8_t sum = 0;
    for (int i = 0; i < 7; i++) {
        sum ^= state[i];
    }
    /* XOR of upper and lower nibbles */
    return ((sum >> 4) ^ (sum & 0x0F));
}

/**
 * @brief 编码美的 48-bit 红外帧为 raw 波形
 *
 * Midea 发送方式: header + 48bits(LSB first) + 48bits(inverted, LSB first) + tail mark
 */
static size_t encode_midea(const uint8_t state[6],
                           uint16_t *raw, size_t raw_max)
{
    size_t idx = 0;

    /* Header */
    raw[idx++] = MIDEA_HDR_MARK;
    raw[idx++] = MIDEA_HDR_SPACE;

    /* 48 bits normal (LSB first, byte 5→0) */
    for (int byte = 0; byte < 6; byte++) {
        if (idx + 16 > raw_max) return 0;
        idx += encode_byte(raw + idx, state[byte],
                           MIDEA_BIT_MARK, MIDEA_ONE_SPACE, MIDEA_ZERO_SPACE);
    }

    /* 48 bits inverted (LSB first) */
    for (int byte = 0; byte < 6; byte++) {
        if (idx + 16 > raw_max) return 0;
        uint8_t inv = ~state[byte];
        idx += encode_byte(raw + idx, inv,
                           MIDEA_BIT_MARK, MIDEA_ONE_SPACE, MIDEA_ZERO_SPACE);
    }

    /* Trailing mark + gap */
    raw[idx++] = MIDEA_BIT_MARK;
    raw[idx++] = MIDEA_GAP;

    return idx;
}

/**
 * @brief 编码格力 64-bit (8字节) 红外帧为 raw 波形
 *
 * Gree 发送方式:
 *   header + 32bits(前4字节) + 3-bit footer(010) + gap
 *   + 32bits(后4字节) + 3-bit footer(010) + trailing mark + gap
 */
static size_t encode_gree(const uint8_t state[8],
                          uint16_t *raw, size_t raw_max)
{
    size_t idx = 0;

    /* Header */
    raw[idx++] = GREE_HDR_MARK;
    raw[idx++] = GREE_HDR_SPACE;

    /* Block 1: bytes 0-3 (32 bits, LSB first per byte) */
    for (int byte = 0; byte < 4; byte++) {
        if (idx + 16 > raw_max) return 0;
        idx += encode_byte(raw + idx, state[byte],
                           GREE_BIT_MARK, GREE_ONE_SPACE, GREE_ZERO_SPACE);
    }

    /* Block 1 footer: 010 pattern (3 bits) */
    /* bit 0=0: mark+zero, bit 1=1: mark+one, bit 2=0: mark+zero */
    raw[idx++] = GREE_BIT_MARK; raw[idx++] = GREE_ZERO_SPACE;  /* bit 0 = 0 */
    raw[idx++] = GREE_BIT_MARK; raw[idx++] = GREE_ONE_SPACE;   /* bit 1 = 1 */
    raw[idx++] = GREE_BIT_MARK; raw[idx++] = GREE_ZERO_SPACE;  /* bit 2 = 0 */
    /* trailing mark */
    raw[idx++] = GREE_BIT_MARK;
    raw[idx++] = GREE_GAP;  /* inter-block gap */

    /* Block 2: bytes 4-7 (32 bits, no header) */
    for (int byte = 4; byte < 8; byte++) {
        if (idx + 16 > raw_max) return 0;
        idx += encode_byte(raw + idx, state[byte],
                           GREE_BIT_MARK, GREE_ONE_SPACE, GREE_ZERO_SPACE);
    }

    /* Block 2 footer: 010 pattern */
    raw[idx++] = GREE_BIT_MARK; raw[idx++] = GREE_ZERO_SPACE;
    raw[idx++] = GREE_BIT_MARK; raw[idx++] = GREE_ONE_SPACE;
    raw[idx++] = GREE_BIT_MARK; raw[idx++] = GREE_ZERO_SPACE;

    /* Trailing mark + gap */
    raw[idx++] = GREE_BIT_MARK;
    raw[idx++] = GREE_GAP;

    return idx;
}

/* ================================================================
 *  构建美的/格力 state bytes
 * ================================================================ */

/**
 * @brief 构建美的 state (6 bytes)
 *
 * byte 0: checksum (后填)
 * byte 1: bit[7]=off_timer_disable, bit[6:0]=sensor_temp(0=off)
 * byte 2: bit[7]=on_timer_disable, bit[6]=beep, bit[5:0]=off_timer_hrs
 * byte 3: bit[7:4]=temp(17-30→0-13), bit[3:0]=0
 * byte 4: bit[7:5]=mode, bit[4:3]=fan, bit[2]=sleep, bit[1]=0, bit[0]=power
 * byte 5: bit[7:5]=type(001=command), bit[4:0]=header(10100)
 */
static void midea_build_state(uint8_t state[6], uint8_t mode, uint8_t temp,
                               uint8_t fan, bool power)
{
    memset(state, 0, 6);

    /* byte 1: sensor temp off (0x80 = 128 = sensor off) */
    state[1] = 0x80;  /* disableSensor=1, sensor temp=0 */

    /* byte 2: timers disabled = 0xC0 */
    state[2] = 0xC0;  /* off_timer_disable=1, on_timer_disable=1 */

    /* byte 3: temperature (17°C = 0, 30°C = 13) */
    if (temp < 17) temp = 17;
    if (temp > 30) temp = 30;
    state[3] = (uint8_t)((temp - 17) << 4);

    /* byte 4: mode[7:5] fan[4:3] sleep[2] power[0] */
    state[4] = (uint8_t)((mode << 5) | (fan << 3) | (power ? 1 : 0));

    /* byte 5: type=001(Command), header=10100 */
    state[5] = 0x14;  /* 0b00_010100 → type=0b001<<5=0x20|0x14=0x34 */
    /* Actually: type in bits[7:5], header in bits[4:0]
     * kMideaACTypeCommand = 0b001, header = 0b10100
     * = 0b001_10100 = 0x34 */
    state[5] = 0x34;

    /* byte 0: checksum */
    state[0] = midea_checksum(state);
}

/* 美的模式常量 */
#define MIDEA_MODE_COOL  0  /* 制冷 */
#define MIDEA_MODE_DRY   1  /* 除湿 */
#define MIDEA_MODE_AUTO  2  /* 自动 */
#define MIDEA_MODE_HEAT  3  /* 制热 */
#define MIDEA_MODE_FAN   4  /* 送风 */
#define MIDEA_FAN_AUTO   0
#define MIDEA_FAN_LOW    1
#define MIDEA_FAN_MED    2
#define MIDEA_FAN_HIGH   3

/**
 * @brief 构建格力 state (8 bytes)
 *
 * byte 0: fan[7:6] swing_auto[5] sleep[4] power[3] mode[2:0]
 *          mode: 0=auto,1=cool,2=dry,3=fan,4=heat,5=econo
 * byte 1: timer[7:4] temp[3:0]
 *          temp: 16°C=0, 30°C=14
 * byte 2: model_A[5] light[4] turbo[3] timer_hrs[3:0]
 * byte 3: useF[6] temp_extra[7] + padding=0x50 (unknown1=0b0101)
 * byte 4: swing_v[7:4] swing_h[3:0] (auto=1)
 * byte 5: wifi[3] unknown2=0b0100[6:4] feel[5] display_temp[7:6]
 * byte 6: reserved (0x00)
 * byte 7: checksum[7:4] econo[3] reserved[2:1]
 *
 * Checksum = (XOR of bytes 0-6 nibbles + 8) 补码的低4位 → 放在 byte7[7:4]
 */

/* 格力模式 */
#define GREE_MODE_AUTO  0
#define GREE_MODE_COOL  1
#define GREE_MODE_DRY   2
#define GREE_MODE_FAN   3
#define GREE_MODE_HEAT  4
/* 格力风扇 (byte 0 bits 7:6) */
#define GREE_FAN_AUTO   0
#define GREE_FAN_LOW    1
#define GREE_FAN_MED    2
#define GREE_FAN_HIGH   3

static void gree_build_state(uint8_t state[8], uint8_t mode, uint8_t temp,
                              uint8_t fan, bool power)
{
    memset(state, 0, 8);

    /* byte 0: mode[2:0] power[3] sleep[4] swing[5] fan[7:6] */
    state[0] = (uint8_t)(mode & 0x07);
    if (power) state[0] |= (1 << 3);
    state[0] |= (uint8_t)((fan & 0x03) << 6);
    /* Swing auto = bit 5 */
    state[0] |= (1 << 5);  /* default swing auto on */

    /* byte 1: temp[3:0] + bit4=0 */
    if (temp < 16) temp = 16;
    if (temp > 30) temp = 30;
    state[1] = (uint8_t)(temp - 16) & 0x0F;

    /* byte 2: reserved mostly */
    state[2] = 0x00;
    /* Light on = bit 4 */
    state[2] |= (1 << 4);

    /* byte 3: unknown1=0b0101 << 4 = 0x50 */
    /* Actually bit layout: tempExtra[7] useF[6] unknown1[5:0]=0b000101? */
    /* From the header: unknown1=4 bits value 0b0101, plus bits 7,6 for tempExtra/useF */
    /* So: bits[3:0]=0b0101=5, bits[7:6]=00, bits[5:4]=00 */
    state[3] = 0x05;

    /* byte 4: swing_v[7:4]=auto(1), swing_h[3:0]=off(0) */
    state[4] = 0x10;  /* swing_v auto=1 in high nibble */

    /* byte 5: display_temp[7:6]=set(1), feel[5]=off(0), unknown2[4:2]=0b100=4, wifi[3]=off(0)? */
    /* unknown2=3 bits 0b100 → bits[4:2] */
    state[5] = 0x40;  /* display set temp = 0b01 << 6 */

    /* byte 6: reserved */
    state[6] = 0x00;

    /* byte 7: checksum in high nibble */
    uint8_t cs = gree_checksum(state);
    state[7] = (uint8_t)(cs << 4);
}

/* ================================================================
 *  预置 raw 数据（静态分配，不动态malloc）
 * ================================================================ */

/* 最大 raw 条目: Midea重发版 ≈ 390, Gree ≈ 280 */
#define PRESET_RAW_LEN 400

/* ================================================================
 *  SPIFFS JSON 写入
 * ================================================================ */

/**
 * @brief 将一个 preset 条目写入 JSON array
 */
static void add_preset_to_json(cJSON *arr, const ac_preset_t *p)
{
    cJSON *obj = cJSON_CreateObject();

    cJSON_AddStringToObject(obj, "name", p->name);
    cJSON_AddNumberToObject(obj, "protocol", 1);  /* IR_PROTO_RAW = 1 */
    cJSON_AddNumberToObject(obj, "address", 0);
    cJSON_AddNumberToObject(obj, "command", 0);
    cJSON_AddNumberToObject(obj, "raw_count", (double)p->raw_count);

    cJSON *raw_arr = cJSON_CreateArray();
    for (size_t j = 0; j < p->raw_count; j++) {
        cJSON_AddItemToArray(raw_arr, cJSON_CreateNumber(p->raw[j]));
    }
    cJSON_AddItemToObject(obj, "raw", raw_arr);

    cJSON_AddItemToArray(arr, obj);
}

/* ================================================================
 *  公开 API
 * ================================================================ */

esp_err_t tool_ir_ac_load_presets(void)
{
    /* 检查 IR 代码文件是否已存在且版本匹配 */
    FILE *f = fopen(MIMI_IR_CODE_FILE, "r");
    if (f) {
        /* Read file to check version tag */
        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        bool version_ok = false;
        if (fsize > 0 && fsize <= 32768) {
            fseek(f, 0, SEEK_SET);
            char *buf = malloc(fsize + 1);
            if (buf) {
                fread(buf, 1, fsize, f);
                buf[fsize] = '\0';
                cJSON *root = cJSON_Parse(buf);
                if (root) {
                    cJSON *j_ver = cJSON_GetObjectItem(root, "preset_version");
                    if (cJSON_IsNumber(j_ver) && j_ver->valueint >= IR_AC_PRESET_VERSION) {
                        version_ok = true;
                    }
                    cJSON_Delete(root);
                }
                free(buf);
            }
        }
        fclose(f);
        if (version_ok) {
            ESP_LOGI(TAG, "IR code file exists (preset_version >= %d), skipping presets",
                     IR_AC_PRESET_VERSION);
            return ESP_OK;
        }
        ESP_LOGI(TAG, "IR code file has older preset version, regenerating...");
    }

    ESP_LOGI(TAG, "No IR code file found, writing AC presets...");

    /* ── 定义所有预置码 ──────────────────────────────────────── */
    uint8_t state[8];
    static uint16_t raw_buf[PRESET_RAW_LEN];

    /* 预置数量 */
    #define N_PRESETS 8
    static ac_preset_t presets[N_PRESETS];
    int pidx = 0;

    /* 美的: 制冷 26°C */
    midea_build_state(state, MIDEA_MODE_COOL, 26, MIDEA_FAN_AUTO, true);
    presets[pidx].name = "ac_midea_cool_26";
    presets[pidx].raw_count = encode_midea(state, raw_buf, PRESET_RAW_LEN);
    presets[pidx].raw = raw_buf;
    ESP_LOGI(TAG, "  %s: %d raw symbols", presets[pidx].name, (int)presets[pidx].raw_count);
    pidx++;

    /* 美的: 制冷 24°C */
    static uint16_t raw_buf2[PRESET_RAW_LEN];
    midea_build_state(state, MIDEA_MODE_COOL, 24, MIDEA_FAN_AUTO, true);
    presets[pidx].name = "ac_midea_cool_24";
    presets[pidx].raw_count = encode_midea(state, raw_buf2, PRESET_RAW_LEN);
    presets[pidx].raw = raw_buf2;
    ESP_LOGI(TAG, "  %s: %d raw symbols", presets[pidx].name, (int)presets[pidx].raw_count);
    pidx++;

    /* 美的: 制热 26°C */
    static uint16_t raw_buf3[PRESET_RAW_LEN];
    midea_build_state(state, MIDEA_MODE_HEAT, 26, MIDEA_FAN_AUTO, true);
    presets[pidx].name = "ac_midea_heat_26";
    presets[pidx].raw_count = encode_midea(state, raw_buf3, PRESET_RAW_LEN);
    presets[pidx].raw = raw_buf3;
    ESP_LOGI(TAG, "  %s: %d raw symbols", presets[pidx].name, (int)presets[pidx].raw_count);
    pidx++;

    /* 美的: 关机 */
    static uint16_t raw_buf4[PRESET_RAW_LEN];
    midea_build_state(state, MIDEA_MODE_COOL, 26, MIDEA_FAN_AUTO, false);
    presets[pidx].name = "ac_midea_off";
    presets[pidx].raw_count = encode_midea(state, raw_buf4, PRESET_RAW_LEN);
    presets[pidx].raw = raw_buf4;
    ESP_LOGI(TAG, "  %s: %d raw symbols", presets[pidx].name, (int)presets[pidx].raw_count);
    pidx++;

    /* 格力: 制冷 26°C */
    static uint16_t raw_buf5[PRESET_RAW_LEN];
    gree_build_state(state, GREE_MODE_COOL, 26, GREE_FAN_AUTO, true);
    presets[pidx].name = "ac_gree_cool_26";
    presets[pidx].raw_count = encode_gree(state, raw_buf5, PRESET_RAW_LEN);
    presets[pidx].raw = raw_buf5;
    ESP_LOGI(TAG, "  %s: %d raw symbols", presets[pidx].name, (int)presets[pidx].raw_count);
    pidx++;

    /* 格力: 制冷 24°C */
    static uint16_t raw_buf6[PRESET_RAW_LEN];
    gree_build_state(state, GREE_MODE_COOL, 24, GREE_FAN_AUTO, true);
    presets[pidx].name = "ac_gree_cool_24";
    presets[pidx].raw_count = encode_gree(state, raw_buf6, PRESET_RAW_LEN);
    presets[pidx].raw = raw_buf6;
    ESP_LOGI(TAG, "  %s: %d raw symbols", presets[pidx].name, (int)presets[pidx].raw_count);
    pidx++;

    /* 格力: 制热 26°C */
    static uint16_t raw_buf7[PRESET_RAW_LEN];
    gree_build_state(state, GREE_MODE_HEAT, 26, GREE_FAN_AUTO, true);
    presets[pidx].name = "ac_gree_heat_26";
    presets[pidx].raw_count = encode_gree(state, raw_buf7, PRESET_RAW_LEN);
    presets[pidx].raw = raw_buf7;
    ESP_LOGI(TAG, "  %s: %d raw symbols", presets[pidx].name, (int)presets[pidx].raw_count);
    pidx++;

    /* 格力: 关机 */
    static uint16_t raw_buf8[PRESET_RAW_LEN];
    gree_build_state(state, GREE_MODE_COOL, 26, GREE_FAN_AUTO, false);
    presets[pidx].name = "ac_gree_off";
    presets[pidx].raw_count = encode_gree(state, raw_buf8, PRESET_RAW_LEN);
    presets[pidx].raw = raw_buf8;
    ESP_LOGI(TAG, "  %s: %d raw symbols", presets[pidx].name, (int)presets[pidx].raw_count);
    pidx++;

    /* ── 构建 JSON ──────────────────────────────────────────── */
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "preset_version", IR_AC_PRESET_VERSION);
    cJSON *arr  = cJSON_CreateArray();

    for (int i = 0; i < pidx; i++) {
        add_preset_to_json(arr, &presets[i]);
    }

    cJSON_AddItemToObject(root, "codes", arr);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) {
        ESP_LOGE(TAG, "cJSON_Print failed");
        return ESP_FAIL;
    }

    f = fopen(MIMI_IR_CODE_FILE, "w");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s for writing", MIMI_IR_CODE_FILE);
        free(json_str);
        return ESP_FAIL;
    }

    size_t json_len = strlen(json_str);
    size_t written = fwrite(json_str, 1, json_len, f);
    fclose(f);
    free(json_str);

    if (written != json_len) {
        ESP_LOGE(TAG, "Write truncated: %d/%d bytes", (int)written, (int)json_len);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Wrote %d AC presets to %s (%d bytes)",
             pidx, MIMI_IR_CODE_FILE, (int)json_len);
    return ESP_OK;
}
