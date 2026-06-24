#pragma once

#include "esp_err.h"
#include <stdint.h>

/* ── Display Modes ──────────────────────────────────────────────── */
typedef enum {
    OLED_MODE_AUTO   = 0,  /* auto-rotate through top-level pages   */
    OLED_MODE_MANUAL = 1,  /* fixed page, 30s timeout → back to AUTO*/
    OLED_MODE_MENU   = 2,  /* button-driven navigation (reserved)    */
} oled_mode_t;

/* ── Public Page Indices ───────────────────────────────────────────
 * These match s_pages[] in oled_display.c.  Use these constants
 * with oled_display_set_page() for readable code.                  */
#define OLED_PAGE_SYSTEM        0
#define OLED_PAGE_WIFI_DETAIL   1
#define OLED_PAGE_MEM_DETAIL    2
#define OLED_PAGE_AI            3
#define OLED_PAGE_MODEL_INFO    4
#define OLED_PAGE_SESSION       5
#define OLED_PAGE_NETWORK       6
#define OLED_PAGE_ESPNOW_PEERS  7
#define OLED_PAGE_TASKS         8
#define OLED_PAGE_CRON_LIST     9
#define OLED_PAGE_HEARTBEAT     10
#define OLED_PAGE_MESSAGE       11
#define OLED_PAGE_COUNT         12   /* total number of pages */

/* ── Lifecycle ──────────────────────────────────────────────────── */

/**
 * @brief Initialize the OLED display, font subsystem, and start the
 *        refresh task.
 *
 * Must be called after SPIFFS is mounted (font file may be loaded).
 */
esp_err_t oled_display_init(void);

/* ── Navigation ─────────────────────────────────────────────────── */

/**
 * @brief Jump to a specific page and enter MANUAL mode.
 *
 * In MANUAL mode the display stays on this page for 30 seconds,
 * then automatically returns to AUTO mode (top-level rotation).
 * Calling again resets the timeout.
 *
 * @param page_index  0..OLED_PAGE_COUNT-1 (use OLED_PAGE_* constants)
 */
void oled_display_set_page(int page_index);

/**
 * @brief Set the display mode.
 *
 * @param mode  OLED_MODE_AUTO, OLED_MODE_MANUAL, or OLED_MODE_MENU
 */
void oled_display_set_mode(oled_mode_t mode);

/**
 * @brief Set the latest message text (displayed on the Message page).
 *
 * Thread-safe; may be called from any task.
 * @param msg  UTF-8 text (truncated to 255 chars)
 */
void oled_display_set_last_msg(const char *msg);

/* ── Query ──────────────────────────────────────────────────────── */

/** @return current page index (0..OLED_PAGE_COUNT-1) */
int  oled_display_get_page(void);

/** @return current page title string */
const char *oled_display_get_page_title(void);

/** @return current display mode */
oled_mode_t oled_display_get_mode(void);

/* ── Button Interface (reserved for future 4-button hardware) ───── */

/**
 * @brief Initialise GPIO inputs and ISR handlers for the 4 navigation
 *        buttons (UP, DOWN, SELECT, BACK).
 *
 * Buttons are active-low with internal pull-ups.  Pressing any button
 * triggers an ISR → debounce → calls oled_display_btn_*().
 *
 * @return ESP_OK on success
 */
esp_err_t oled_display_btn_init(void);

void oled_display_btn_up(void);
void oled_display_btn_down(void);
void oled_display_btn_select(void);
void oled_display_btn_back(void);
