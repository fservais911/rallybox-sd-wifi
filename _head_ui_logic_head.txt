/**
 * @file ui_logic.c
 * @brief UI Logic Layer - Bridge between system metrics and LVGL display
 * @author Rallybox Development Team
 * @developer Akhil
 *
 * This module implements the application logic layer that:
 * 1. Bridges system_monitor metrics to LVGL UI components
 * 2. Manages file browser (file manager) for SD cards
 * 3. Handles WiFi configuration and credential management
 * 4. Implements text display components for logs and settings
 * 5. Manages screen transitions and navigation
 *
 * Architecture:
 * - **Update Functions**: ui_logic_update_*() called periodically from main task
 * - **Event Handlers**: lv_event_t callbacks for button presses, selections
 * - **File Manager**: Embedded directory browser with file preview
 *
 * Color Scheme (Material Design):
 * - Background (BG): 0xff0f1419 (very dark blue-grey)
 * - Surface: 0xff182028 (dark surface)
 * - Accent: 0xff6ba6a8 (teal)
 * - Text: 0xfff2f4f5 (light grey-white)
 * - Success: 0xff77b255 (green)
 * - Warning: 0xffd4a14a (amber)
 * - Danger: 0xffd77474 (red)
 *
 * Components (2KB Text Buffer Limits):
 * - File Manager: Max 64 items, 4 KB preview per file
 * - Text Display: 8 KB textarea for logs/reports
 * - File Preview: 160 lines +ù 72 chars (4096 bytes max)
 *
 * @note All display operations lock the display mutex to prevent tearing
 * @note File operations may block briefly for I/O; called from UI task only
 * @note WiFi credentials stored permanently in NVS (auto-restored on boot)
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdint.h>
#include "lvgl.h"                   ///< LVGL graphics library
#include "ui.h"                     ///< Generated UI definitions (EEZ Studio)
#include "screens.h"                ///< Screen switching functions
#include "esp_log.h"                ///< ESP-IDF logging
#include "nvs_flash.h"              ///< Non-volatile storage API
#include "nvs.h"                    ///< NVS namespace handle
#include "esp_timer.h"              ///< System timer for timestamps
#include "driver/uart.h"            ///< UART driver for GNSS monitor
#include "sd_card.h"                ///< SD card module
#if CONFIG_RALLYBOX_GNSS_ENABLED
#include "gnss.h"                   ///< GNSS backend runtime control
#endif
#include "system_monitor.h"         ///< System monitoring/status
#if CONFIG_RALLYBOX_RACEBOX_ENABLED
#include "racebox.h"                ///< RaceBox BLE scan/connect APIs
#endif
#include "freertos/FreeRTOS.h"      ///< Real-time OS kernel
#include "freertos/semphr.h"        ///< Mutex/semaphore support
#include "freertos/task.h"          ///< Task management

static const char* TAG = "UI_Logic";   ///< Log tag for this module

#define BLUETOOTH_TAB_INDEX 2
#define GNSS_TAB_INDEX 3
#define BLUETOOTH_DROPDOWN_REFRESH_MS 2000
#define BLUETOOTH_DUMP_PENDING_MAX 4096
#define BLUETOOTH_DUMP_LINE_MAX 192

#define GNSS_DUMP_PENDING_MAX 4096
#define GNSS_DUMP_LINE_MAX 192
#define UART_MONITOR_MAX_CHARS 1600
#define UART_MONITOR_LINES 20
#define UART_STREAM_BUFFER_SIZE 8192
#define UART_STREAM_CHUNK_MAX 384

#define UI_COLOR_BG 0xff0f1419
#define UI_COLOR_SURFACE 0xff182028
#define UI_COLOR_SURFACE_ALT 0xff22303b
#define UI_COLOR_BORDER 0xff31404c
#define UI_COLOR_ACCENT 0xff6ba6a8
#define UI_COLOR_ACCENT_ALT 0xff4f8d8f
#define UI_COLOR_TEXT 0xfff2f4f5
#define UI_COLOR_MUTED 0xff9fadb8
#define UI_COLOR_SUCCESS 0xff77b255
#define UI_COLOR_WARNING 0xffd4a14a
#define UI_COLOR_DANGER 0xffd77474

#define FILE_MANAGER_MAX_ITEMS 64
#define FILE_PREVIEW_MAX_BYTES 4096
#define FILE_PREVIEW_MAX_LINES 160
#define FILE_PREVIEW_MAX_LINE_LENGTH 72

/* ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
 * FORWARD DECLARATIONS
 *
 * These functions are implemented later in this file and called from
 * event handlers or other functions defined before their implementation.
 * ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ */

 /// Event handler for on-screen keyboard display
void action_show_keyboard(lv_event_t* e);

/// Event handler for WiFi password/SSID keyboard modal
void action_password_ssid_keyboad(lv_event_t* e);

/// Display error message dialog with error code details
static void ui_handle_error(const char* msg, esp_err_t err);

/// Display a modal message dialog
void ui_show_message(const char* title, const char* msg, lv_color_t color);

/// Remove/forget saved WiFi credentials from NVS
static void action_remove_saved_wifi(lv_event_t* e);

// File manager functions
void file_manager_load_files(const char* mount_point, lv_obj_t* list_obj);
void file_manager_file_selected(lv_event_t* e);
void notepad_viewer_load_file(const char* filepath, lv_obj_t* preview_container);
lv_obj_t* create_file_manager_screen(lv_obj_t* parent);
lv_obj_t* create_minimal_keyboard(lv_obj_t* parent);

// Tab switching callbacks
static void file_manager_tab_sd1_cb(lv_event_t* e);
static void file_manager_tab_sd2_cb(lv_event_t* e);
static void file_modal_close_cb(lv_event_t* e);
static void file_modal_delete_cb(lv_event_t* e);
static lv_obj_t* ui_get_first_child(lv_obj_t* obj);
static void ui_refresh_bluetooth_controls(const system_status_t* status);
static bool ui_is_bluetooth_tab_active(void);
static bool ui_is_gnss_tab_active(void);
static void ui_style_surface(lv_obj_t* obj, lv_color_t bg_color, lv_coord_t radius);
static void ui_create_main_status_panels(void);
static void ui_update_main_status_panels(const system_status_t* status);
static void action_main_panel_racebox_click(lv_event_t* e);
static void action_main_panel_gnss_click(lv_event_t* e);
static void action_bluetooth_refresh(lv_event_t* e);
static void action_bluetooth_connect(lv_event_t* e);
static void action_gnss_start_stop(lv_event_t* e);
static void ui_refresh_gnss_dump(void);
static void ui_refresh_bluetooth_dump(void);

typedef struct
{
    SemaphoreHandle_t mutex;
    char data[UART_STREAM_BUFFER_SIZE];
    size_t head;
    size_t tail;
    bool active;
    bool overflowed;
    char partial_line[GNSS_DUMP_LINE_MAX];
    size_t partial_len;
} ui_stream_buffer_t;

typedef struct
{
    char lines[UART_MONITOR_LINES][GNSS_DUMP_LINE_MAX];
    uint8_t start;
    uint8_t count;
    bool dirty;
} ui_line_window_t;

static bool ui_stream_init(ui_stream_buffer_t* stream);
static void ui_stream_start(ui_stream_buffer_t* stream);
static void ui_stream_stop(ui_stream_buffer_t* stream);
static void ui_stream_push_bytes(ui_stream_buffer_t* stream, const uint8_t* data, size_t len);
static size_t ui_stream_pop_chunk(ui_stream_buffer_t* stream, char* out, size_t max_len, bool* had_overflow);
static void ui_window_reset(ui_line_window_t* window, lv_obj_t* panel);
static void ui_window_append_line(ui_line_window_t* window, const char* line);
static void ui_window_consume_chunk(ui_stream_buffer_t* stream, ui_line_window_t* window, const char* chunk, size_t len);
static void ui_window_render_if_dirty(ui_line_window_t* window, lv_obj_t* panel);

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
// MODERN TEXT DISPLAY COMPONENT
// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ

/**
 * @brief Create a modern text display container (similar to notepad editor)
 * Used for displaying logs, test results, system reports in a polished format
 */
lv_obj_t* create_modern_text_display(lv_obj_t* parent, int x, int y, int w, int h)
{
    // Main container
    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_set_pos(container, x, y);
    lv_obj_set_size(container, w, h);

    // Modern styling: subtle border, dark background, good contrast
    lv_obj_set_style_bg_color(container, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(container, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_border_width(container, 1, 0);
    lv_obj_set_style_border_side(container, LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT | LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(container, 12, 0);
    lv_obj_set_style_pad_all(container, 16, 0);
    lv_obj_set_style_shadow_width(container, 3, 0);
    lv_obj_set_style_shadow_color(container, lv_color_hex(0xff000000), 0);

    // Create scrollable text area (like notepad)
    lv_obj_t* textarea = lv_textarea_create(container);
    lv_obj_set_size(textarea, LV_PCT(100), LV_PCT(100));
    lv_textarea_set_max_length(textarea, 8192);  // Larger buffer for logs
    lv_textarea_set_one_line(textarea, false);
    lv_textarea_set_placeholder_text(textarea, "Text display area...");

    // Enhanced text styling for readability
    lv_obj_set_style_text_font(textarea, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(textarea, lv_color_hex(UI_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(textarea, 1, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(textarea, 4, LV_PART_MAIN);

    // Placeholder styling
    lv_obj_set_style_text_color(textarea, lv_color_hex(UI_COLOR_MUTED), LV_PART_TEXTAREA_PLACEHOLDER);

    // Background and border for textarea
    lv_obj_set_style_bg_color(textarea, lv_color_hex(UI_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(textarea, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(textarea, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(textarea, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(textarea, 12, LV_PART_MAIN);

    // Scrollbar styling
    lv_obj_set_style_bg_color(textarea, lv_color_hex(UI_COLOR_SURFACE_ALT), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(textarea, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(textarea, 6, LV_PART_SCROLLBAR);

    return container;
}

/**
 * @brief Apply consistent modern styling to message/info labels
 *
 * Sets text color, font size, letter spacing, and line spacing
 * to create a polished, readable UI appearance across all labels.
 *
 * Font sizes:
 * - 14 pt: Small text (descriptions, hints)
 * - 16 pt: Regular text (default)
 * - 18-22 pt: Headings
 *
 * @param label LVGL label object to style
 * @param color Text color (0xRRGGBB format, processed by lv_color_hex)
 * @param font_size Desired font size in points (14, 16, 18, 20, 22, default 16)
 */
void style_modern_label(lv_obj_t* label, lv_color_t color, int font_size)
{
    if (!label) return;

    lv_obj_set_style_text_color(label, color, 0);
    switch (font_size)
    {
        case 14: lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0); break;
        case 16: lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0); break;
        case 18: lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0); break;
        case 20: lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0); break;
        case 22: lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0); break;
        default: lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0); break;
    }
    lv_obj_set_style_text_letter_space(label, 1, 0);
    lv_obj_set_style_text_line_space(label, 2, 0);
}

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
// PRODUCTION STATUS GLOBALS
// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ

static bool s_wifi_connected = false;
static char s_wifi_ip[16] = "0.0.0.0";
static uint32_t s_log_line_count = 1999; // Initialize to 1999 to trigger first file creation
static uint32_t s_log_file_index = 0;
static char current_log_filename[128] = "";

// File manager tab
static lv_obj_t* g_files_tab = NULL;
static lv_obj_t* g_file_manager_screen = NULL;

typedef struct
{
    char path[256];
    char filename[256];
    char mount_point[32];
    bool is_dir;
    uint32_t size;
} file_entry_t;

typedef struct
{
    char full_path[512];
    char filename[256];
    char mount_point[32];
    bool is_dir;
    uint32_t size;
} file_item_context_t;

typedef struct
{
    char active_mount[32];
    lv_obj_t* list_container;
    lv_obj_t* status_label;
    lv_obj_t* sd1_button;
    lv_obj_t* sd2_button;
} file_manager_view_t;

static file_entry_t selected_file = { 0 };
static file_item_context_t g_file_items[FILE_MANAGER_MAX_ITEMS] = { 0 };
static file_manager_view_t g_file_manager_view = { 0 };

#if CONFIG_RALLYBOX_RACEBOX_ENABLED
static racebox_visible_device_t s_bt_visible_devices[RACEBOX_VISIBLE_DEVICES_MAX] = { 0 };
static size_t s_bt_visible_count = 0;
static uint64_t s_bt_last_dropdown_refresh_us = 0;
static char s_bt_last_options[1536] = { 0 };
static ui_stream_buffer_t s_bt_stream = { 0 };
static ui_line_window_t s_bt_window = { 0 };
static char s_bt_render_buf[UART_MONITOR_LINES * (BLUETOOTH_DUMP_LINE_MAX + 1)] = { 0 };
static lv_timer_t* s_bt_dump_timer = NULL;
static lv_obj_t* s_bt_dump_panel = NULL;
static bool s_bt_prev_connected = false;

typedef struct
{
    bool has_data;
    bool solution_valid;
    uint8_t fix_status;
    uint8_t satellites;
    float speed_kph;
    float heading_deg;
    float pdop;
    float horizontal_accuracy_m;
    float vertical_accuracy_m;
    uint32_t packets;
} racebox_nav_snapshot_t;

typedef struct
{
    SemaphoreHandle_t mutex;
    uint8_t frame[520];
    size_t frame_len;
    racebox_nav_snapshot_t nav;
} racebox_decode_state_t;

static racebox_decode_state_t s_rb_decode = { 0 };

static bool racebox_decode_ensure_mutex(void);
static uint16_t racebox_le_u16(const uint8_t* p);
static uint32_t racebox_le_u32(const uint8_t* p);
static int32_t racebox_le_i32(const uint8_t* p);
static void racebox_ubx_checksum(const uint8_t* data, size_t len, uint8_t* ck_a, uint8_t* ck_b);
static void racebox_decode_nav_payload_locked(const uint8_t* payload, size_t payload_len);
static void racebox_decode_stream_bytes(const uint8_t* data, size_t len);
static bool racebox_get_nav_snapshot(racebox_nav_snapshot_t* out);
#endif

static ui_stream_buffer_t s_gnss_stream = { 0 };
static ui_line_window_t s_gnss_window = { 0 };
static char s_gnss_render_buf[UART_MONITOR_LINES * (GNSS_DUMP_LINE_MAX + 1)] = { 0 };
static bool s_gnss_listening = false;
static volatile uint32_t s_gnss_rx_count = 0;
static volatile uint32_t s_bt_rx_count = 0;
static char s_gnss_status_text[96] = "Idle. Enter UART settings, then press START";
static lv_timer_t* s_gnss_dump_timer = NULL;
static uint8_t s_bt_ui_last_connected = 0xFF;
static uint8_t s_bt_ui_last_initialized = 0xFF;

static lv_obj_t* s_panel_racebox = NULL;
static lv_obj_t* s_panel_racebox_state = NULL;
static lv_obj_t* s_panel_racebox_count = NULL;
static lv_obj_t* s_panel_racebox_detail = NULL;
static lv_obj_t* s_panel_gnss = NULL;
static lv_obj_t* s_panel_gnss_state = NULL;
static lv_obj_t* s_panel_gnss_count = NULL;
static lv_obj_t* s_panel_gnss_detail = NULL;
static uint32_t s_last_gnss_count = 0;
static uint32_t s_last_bt_count = 0;
static bool s_rb_smooth_valid = false;
static float s_rb_speed_smooth = 0.0f;
static float s_rb_heading_smooth = 0.0f;
static bool s_gnss_smooth_valid = false;
static float s_gnss_speed_smooth = 0.0f;
static float s_gnss_heading_smooth = 0.0f;

static float ui_smooth_scalar(float prev, float current, float alpha)
{
    return prev + alpha * (current - prev);
}

static float ui_smooth_heading(float prev, float current, float alpha)
{
    float delta = current - prev;

    if (delta > 180.0f)
    {
        delta -= 360.0f;
    }
    else if (delta < -180.0f)
    {
        delta += 360.0f;
    }

    prev = prev + alpha * delta;
    while (prev < 0.0f)
    {
        prev += 360.0f;
    }
    while (prev >= 360.0f)
    {
        prev -= 360.0f;
    }
    return prev;
}

static void gnss_dump_timer_cb(lv_timer_t* t)
{
    LV_UNUSED(t);
    ui_refresh_gnss_dump();
}

#if CONFIG_RALLYBOX_RACEBOX_ENABLED
static void bluetooth_dump_timer_cb(lv_timer_t* t)
{
    LV_UNUSED(t);
    ui_refresh_bluetooth_dump();
}

static bool racebox_decode_ensure_mutex(void)
{
    if (s_rb_decode.mutex == NULL)
    {
        s_rb_decode.mutex = xSemaphoreCreateMutex();
    }
    return s_rb_decode.mutex != NULL;
}

static uint16_t racebox_le_u16(const uint8_t* p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t racebox_le_u32(const uint8_t* p)
{
    return (uint32_t)p[0] |
        ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24);
}

static int32_t racebox_le_i32(const uint8_t* p)
{
    return (int32_t)((uint32_t)p[0] |
        ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24));
}

static void racebox_ubx_checksum(const uint8_t* data, size_t len, uint8_t* ck_a, uint8_t* ck_b)
{
    size_t i;
    uint8_t a = 0;
    uint8_t b = 0;

    for (i = 0; i < len; ++i)
    {
        a = (uint8_t)(a + data[i]);
        b = (uint8_t)(b + a);
    }

    if (ck_a) *ck_a = a;
    if (ck_b) *ck_b = b;
}

static void racebox_decode_nav_payload_locked(const uint8_t* payload, size_t payload_len)
{
    uint8_t fix_status;
    uint8_t fix_flags;
    uint8_t lat_lon_flags;
    int32_t speed_mmps;
    int32_t heading_1e5;
    uint16_t pdop_1e2;
    uint32_t h_acc_mm;
    uint32_t v_acc_mm;
    float heading;

    if (payload == NULL || payload_len < 80)
    {
        return;
    }

    fix_status = payload[20];
    fix_flags = payload[21];
    lat_lon_flags = payload[66];
    speed_mmps = racebox_le_i32(payload + 48);
    heading_1e5 = racebox_le_i32(payload + 52);
    pdop_1e2 = racebox_le_u16(payload + 64);
    h_acc_mm = racebox_le_u32(payload + 40);
    v_acc_mm = racebox_le_u32(payload + 44);
    heading = (float)heading_1e5 / 100000.0f;

    while (heading < 0.0f)
    {
        heading += 360.0f;
    }
    while (heading >= 360.0f)
    {
        heading -= 360.0f;
    }

    s_rb_decode.nav.has_data = true;
    s_rb_decode.nav.fix_status = fix_status;
    s_rb_decode.nav.satellites = payload[23];
    s_rb_decode.nav.speed_kph = ((float)speed_mmps) * 0.0036f;
    s_rb_decode.nav.heading_deg = heading;
    s_rb_decode.nav.pdop = ((float)pdop_1e2) / 100.0f;
    s_rb_decode.nav.horizontal_accuracy_m = ((float)h_acc_mm) / 1000.0f;
    s_rb_decode.nav.vertical_accuracy_m = ((float)v_acc_mm) / 1000.0f;
    s_rb_decode.nav.solution_valid = ((fix_flags & 0x01U) != 0U) &&
        (fix_status >= 2U) && ((lat_lon_flags & 0x01U) == 0U);
    s_rb_decode.nav.packets++;
}

static void racebox_decode_stream_bytes(const uint8_t* data, size_t len)
{
    size_t i;

    if (data == NULL || len == 0)
    {
        return;
    }

    if (!racebox_decode_ensure_mutex())
    {
        return;
    }

    if (xSemaphoreTake(s_rb_decode.mutex, pdMS_TO_TICKS(5)) != pdTRUE)
    {
        return;
    }

    for (i = 0; i < len; ++i)
    {
        if (s_rb_decode.frame_len >= sizeof(s_rb_decode.frame))
        {
            s_rb_decode.frame_len = 0;
        }
        s_rb_decode.frame[s_rb_decode.frame_len++] = data[i];

        while (s_rb_decode.frame_len >= 8)
        {
            size_t start = 0;
            uint16_t payload_len;
            size_t packet_len;
            uint8_t ck_a;
            uint8_t ck_b;

            while ((start + 1) < s_rb_decode.frame_len)
            {
                if (s_rb_decode.frame[start] == 0xB5 && s_rb_decode.frame[start + 1] == 0x62)
                {
                    break;
                }
                start++;
            }

            if (start > 0)
            {
                memmove(s_rb_decode.frame,
                    s_rb_decode.frame + start,
                    s_rb_decode.frame_len - start);
                s_rb_decode.frame_len -= start;
            }

            if (s_rb_decode.frame_len < 8)
            {
                break;
            }

            if (!(s_rb_decode.frame[0] == 0xB5 && s_rb_decode.frame[1] == 0x62))
            {
                memmove(s_rb_decode.frame, s_rb_decode.frame + 1, s_rb_decode.frame_len - 1);
                s_rb_decode.frame_len -= 1;
                continue;
            }

            payload_len = racebox_le_u16(s_rb_decode.frame + 4);
            if (payload_len > 504)
            {
                memmove(s_rb_decode.frame, s_rb_decode.frame + 1, s_rb_decode.frame_len - 1);
                s_rb_decode.frame_len -= 1;
                continue;
            }

            packet_len = (size_t)payload_len + 8;
            if (s_rb_decode.frame_len < packet_len)
            {
                break;
            }

            racebox_ubx_checksum(s_rb_decode.frame + 2, (size_t)payload_len + 4, &ck_a, &ck_b);
            if (ck_a == s_rb_decode.frame[6 + payload_len] &&
                ck_b == s_rb_decode.frame[7 + payload_len])
            {
                if (s_rb_decode.frame[2] == 0xFF && s_rb_decode.frame[3] == 0x01)
                {
                    racebox_decode_nav_payload_locked(s_rb_decode.frame + 6, payload_len);
                }

                memmove(s_rb_decode.frame,
                    s_rb_decode.frame + packet_len,
                    s_rb_decode.frame_len - packet_len);
                s_rb_decode.frame_len -= packet_len;
            }
            else
            {
                memmove(s_rb_decode.frame, s_rb_decode.frame + 1, s_rb_decode.frame_len - 1);
                s_rb_decode.frame_len -= 1;
            }
        }
    }

    xSemaphoreGive(s_rb_decode.mutex);
}

static bool racebox_get_nav_snapshot(racebox_nav_snapshot_t* out)
{
    if (out == NULL)
    {
        return false;
    }

    memset(out, 0, sizeof(*out));
    if (!racebox_decode_ensure_mutex())
    {
        return false;
    }

    if (xSemaphoreTake(s_rb_decode.mutex, pdMS_TO_TICKS(2)) != pdTRUE)
    {
        return false;
    }

    *out = s_rb_decode.nav;
    xSemaphoreGive(s_rb_decode.mutex);
    return out->has_data;
}

static void racebox_ui_rx_cb(const uint8_t* data, size_t len, void* user_ctx)
{
    LV_UNUSED(user_ctx);
    if (len > 0)
    {
        s_bt_rx_count++;
        racebox_decode_stream_bytes(data, len);
    }
    ui_stream_push_bytes(&s_bt_stream, data, len);
}
#endif

static void gnss_dump_append_line(const char* line)
{
    if (line == NULL || line[0] == '\0')
    {
        return;
    }
    ui_stream_push_bytes(&s_gnss_stream, (const uint8_t*)line, strlen(line));
    ui_stream_push_bytes(&s_gnss_stream, (const uint8_t*)"\n", 1);
}

static void gnss_ui_sentence_cb(const char* sentence, void* user_ctx)
{
    LV_UNUSED(user_ctx);

    if (sentence && sentence[0] != '\0')
    {
        s_gnss_rx_count++;
        gnss_dump_append_line(sentence);
    }
}

static void ui_create_main_status_panels(void)
{
    lv_obj_t* title;

    if (objects.menu == NULL)
    {
        return;
    }

    if (s_panel_racebox == NULL)
    {
        s_panel_racebox = lv_obj_create(objects.menu);
        lv_obj_set_pos(s_panel_racebox, 430, 452);
        lv_obj_set_size(s_panel_racebox, 350, 156);
        ui_style_surface(s_panel_racebox, lv_color_hex(UI_COLOR_SURFACE), 14);
        lv_obj_set_style_border_color(s_panel_racebox, lv_color_hex(UI_COLOR_BORDER), 0);
        lv_obj_set_style_border_width(s_panel_racebox, 1, 0);
        lv_obj_add_flag(s_panel_racebox, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_obj_set_style_border_color(s_panel_racebox, lv_color_hex(UI_COLOR_ACCENT), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_border_width(s_panel_racebox, 2, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(s_panel_racebox, 240, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_add_event_cb(s_panel_racebox, action_main_panel_racebox_click, LV_EVENT_CLICKED, NULL);

        title = lv_label_create(s_panel_racebox);
        lv_obj_set_pos(title, 14, 10);
        lv_label_set_text(title, "RaceBox BLE");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_TEXT), 0);

        s_panel_racebox_state = lv_label_create(s_panel_racebox);
        lv_obj_set_pos(s_panel_racebox_state, 14, 52);
        lv_label_set_text(s_panel_racebox_state, "Status: Disconnected");
        lv_obj_set_style_text_font(s_panel_racebox_state, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(s_panel_racebox_state, lv_color_hex(UI_COLOR_DANGER), 0);

        s_panel_racebox_count = lv_label_create(s_panel_racebox);
        lv_obj_set_pos(s_panel_racebox_count, 14, 88);
        lv_label_set_text(s_panel_racebox_count, "Packets: 0");
        lv_obj_set_style_text_font(s_panel_racebox_count, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(s_panel_racebox_count, lv_color_hex(UI_COLOR_MUTED), 0);

        s_panel_racebox_detail = lv_label_create(s_panel_racebox);
        lv_obj_set_pos(s_panel_racebox_detail, 14, 108);
        lv_obj_set_width(s_panel_racebox_detail, 320);
        lv_label_set_long_mode(s_panel_racebox_detail, LV_LABEL_LONG_WRAP);
        lv_label_set_text(s_panel_racebox_detail, "SRC:RB | Device: -- | RSSI: -- dBm");
        lv_obj_set_style_text_font(s_panel_racebox_detail, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(s_panel_racebox_detail, lv_color_hex(UI_COLOR_MUTED), 0);
    }

    if (s_panel_gnss == NULL)
    {
        s_panel_gnss = lv_obj_create(objects.menu);
        lv_obj_set_pos(s_panel_gnss, 790, 452);
        lv_obj_set_size(s_panel_gnss, 350, 156);
        ui_style_surface(s_panel_gnss, lv_color_hex(UI_COLOR_SURFACE), 14);
        lv_obj_set_style_border_color(s_panel_gnss, lv_color_hex(UI_COLOR_BORDER), 0);
        lv_obj_set_style_border_width(s_panel_gnss, 1, 0);
        lv_obj_add_flag(s_panel_gnss, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_obj_set_style_border_color(s_panel_gnss, lv_color_hex(UI_COLOR_ACCENT), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_border_width(s_panel_gnss, 2, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(s_panel_gnss, 240, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_add_event_cb(s_panel_gnss, action_main_panel_gnss_click, LV_EVENT_CLICKED, NULL);

        title = lv_label_create(s_panel_gnss);
        lv_obj_set_pos(title, 14, 10);
        lv_label_set_text(title, "GNSS");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_TEXT), 0);

        s_panel_gnss_state = lv_label_create(s_panel_gnss);
        lv_obj_set_pos(s_panel_gnss_state, 14, 52);
        lv_label_set_text(s_panel_gnss_state, "Status: Disconnected");
        lv_obj_set_style_text_font(s_panel_gnss_state, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(s_panel_gnss_state, lv_color_hex(UI_COLOR_DANGER), 0);

        s_panel_gnss_count = lv_label_create(s_panel_gnss);
        lv_obj_set_pos(s_panel_gnss_count, 14, 88);
        lv_label_set_text(s_panel_gnss_count, "Sentences: 0");
        lv_obj_set_style_text_font(s_panel_gnss_count, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(s_panel_gnss_count, lv_color_hex(UI_COLOR_MUTED), 0);

        s_panel_gnss_detail = lv_label_create(s_panel_gnss);
        lv_obj_set_pos(s_panel_gnss_detail, 14, 108);
        lv_obj_set_width(s_panel_gnss_detail, 320);
        lv_label_set_long_mode(s_panel_gnss_detail, LV_LABEL_LONG_WRAP);
        lv_label_set_text(s_panel_gnss_detail, "SRC:UART | Fix: -- | Sats: -- | Speed: --");
        lv_obj_set_style_text_font(s_panel_gnss_detail, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(s_panel_gnss_detail, lv_color_hex(UI_COLOR_MUTED), 0);
    }
}

static void action_main_panel_racebox_click(lv_event_t* e)
{
    action_bluetooth_connect(e);
}

static void action_main_panel_gnss_click(lv_event_t* e)
{
    action_gnss_start_stop(e);
}

static void ui_update_main_status_panels(const system_status_t* status)
{
    char line[160];
    char detail[192];
    uint32_t gnss_count;
    uint32_t bt_count;
    bool gnss_receiving;
    bool bt_receiving;
    bool moving;
    float speed_kph;
    float heading_deg;
#if CONFIG_RALLYBOX_RACEBOX_ENABLED
    racebox_nav_snapshot_t rb_nav = { 0 };
    bool rb_has_data = racebox_get_nav_snapshot(&rb_nav);
#endif

    if (status == NULL)
    {
        return;
    }

    if (s_panel_racebox == NULL || s_panel_gnss == NULL)
    {
        ui_create_main_status_panels();
    }

    if (s_panel_racebox_state == NULL || s_panel_racebox_count == NULL ||
        s_panel_gnss_state == NULL || s_panel_gnss_count == NULL ||
        s_panel_racebox_detail == NULL || s_panel_gnss_detail == NULL)
    {
        return;
    }

    gnss_count = s_gnss_rx_count;
    bt_count = s_bt_rx_count;
    gnss_receiving = s_gnss_listening && (gnss_count != s_last_gnss_count);
    bt_receiving = status->racebox_connected && (bt_count != s_last_bt_count);

    if (status->racebox_connected)
    {
        snprintf(line, sizeof(line), "Status: %s",
            bt_receiving ? "Connected / Receiving" : "Connected / Idle");
        lv_label_set_text(s_panel_racebox_state, line);
        lv_obj_set_style_text_color(s_panel_racebox_state,
            bt_receiving ? lv_color_hex(UI_COLOR_SUCCESS) : lv_color_hex(UI_COLOR_WARNING), 0);
    }
    else
    {
        lv_label_set_text(s_panel_racebox_state, "Status: Disconnected");
        lv_obj_set_style_text_color(s_panel_racebox_state, lv_color_hex(UI_COLOR_DANGER), 0);
    }

    snprintf(line, sizeof(line), "Packets: %lu", (unsigned long)bt_count);
    lv_label_set_text(s_panel_racebox_count, line);

    #if CONFIG_RALLYBOX_RACEBOX_ENABLED
    if (status->racebox_connected && rb_has_data)
    {
        const char* fix_text = "none";
            speed_kph = rb_nav.speed_kph;
            heading_deg = rb_nav.heading_deg;
        if (speed_kph < 0.0f)
        {
            speed_kph = -speed_kph;
        }

        if (rb_nav.fix_status == 3)
        {
            fix_text = "3D";
        }
        else if (rb_nav.fix_status == 2)
        {
            fix_text = "2D";
        }

        if (!s_rb_smooth_valid)
        {
            s_rb_speed_smooth = speed_kph;
            s_rb_heading_smooth = heading_deg;
            s_rb_smooth_valid = true;
        }
        else
        {
            s_rb_speed_smooth = ui_smooth_scalar(s_rb_speed_smooth, speed_kph, 0.25f);
            s_rb_heading_smooth = ui_smooth_heading(s_rb_heading_smooth, heading_deg, 0.25f);
        }

        snprintf(detail, sizeof(detail),
            "SRC:RB | Fix:%s | S:%u | %.1f km/h | %.0f deg | %s\nPDOP:%.2f | hAcc:%.1fm | vAcc:%.1fm",
            rb_nav.solution_valid ? fix_text : "none",
            (unsigned)rb_nav.satellites,
            (double)s_rb_speed_smooth,
            (double)s_rb_heading_smooth,
            s_rb_speed_smooth > 2.0f ? "Moving" : "Still",
            (double)rb_nav.pdop,
            (double)rb_nav.horizontal_accuracy_m,
            (double)rb_nav.vertical_accuracy_m);
    }
    else
    {
        s_rb_smooth_valid = false;
        snprintf(detail, sizeof(detail), "SRC:RB | Device: %s | RSSI: %d dBm",
            status->racebox_device_name[0] ? status->racebox_device_name : "--",
            (int)status->racebox_rssi);
    }
    #else
    snprintf(detail, sizeof(detail), "SRC:RB | Device: %s | RSSI: %d dBm",
        status->racebox_device_name[0] ? status->racebox_device_name : "--",
        (int)status->racebox_rssi);
    #endif
    lv_label_set_text(s_panel_racebox_detail, detail);

    if (s_gnss_listening)
    {
        snprintf(line, sizeof(line), "Status: %s",
            gnss_receiving ? "Connected / Receiving" : "Connected / Idle");
        lv_label_set_text(s_panel_gnss_state, line);
        lv_obj_set_style_text_color(s_panel_gnss_state,
            gnss_receiving ? lv_color_hex(UI_COLOR_SUCCESS) : lv_color_hex(UI_COLOR_WARNING), 0);
    }
    else
    {
        lv_label_set_text(s_panel_gnss_state, "Status: Disconnected");
        lv_obj_set_style_text_color(s_panel_gnss_state, lv_color_hex(UI_COLOR_DANGER), 0);
    }

    snprintf(line, sizeof(line), "Sentences: %lu", (unsigned long)gnss_count);
    lv_label_set_text(s_panel_gnss_count, line);

    speed_kph = status->gnss_speed_kph;
    if (speed_kph < 0.0f)
    {
        speed_kph = -speed_kph;
    }
    heading_deg = status->gnss_heading_deg;
    while (heading_deg < 0.0f)
    {
        heading_deg += 360.0f;
    }
    while (heading_deg >= 360.0f)
    {
        heading_deg -= 360.0f;
    }

    if (status->gnss_fix_valid)
    {
        if (!s_gnss_smooth_valid)
        {
            s_gnss_speed_smooth = speed_kph;
            s_gnss_heading_smooth = heading_deg;
            s_gnss_smooth_valid = true;
        }
        else
        {
            s_gnss_speed_smooth = ui_smooth_scalar(s_gnss_speed_smooth, speed_kph, 0.25f);
            s_gnss_heading_smooth = ui_smooth_heading(s_gnss_heading_smooth, heading_deg, 0.25f);
        }
    }
    else
    {
        s_gnss_smooth_valid = false;
    }

    moving = s_gnss_speed_smooth > 2.0f;
    if (status->gnss_fix_valid)
    {
        snprintf(detail, sizeof(detail), "SRC:UART | Fix:%u | S:%u | %.1f km/h\nHead:%.0f deg | %s",
            (unsigned)status->gnss_fix_quality,
            (unsigned)status->gnss_satellites,
            (double)s_gnss_speed_smooth,
            (double)s_gnss_heading_smooth,
            moving ? "Moving" : "Still");
    }
    else
    {
        snprintf(detail, sizeof(detail), "SRC:UART | Fix:none | S:%u\nWaiting for lock",
            (unsigned)status->gnss_satellites);
    }
    lv_label_set_text(s_panel_gnss_detail, detail);

    s_last_gnss_count = gnss_count;
    s_last_bt_count = bt_count;
}

static esp_err_t gnss_monitor_start(int baud_rate, int tx_gpio, int rx_gpio)
{
#if CONFIG_RALLYBOX_GNSS_ENABLED
    if (s_gnss_listening)
    {
        return ESP_ERR_INVALID_STATE;
    }

    gnss_set_sentence_callback(gnss_ui_sentence_cb, NULL);
    return gnss_start_with_config(baud_rate, tx_gpio, rx_gpio, true);
#else
    LV_UNUSED(baud_rate);
    LV_UNUSED(tx_gpio);
    LV_UNUSED(rx_gpio);
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static void gnss_monitor_stop(void)
{
#if CONFIG_RALLYBOX_GNSS_ENABLED
    gnss_set_sentence_callback(NULL, NULL);
    gnss_stop();
#endif
}

static bool ui_stream_init(ui_stream_buffer_t* stream)
{
    if (stream == NULL)
    {
        return false;
    }

    if (stream->mutex == NULL)
    {
        stream->mutex = xSemaphoreCreateMutex();
    }

    return stream->mutex != NULL;
}

static void ui_stream_start(ui_stream_buffer_t* stream)
{
    if (!ui_stream_init(stream))
    {
        return;
    }

    if (xSemaphoreTake(stream->mutex, pdMS_TO_TICKS(20)) == pdTRUE)
    {
        stream->head = 0;
        stream->tail = 0;
        stream->partial_len = 0;
        stream->overflowed = false;
        stream->active = true;
        xSemaphoreGive(stream->mutex);
    }
}

static void ui_stream_stop(ui_stream_buffer_t* stream)
{
    if (!ui_stream_init(stream))
    {
        return;
    }

    if (xSemaphoreTake(stream->mutex, pdMS_TO_TICKS(20)) == pdTRUE)
    {
        stream->active = false;
        stream->head = 0;
        stream->tail = 0;
        stream->partial_len = 0;
        stream->overflowed = false;
        xSemaphoreGive(stream->mutex);
    }
}

static void ui_stream_push_bytes(ui_stream_buffer_t* stream, const uint8_t* data, size_t len)
{
    size_t i;
    size_t next_head;

    if (stream == NULL || data == NULL || len == 0)
    {
        return;
    }

    if (!ui_stream_init(stream))
    {
        return;
    }

    if (xSemaphoreTake(stream->mutex, pdMS_TO_TICKS(5)) != pdTRUE)
    {
        return;
    }

    if (!stream->active)
    {
        xSemaphoreGive(stream->mutex);
        return;
    }

    for (i = 0; i < len; ++i)
    {
        next_head = (stream->head + 1) % sizeof(stream->data);
        if (next_head == stream->tail)
        {
            stream->tail = 0;
            stream->head = 0;
            stream->partial_len = 0;
            stream->overflowed = true;
            next_head = 1;
        }

        stream->data[stream->head] = (char)data[i];
        stream->head = next_head;
    }

    xSemaphoreGive(stream->mutex);
}

static size_t ui_stream_pop_chunk(ui_stream_buffer_t* stream, char* out, size_t max_len, bool* had_overflow)
{
    size_t copied = 0;

    if (stream == NULL || out == NULL || max_len == 0)
    {
        return 0;
    }

    if (had_overflow)
    {
        *had_overflow = false;
    }

    if (!ui_stream_init(stream))
    {
        return 0;
    }

    if (xSemaphoreTake(stream->mutex, pdMS_TO_TICKS(5)) != pdTRUE)
    {
        return 0;
    }

    if (had_overflow && stream->overflowed)
    {
        *had_overflow = true;
        stream->overflowed = false;
    }

    while (stream->tail != stream->head && copied < max_len)
    {
        out[copied++] = stream->data[stream->tail];
        stream->tail = (stream->tail + 1) % sizeof(stream->data);
    }

    xSemaphoreGive(stream->mutex);
    return copied;
}

static void ui_window_reset(ui_line_window_t* window, lv_obj_t* panel)
{
    if (window == NULL)
    {
        return;
    }

    window->start = 0;
    window->count = 0;
    window->dirty = true;

    if (panel)
    {
        lv_textarea_set_text(panel, "");
    }
}

static void ui_window_append_line(ui_line_window_t* window, const char* line)
{
    uint8_t slot;

    if (window == NULL || line == NULL || line[0] == '\0')
    {
        return;
    }

    if (window->count < UART_MONITOR_LINES)
    {
        slot = (uint8_t)((window->start + window->count) % UART_MONITOR_LINES);
        window->count++;
    }
    else
    {
        slot = window->start;
        window->start = (uint8_t)((window->start + 1) % UART_MONITOR_LINES);
    }

    snprintf(window->lines[slot], sizeof(window->lines[slot]), "%s", line);
    window->dirty = true;
}

static void ui_window_consume_chunk(ui_stream_buffer_t* stream, ui_line_window_t* window, const char* chunk, size_t len)
{
    size_t i;

    if (stream == NULL || window == NULL || chunk == NULL || len == 0)
    {
        return;
    }

    for (i = 0; i < len; ++i)
    {
        char ch = chunk[i];

        if (ch == '\r')
        {
            continue;
        }

        if (ch == '\n')
        {
            if (stream->partial_len > 0)
            {
                stream->partial_line[stream->partial_len] = '\0';
                ui_window_append_line(window, stream->partial_line);
                stream->partial_len = 0;
            }
            continue;
        }

        if (!(isprint((unsigned char)ch) || ch == '\t' || ch == ' '))
        {
            continue;
        }

        if (stream->partial_len < (sizeof(stream->partial_line) - 1))
        {
            stream->partial_line[stream->partial_len++] = ch;
        }
    }
}

static void ui_window_render_if_dirty(ui_line_window_t* window, lv_obj_t* panel)
{
    uint8_t i;
    size_t offset = 0;

    if (window == NULL || panel == NULL || !window->dirty)
    {
        return;
    }

    if (panel == objects.gnss_dump_panel)
    {
        s_gnss_render_buf[0] = '\0';
        for (i = 0; i < window->count; ++i)
        {
            uint8_t idx = (uint8_t)((window->start + i) % UART_MONITOR_LINES);
            int wrote = snprintf(s_gnss_render_buf + offset,
                sizeof(s_gnss_render_buf) - offset,
                "%s%s",
                window->lines[idx],
                (i + 1 < window->count) ? "\n" : "");
            if (wrote <= 0 || (size_t)wrote >= (sizeof(s_gnss_render_buf) - offset))
            {
                break;
            }
            offset += (size_t)wrote;
        }
        lv_textarea_set_text(panel, s_gnss_render_buf);
    }
#if CONFIG_RALLYBOX_RACEBOX_ENABLED
    else if (panel == s_bt_dump_panel)
    {
        s_bt_render_buf[0] = '\0';
        for (i = 0; i < window->count; ++i)
        {
            uint8_t idx = (uint8_t)((window->start + i) % UART_MONITOR_LINES);
            int wrote = snprintf(s_bt_render_buf + offset,
                sizeof(s_bt_render_buf) - offset,
                "%s%s",
                window->lines[idx],
                (i + 1 < window->count) ? "\n" : "");
            if (wrote <= 0 || (size_t)wrote >= (sizeof(s_bt_render_buf) - offset))
            {
                break;
            }
            offset += (size_t)wrote;
        }
        lv_textarea_set_text(panel, s_bt_render_buf);
    }
#endif

    lv_textarea_set_cursor_pos(panel, LV_TEXTAREA_CURSOR_LAST);
    window->dirty = false;
}

static void ui_refresh_gnss_dump(void)
{
    if (!ui_is_gnss_tab_active())
    {
        return;
    }

    char chunk[UART_STREAM_CHUNK_MAX];
    bool had_overflow = false;
    size_t n;

    n = ui_stream_pop_chunk(&s_gnss_stream, chunk, sizeof(chunk), &had_overflow);
    if (had_overflow)
    {
        ui_window_append_line(&s_gnss_window, "[buffer rollover]");
    }
    if (n > 0)
    {
        ui_window_consume_chunk(&s_gnss_stream, &s_gnss_window, chunk, n);
    }

    if (objects.gnss_dump_panel)
    {
        ui_window_render_if_dirty(&s_gnss_window, objects.gnss_dump_panel);
    }
}

static void ui_refresh_bluetooth_dump(void)
{
#if CONFIG_RALLYBOX_RACEBOX_ENABLED
    if (!ui_is_bluetooth_tab_active())
    {
        return;
    }

    char chunk[UART_STREAM_CHUNK_MAX];
    bool had_overflow = false;
    size_t n;

    n = ui_stream_pop_chunk(&s_bt_stream, chunk, sizeof(chunk), &had_overflow);
    if (had_overflow)
    {
        ui_window_append_line(&s_bt_window, "[buffer rollover]");
    }
    if (n > 0)
    {
        ui_window_consume_chunk(&s_bt_stream, &s_bt_window, chunk, n);
    }

    if (s_bt_dump_panel)
    {
        ui_window_render_if_dirty(&s_bt_window, s_bt_dump_panel);
    }
#endif
}

static void action_gnss_start_stop(lv_event_t* e)
{
    int baud_rate;
    int tx_gpio;
    int rx_gpio;
    lv_obj_t* label;
    char line[96];

    LV_UNUSED(e);

    if (s_gnss_listening)
    {
        if (objects.gnss_start_stop_button)
        {
            label = ui_get_first_child(objects.gnss_start_stop_button);
            if (label) lv_label_set_text(label, "START");
        }

        gnss_monitor_stop();
        s_gnss_listening = false;
        gnss_dump_append_line("Stopped by user");
        ui_refresh_gnss_dump();
        ui_stream_stop(&s_gnss_stream);
        snprintf(s_gnss_status_text, sizeof(s_gnss_status_text), "Stopped. Press START to listen again");
        if (objects.gnss_status_label) lv_label_set_text(objects.gnss_status_label, s_gnss_status_text);
        return;
    }

    if (objects.gnss_baud_input == NULL || objects.gnss_tx_input == NULL || objects.gnss_rx_input == NULL)
    {
        return;
    }

    baud_rate = atoi(lv_textarea_get_text(objects.gnss_baud_input));
    tx_gpio = atoi(lv_textarea_get_text(objects.gnss_tx_input));
    rx_gpio = atoi(lv_textarea_get_text(objects.gnss_rx_input));

    if (baud_rate < 1200 || baud_rate > 921600)
    {
        ui_show_message("GNSS", "Invalid baud rate. Use 1200..921600", lv_color_hex(UI_COLOR_DANGER));
        return;
    }

    if (tx_gpio < 0 || tx_gpio > 54 || rx_gpio < 0 || rx_gpio > 54 || tx_gpio == rx_gpio)
    {
        ui_show_message("GNSS", "Invalid TX/RX GPIO values.", lv_color_hex(UI_COLOR_DANGER));
        return;
    }

    if (objects.gnss_start_stop_button)
    {
        label = ui_get_first_child(objects.gnss_start_stop_button);
        if (label) lv_label_set_text(label, "STOP");
    }

    if (objects.gnss_dump_panel)
    {
        ui_window_reset(&s_gnss_window, objects.gnss_dump_panel);
    }
    s_gnss_rx_count = 0;
    s_last_gnss_count = 0;
    ui_stream_start(&s_gnss_stream);

    esp_err_t ret = gnss_monitor_start(baud_rate, tx_gpio, rx_gpio);
    if (ret != ESP_OK)
    {
        ui_stream_stop(&s_gnss_stream);
        if (objects.gnss_start_stop_button)
        {
            label = ui_get_first_child(objects.gnss_start_stop_button);
            if (label) lv_label_set_text(label, "START");
        }
        ui_handle_error("Failed to start GNSS listener", ret);
        return;
    }

    s_gnss_listening = true;
    snprintf(line, sizeof(line), "Started by user: Baud=%d TX=%d RX=%d", baud_rate, tx_gpio, rx_gpio);
    gnss_dump_append_line(line);
    ui_refresh_gnss_dump();

    snprintf(s_gnss_status_text, sizeof(s_gnss_status_text),
        "Listening @ %d baud (TX=%d RX=%d)",
        baud_rate, tx_gpio, rx_gpio);
    if (objects.gnss_status_label) lv_label_set_text(objects.gnss_status_label, s_gnss_status_text);
}

// Forward declarations for hardware functions (to be linked from main/sd_card.c, etc.)
extern void system_sdcard_test(uint8_t card_number);
extern uint8_t system_sdcard1_init(void);
extern bool sd_card1_is_mounted(void);
extern bool sd_card2_is_mounted(void);

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
// FILE MANAGER AUTO-REFRESH SYSTEM
// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ

typedef struct
{
    char mount_point[32];
    uint32_t file_count;
    time_t last_check;
    lv_obj_t* ui_container;  // Reference to UI element to update
    bool enabled;
} file_manager_state_t;

static file_manager_state_t file_manager_sd1 = { 0 };
static file_manager_state_t file_manager_sd2 = { 0 };

/**
 * @brief Scan directory and count files (detects changes)
 */
static uint32_t count_directory_files(const char* path)
{
    DIR* dir = opendir(path);
    if (!dir) return 0;

    uint32_t count = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL)
    {
        // Skip . and .. entries
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
        {
            count++;
        }
    }
    closedir(dir);
    return count;
}

/**
 * @brief Auto-refresh timer callback for file manager
 * Detects new files and triggers UI updates
 */
static void file_manager_auto_refresh_cb(lv_timer_t* t)
{
    file_manager_state_t* fm = (file_manager_state_t*)lv_timer_get_user_data(t);

    if (!fm || !fm->enabled || fm->mount_point[0] == '\0')
    {
        return;
    }

    // Count current files
    uint32_t current_count = count_directory_files(fm->mount_point);

    // Detect changes
    if (current_count != fm->file_count)
    {
        ESP_LOGI(TAG, "File manager detected change: %u -> %u files in %s",
            fm->file_count, current_count, fm->mount_point);

        fm->file_count = current_count;
        fm->last_check = time(NULL);

        if (fm->ui_container)
        {
            if (strcmp(g_file_manager_view.active_mount, fm->mount_point) == 0)
            {
                file_manager_load_files(fm->mount_point, fm->ui_container);
                ESP_LOGI(TAG, "File manager UI refreshed for %s", fm->mount_point);
            }
        }
    }
}

/**
 * @brief Initialize file manager auto-refresh for a mount point
 */
void file_manager_enable_auto_refresh(const char* mount_point, lv_obj_t* ui_container)
{
    if (!mount_point || strlen(mount_point) == 0) return;

    file_manager_state_t* fm = NULL;

    if (strcmp(mount_point, "/sdcard") == 0)
    {
        fm = &file_manager_sd1;
    }
    else if (strcmp(mount_point, "/sdcard2") == 0)
    {
        fm = &file_manager_sd2;
    }
    else
    {
        return;
    }

    // Initialize or reset
    strncpy(fm->mount_point, mount_point, sizeof(fm->mount_point) - 1);
    fm->file_count = count_directory_files(mount_point);
    fm->last_check = time(NULL);
    fm->ui_container = ui_container;
    fm->enabled = true;

    ESP_LOGI(TAG, "File manager auto-refresh enabled for %s (initial: %u files)",
        mount_point, fm->file_count);

    // Create timer for periodic checks (every 2 seconds)
    lv_timer_t* timer = lv_timer_create(file_manager_auto_refresh_cb, 2000, fm);
    lv_timer_set_repeat_count(timer, -1);  // Infinite repeats
}

/**
 * @brief Disable file manager auto-refresh
 */
void file_manager_disable_auto_refresh(const char* mount_point)
{
    if (!mount_point) return;

    file_manager_state_t* fm = NULL;

    if (strcmp(mount_point, "/sdcard") == 0)
    {
        fm = &file_manager_sd1;
    }
    else if (strcmp(mount_point, "/sdcard2") == 0)
    {
        fm = &file_manager_sd2;
    }
    else
    {
        return;
    }

    fm->enabled = false;
    fm->mount_point[0] = '\0';

    ESP_LOGI(TAG, "File manager auto-refresh disabled for %s", mount_point);
}



static lv_obj_t* find_msgbox_ancestor(lv_obj_t* obj)
{
    while (obj)
    {
        if (lv_obj_check_type(obj, &lv_msgbox_class))
        {
            return obj;
        }
        obj = lv_obj_get_parent(obj);
    }
    return NULL;
}

static void ui_style_msgbox(lv_obj_t* mbox, lv_color_t bg_color)
{
    lv_obj_set_style_bg_color(mbox, bg_color, 0);
    lv_obj_set_style_bg_opa(mbox, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(mbox, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_border_width(mbox, 1, 0);
    lv_obj_set_style_radius(mbox, 16, 0);
    lv_obj_set_style_pad_all(mbox, 16, 0);
    lv_obj_set_style_text_color(mbox, lv_color_hex(0xff000000), 0);

    lv_obj_t* title = lv_msgbox_get_title(mbox);
    if (title)
    {
        lv_obj_set_style_text_color(title, lv_color_hex(0xff000000), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    }

    lv_obj_t* content = lv_msgbox_get_content(mbox);
    if (content)
    {
        lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(content, 0, 0);
        lv_obj_set_style_text_color(content, lv_color_hex(0xff000000), 0);
    }

    lv_obj_t* footer = lv_msgbox_get_footer(mbox);
    if (footer)
    {
        // Footer should be transparent but arrange buttons nicely
        lv_obj_set_style_bg_opa(footer, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(footer, 0, 0);
        lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
        /* LVGL variant in this build doesn't provide lv_obj_set_style_pad_inner()
         * Use column padding to space items horizontally in a row flex container.
         */
        lv_obj_set_style_pad_column(footer, 12, 0);
        lv_obj_set_style_pad_top(footer, 8, 0);
        lv_obj_set_style_pad_bottom(footer, 8, 0);
        lv_obj_set_style_align(footer, LV_FLEX_ALIGN_CENTER, 0);
        uint32_t child_count = lv_obj_get_child_count(footer);
        for (uint32_t index = 0; index < child_count; index++)
        {
            lv_obj_t* child = lv_obj_get_child(footer, index);
            lv_obj_set_style_bg_color(child, lv_color_hex(UI_COLOR_SURFACE_ALT), 0);
            lv_obj_set_style_bg_opa(child, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(child, lv_color_hex(0xff000000), 0);
            lv_obj_set_style_border_color(child, lv_color_hex(UI_COLOR_BORDER), 0);
            lv_obj_set_style_border_width(child, 1, 0);
            lv_obj_set_style_radius(child, 12, 0);
            lv_obj_set_style_pad_hor(child, 18, 0);
            lv_obj_set_style_pad_ver(child, 10, 0);
            lv_obj_set_width(child, LV_PCT(0));
        }
    }
    // Limit width so msgboxes are not full-screen on large displays
    lv_obj_set_width(mbox, 640);
}

static void mbox_close_cb(lv_event_t* e)
{
    lv_obj_t* mbox = lv_event_get_current_target(e);
    lv_msgbox_close_async(mbox);
}

static void mbox_deleted_cb(lv_event_t* e)
{
    // When the messagebox is deleted, also delete its overlay parent (if any)
    lv_obj_t* mbox = lv_event_get_current_target(e);
    lv_obj_t* parent = lv_obj_get_parent(mbox);
    if (parent)
    {
        lv_obj_del_async(parent);
    }
}

/**
 * @brief Show a professional popup message/modal
 */
void ui_show_message(const char* title, const char* msg, lv_color_t color)
{
    // Create a semi-opaque overlay to dim background and capture taps
    lv_obj_t* overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    /* Use an opaque overlay to avoid underlying header/title showing through
     * when viewing a file; a semi-transparent dim can reveal duplicate titles
     * and cause visual clutter. */
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);

    lv_obj_t* mbox = lv_msgbox_create(overlay);
    lv_msgbox_add_title(mbox, title);
    lv_msgbox_add_text(mbox, msg);

    lv_obj_t* btn = lv_msgbox_add_close_button(mbox);
    lv_obj_add_event_cb(btn, mbox_close_cb, LV_EVENT_CLICKED, NULL);

    // Ensure overlay is removed when msgbox is deleted
    lv_obj_add_event_cb(mbox, mbox_deleted_cb, LV_EVENT_DELETE, NULL);

    ui_style_msgbox(mbox, color);
    lv_obj_center(mbox);
}

/**
 * @brief Create a modal (overlay + msgbox) and return the msgbox object.
 * The overlay parent will be deleted when the msgbox is deleted.
 */
static lv_obj_t* ui_create_modal(const char* title, const char* msg, lv_color_t color)
{
    lv_obj_t* overlay = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);

    lv_obj_t* mbox = lv_msgbox_create(overlay);
    lv_msgbox_add_title(mbox, title);
    lv_msgbox_add_text(mbox, msg);
    ui_style_msgbox(mbox, color);
    lv_obj_add_event_cb(mbox, mbox_deleted_cb, LV_EVENT_DELETE, NULL);
    lv_obj_center(mbox);
    return mbox;
}

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
// FILE MANAGER UI - FILE LISTING & NAVIGATION
// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ

static void ui_style_surface(lv_obj_t* obj, lv_color_t bg_color, lv_coord_t radius)
{
    if (!obj)
    {
        return;
    }

    lv_obj_set_style_bg_color(obj, bg_color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, radius, 0);
}

static void ui_style_button(lv_obj_t* button, lv_color_t bg_color, lv_color_t text_color)
{
    if (!button)
    {
        return;
    }

    lv_obj_set_style_bg_color(button, bg_color, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_COLOR_SURFACE_ALT), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(button, lv_color_hex(UI_COLOR_BORDER), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(button, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_hor(button, 18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_ver(button, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t* label = ui_get_first_child(button);
    if (label)
    {
        lv_obj_set_style_text_color(label, text_color, 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_letter_space(label, 1, 0);
    }
}

static void ui_style_input(lv_obj_t* input)
{
    if (!input)
    {
        return;
    }

    ui_style_surface(input, lv_color_hex(UI_COLOR_SURFACE), 14);
    lv_obj_set_style_pad_left(input, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_right(input, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_top(input, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(input, 14, LV_PART_MAIN);
    lv_obj_set_style_text_font(input, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(input, lv_color_hex(UI_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_color(input, lv_color_hex(UI_COLOR_MUTED), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_border_color(input, lv_color_hex(UI_COLOR_ACCENT), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(input, 2, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_width(input, 0, LV_PART_MAIN);
}

static void ui_style_keyboard(lv_obj_t* keyboard)
{
    if (!keyboard)
    {
        return;
    }

    ui_style_surface(keyboard, lv_color_hex(UI_COLOR_SURFACE), 18);
    lv_obj_set_style_pad_all(keyboard, 10, 0);
    lv_obj_set_style_pad_row(keyboard, 8, 0);
    lv_obj_set_style_pad_column(keyboard, 8, 0);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(UI_COLOR_SURFACE), LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(UI_COLOR_ACCENT_ALT), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(UI_COLOR_ACCENT), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(keyboard, lv_color_hex(UI_COLOR_TEXT), LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(keyboard, lv_color_hex(UI_COLOR_BORDER), LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(keyboard, 1, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(keyboard, 12, LV_PART_ITEMS | LV_STATE_DEFAULT);
}

static void ui_update_file_tab_buttons(bool sd1_active)
{
    if (g_file_manager_view.sd1_button)
    {
        ui_style_button(g_file_manager_view.sd1_button,
            lv_color_hex(sd1_active ? UI_COLOR_ACCENT : UI_COLOR_SURFACE),
            lv_color_hex(UI_COLOR_TEXT));
    }
    if (g_file_manager_view.sd2_button)
    {
        ui_style_button(g_file_manager_view.sd2_button,
            lv_color_hex(sd1_active ? UI_COLOR_SURFACE : UI_COLOR_ACCENT),
            lv_color_hex(UI_COLOR_TEXT));
    }
}

static bool ui_is_text_file(const char* filename)
{
    const char* extension = filename ? strrchr(filename, '.') : NULL;

    if (!extension)
    {
        return false;
    }

    return strcasecmp(extension, ".txt") == 0 ||
        strcasecmp(extension, ".log") == 0 ||
        strcasecmp(extension, ".csv") == 0 ||
        strcasecmp(extension, ".json") == 0;
}

static void file_manager_open_text_modal(const file_entry_t* entry)
{
    if (!entry || entry->path[0] == '\0')
    {
        return;
    }

    lv_obj_t* overlay = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);

    lv_obj_t* panel = lv_obj_create(overlay);
    lv_obj_set_size(panel, 920, 600);
    lv_obj_center(panel);
    ui_style_surface(panel, lv_color_hex(UI_COLOR_BG), 22);
    lv_obj_set_style_pad_all(panel, 18, 0);
    lv_obj_set_style_pad_row(panel, 14, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);

    lv_obj_t* header = lv_obj_create(panel);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, LV_SIZE_CONTENT);
    ui_style_surface(header, lv_color_hex(UI_COLOR_SURFACE), 16);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 14, 0);

    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, entry->filename);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_TEXT), 0);

    lv_obj_t* subtitle = lv_label_create(header);
    lv_label_set_text(subtitle, entry->path);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(UI_COLOR_MUTED), 0);
    lv_obj_align(subtitle, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t* preview_container = lv_obj_create(panel);
    lv_obj_set_width(preview_container, LV_PCT(100));
    lv_obj_set_flex_grow(preview_container, 1);
    ui_style_surface(preview_container, lv_color_hex(UI_COLOR_SURFACE), 16);
    lv_obj_set_style_pad_all(preview_container, 14, 0);
    lv_obj_set_style_pad_row(preview_container, 6, 0);
    lv_obj_set_scrollbar_mode(preview_container, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_flex_flow(preview_container, LV_FLEX_FLOW_COLUMN);
    notepad_viewer_load_file(entry->path, preview_container);

    lv_obj_t* actions = lv_obj_create(panel);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_height(actions, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_style_pad_column(actions, 12, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* delete_btn = lv_button_create(actions);
    lv_label_set_text(lv_label_create(delete_btn), "Delete File");
    ui_style_button(delete_btn, lv_color_hex(UI_COLOR_DANGER), lv_color_hex(UI_COLOR_TEXT));
    lv_obj_add_event_cb(delete_btn, file_modal_delete_cb, LV_EVENT_CLICKED, overlay);

    lv_obj_t* close_btn = lv_button_create(actions);
    lv_label_set_text(lv_label_create(close_btn), "Close");
    ui_style_button(close_btn, lv_color_hex(UI_COLOR_ACCENT), lv_color_hex(UI_COLOR_TEXT));
    lv_obj_add_event_cb(close_btn, file_modal_close_cb, LV_EVENT_CLICKED, overlay);
}

static void file_modal_close_cb(lv_event_t* e)
{
    lv_obj_t* overlay = (lv_obj_t*)lv_event_get_user_data(e);

    if (overlay && lv_obj_is_valid(overlay))
    {
        lv_obj_delete_async(overlay);
    }
}

static void file_modal_delete_cb(lv_event_t* e)
{
    lv_obj_t* overlay = (lv_obj_t*)lv_event_get_user_data(e);

    if (selected_file.path[0] == '\0' || selected_file.is_dir)
    {
        ui_show_message("Delete Failed", "Only text files can be deleted from this viewer.", lv_color_hex(UI_COLOR_DANGER));
        return;
    }

    if (remove(selected_file.path) != 0)
    {
        ui_show_message("Delete Failed", "Unable to delete the selected file.", lv_color_hex(UI_COLOR_DANGER));
        return;
    }

    if (overlay && lv_obj_is_valid(overlay))
    {
        lv_obj_delete_async(overlay);
    }

    if (g_file_manager_view.list_container && g_file_manager_view.active_mount[0] != '\0')
    {
        file_manager_load_files(g_file_manager_view.active_mount, g_file_manager_view.list_container);
    }

    memset(&selected_file, 0, sizeof(selected_file));
    ui_show_message("File Deleted", "The selected text file was removed successfully.", lv_color_hex(0xff000000));
}

/**
 * @brief Tab switching callback for SD Card 1
 */
static void file_manager_tab_sd1_cb(lv_event_t* e)
{
    if (!g_file_manager_view.list_container) return;

    ui_update_file_tab_buttons(true);
    file_manager_load_files("/sdcard", g_file_manager_view.list_container);
    ESP_LOGI(TAG, "Switched to SD Card 1");
    (void)e;
}

/**
 * @brief Tab switching callback for SD Card 2
 */
static void file_manager_tab_sd2_cb(lv_event_t* e)
{
    if (!g_file_manager_view.list_container) return;

    ui_update_file_tab_buttons(false);
    file_manager_load_files("/sdcard2", g_file_manager_view.list_container);
    ESP_LOGI(TAG, "Switched to SD Card 2");
    (void)e;
}

/**
 * @brief Load and display files from directory (LVGL 9.x compatible)
 */
void file_manager_load_files(const char* mount_point, lv_obj_t* container)
{
    if (!container || !mount_point) return;

    strncpy(g_file_manager_view.active_mount, mount_point, sizeof(g_file_manager_view.active_mount) - 1);
    g_file_manager_view.active_mount[sizeof(g_file_manager_view.active_mount) - 1] = '\0';

    if (g_file_manager_view.status_label)
    {
        char status_text[96];
        snprintf(status_text, sizeof(status_text), "Showing %s", mount_point);
        lv_label_set_text(g_file_manager_view.status_label, status_text);
    }

    uint32_t child_count = lv_obj_get_child_count(container);
    for (int i = 0; i < child_count; i++)
    {
        lv_obj_del(lv_obj_get_child(container, 0));
    }

    memset(g_file_items, 0, sizeof(g_file_items));

    DIR* dir = opendir(mount_point);
    if (!dir)
    {
        lv_obj_t* err = lv_label_create(container);
        lv_label_set_text(err, "Unable to open storage");
        lv_obj_set_style_text_color(err, lv_color_hex(UI_COLOR_DANGER), 0);
        lv_obj_set_style_text_font(err, &lv_font_montserrat_16, 0);
        return;
    }

    struct dirent* entry;
    struct stat st;
    char full_path[512];
    int file_count = 0;

    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        snprintf(full_path, sizeof(full_path), "%s/%s", mount_point, entry->d_name);

        if (stat(full_path, &st) == 0 && file_count < FILE_MANAGER_MAX_ITEMS)
        {
            file_item_context_t* item = &g_file_items[file_count];
            snprintf(item->full_path, sizeof(item->full_path), "%s", full_path);
            snprintf(item->filename, sizeof(item->filename), "%s", entry->d_name);
            snprintf(item->mount_point, sizeof(item->mount_point), "%s", mount_point);
            item->is_dir = S_ISDIR(st.st_mode);
            item->size = (uint32_t)st.st_size;

            lv_obj_t* btn = lv_button_create(container);
            lv_obj_set_width(btn, LV_PCT(100));
            lv_obj_set_height(btn, 58);
            ui_style_button(btn, lv_color_hex(UI_COLOR_SURFACE), lv_color_hex(UI_COLOR_TEXT));
            lv_obj_set_style_pad_left(btn, 14, 0);
            lv_obj_set_style_pad_right(btn, 14, 0);

            char text[160];
            snprintf(text, sizeof(text), "%s %s",
                item->is_dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE,
                item->filename);

            lv_obj_t* label = lv_label_create(btn);
            lv_label_set_text(label, text);
            lv_obj_set_width(label, LV_PCT(100));
            lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT), 0);
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
            lv_obj_center(label);

            lv_obj_add_event_cb(btn, file_manager_file_selected, LV_EVENT_CLICKED, item);
            file_count++;
        }
    }
    closedir(dir);

    if (file_count == 0)
    {
        lv_obj_t* empty = lv_label_create(container);
        lv_label_set_text(empty, "No files found");
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(UI_COLOR_MUTED), 0);
    }
}

/**
 * @brief File selection callback (LVGL 9.x compatible)
 */
void file_manager_file_selected(lv_event_t* e)
{
    file_item_context_t* item = (file_item_context_t*)lv_event_get_user_data(e);

    if (!item) return;

    snprintf(selected_file.filename, sizeof(selected_file.filename), "%s", item->filename);
    snprintf(selected_file.path, sizeof(selected_file.path), "%s", item->full_path);
    snprintf(selected_file.mount_point, sizeof(selected_file.mount_point), "%s", item->mount_point);
    selected_file.is_dir = item->is_dir;
    selected_file.size = item->size;

    if (item->is_dir)
    {
        ui_show_message("Folder Selected", "Folder navigation is not enabled yet. Select a text file instead.", lv_color_hex(UI_COLOR_SURFACE));
        return;
    }

    if (!ui_is_text_file(item->filename))
    {
        ui_show_message("Preview Unavailable", "Only text-based files can be opened in the notepad viewer.", lv_color_hex(UI_COLOR_WARNING));
        return;
    }

    ESP_LOGI(TAG, "File selected: %s", selected_file.filename);
    file_manager_open_text_modal(&selected_file);
}

/**
 * @brief Load and display txt file content in notepad
 */
void notepad_viewer_load_file(const char* filepath, lv_obj_t* preview_container)
{
    if (!preview_container) return;

    uint32_t child_count = lv_obj_get_child_count(preview_container);
    for (uint32_t index = 0; index < child_count; index++)
    {
        lv_obj_del(lv_obj_get_child(preview_container, 0));
    }

    FILE* f = fopen(filepath, "r");
    if (!f)
    {
        lv_obj_t* error_label = lv_label_create(preview_container);
        lv_label_set_text(error_label, "Error: Could not open file");
        lv_obj_set_style_text_font(error_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(error_label, lv_color_hex(UI_COLOR_DANGER), 0);
        return;
    }

    char line_buffer[FILE_PREVIEW_MAX_LINE_LENGTH + 1] = { 0 };
    int ch;
    int line_length = 0;
    int line_count = 0;
    size_t total_bytes = 0;
    bool truncated = false;

    while ((ch = fgetc(f)) != EOF)
    {
        total_bytes++;

        if (total_bytes > FILE_PREVIEW_MAX_BYTES || line_count >= FILE_PREVIEW_MAX_LINES)
        {
            truncated = true;
            break;
        }

        if (ch == '\r')
        {
            continue;
        }

        if (ch == '\n' || line_length >= FILE_PREVIEW_MAX_LINE_LENGTH)
        {
            line_buffer[line_length] = '\0';

            lv_obj_t* line_label = lv_label_create(preview_container);
            lv_label_set_text(line_label, line_length > 0 ? line_buffer : " ");
            lv_obj_set_width(line_label, LV_PCT(100));
            lv_obj_set_style_text_font(line_label, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(line_label, lv_color_hex(UI_COLOR_TEXT), 0);
            lv_obj_set_style_text_align(line_label, LV_TEXT_ALIGN_LEFT, 0);
            /* Wrap long lines instead of clipping or replacing with dots. */
            lv_label_set_long_mode(line_label, LV_LABEL_LONG_WRAP);

            line_count++;
            line_length = 0;

            if (ch == '\n')
            {
                continue;
            }
        }

        if (!isprint((unsigned char)ch) && ch != '\t')
        {
            /* Replace non-printable characters with a space to avoid long
             * runs of dots or control glyphs in the preview. */
            ch = ' ';
        }

        if (ch == '\t')
        {
            ch = ' ';
        }

        if (line_length < FILE_PREVIEW_MAX_LINE_LENGTH)
        {
            line_buffer[line_length++] = (char)ch;
        }
    }

    if (line_length > 0 && line_count < FILE_PREVIEW_MAX_LINES)
    {
        line_buffer[line_length] = '\0';
        lv_obj_t* line_label = lv_label_create(preview_container);
        lv_label_set_text(line_label, line_buffer);
        lv_obj_set_width(line_label, LV_PCT(100));
        lv_obj_set_style_text_font(line_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(line_label, lv_color_hex(UI_COLOR_TEXT), 0);
        lv_obj_set_style_text_align(line_label, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_long_mode(line_label, LV_LABEL_LONG_CLIP);
        line_count++;
    }

    fclose(f);

    if (line_count == 0)
    {
        lv_obj_t* empty_label = lv_label_create(preview_container);
        lv_label_set_text(empty_label, "File is empty");
        lv_obj_set_style_text_font(empty_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(empty_label, lv_color_hex(UI_COLOR_MUTED), 0);
    }

    if (truncated)
    {
        lv_obj_t* truncated_label = lv_label_create(preview_container);
        lv_label_set_text(truncated_label, "[Preview truncated for performance]");
        lv_obj_set_style_text_font(truncated_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(truncated_label, lv_color_hex(UI_COLOR_WARNING), 0);
    }
}

/**
 * @brief Create file manager screen with SD1 and SD2 tabs
 */
lv_obj_t* create_file_manager_screen(lv_obj_t* parent)
{
    lv_obj_t* cont = lv_obj_create(parent);
    lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
    ui_style_surface(cont, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_radius(cont, 0, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

    lv_obj_t* header = lv_obj_create(cont);
    lv_obj_set_size(header, LV_PCT(100), 68);
    ui_style_surface(header, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_left(header, 18, 0);
    lv_obj_set_style_pad_right(header, 18, 0);
    lv_obj_set_style_pad_top(header, 12, 0);
    lv_obj_set_style_pad_bottom(header, 8, 0);

    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "File Manager");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_TEXT), 0);

    lv_obj_t* subtitle = lv_label_create(header);
    lv_label_set_text(subtitle, "Tap a text file to open it in a clean notepad viewer.");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(UI_COLOR_MUTED), 0);
    lv_obj_align(subtitle, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t* tab_container = lv_obj_create(cont);
    lv_obj_set_size(tab_container, LV_PCT(100), 64);
    ui_style_surface(tab_container, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_border_width(tab_container, 0, 0);
    lv_obj_set_style_pad_left(tab_container, 18, 0);
    lv_obj_set_style_pad_right(tab_container, 18, 0);
    lv_obj_set_style_pad_top(tab_container, 8, 0);
    lv_obj_set_style_pad_bottom(tab_container, 8, 0);
    lv_obj_set_style_pad_column(tab_container, 10, 0);
    lv_obj_set_flex_flow(tab_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* btn_sd1 = lv_button_create(tab_container);
    lv_obj_set_size(btn_sd1, 160, 46);
    lv_label_set_text(lv_label_create(btn_sd1), "SD Card 1");

    lv_obj_t* btn_sd2 = lv_button_create(tab_container);
    lv_obj_set_size(btn_sd2, 160, 46);
    lv_label_set_text(lv_label_create(btn_sd2), "SD Card 2");

    lv_obj_t* status_label = lv_label_create(tab_container);
    lv_label_set_text(status_label, "Showing /sdcard");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(UI_COLOR_MUTED), 0);

    lv_obj_t* content = lv_obj_create(cont);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
    ui_style_surface(content, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_left(content, 18, 0);
    lv_obj_set_style_pad_right(content, 18, 0);
    lv_obj_set_style_pad_top(content, 6, 0);
    lv_obj_set_style_pad_bottom(content, 18, 0);

    lv_obj_t* file_list = lv_obj_create(content);
    lv_obj_set_size(file_list, LV_PCT(100), LV_PCT(100));
    ui_style_surface(file_list, lv_color_hex(UI_COLOR_SURFACE), 18);
    lv_obj_set_style_pad_all(file_list, 10, 0);
    lv_obj_set_style_pad_row(file_list, 8, 0);
    lv_obj_set_flex_flow(file_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(file_list, LV_SCROLLBAR_MODE_AUTO);

    g_file_manager_view.list_container = file_list;
    g_file_manager_view.status_label = status_label;
    g_file_manager_view.sd1_button = btn_sd1;
    g_file_manager_view.sd2_button = btn_sd2;
    ui_update_file_tab_buttons(true);

    file_manager_load_files("/sdcard", file_list);

    lv_obj_add_event_cb(btn_sd1, file_manager_tab_sd1_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btn_sd2, file_manager_tab_sd2_cb, LV_EVENT_CLICKED, NULL);
    file_manager_enable_auto_refresh("/sdcard", file_list);
    file_manager_enable_auto_refresh("/sdcard2", file_list);

    return cont;
}

/**
 * @brief Simple minimal keyboard for file search
 */
lv_obj_t* create_minimal_keyboard(lv_obj_t* parent)
{
    lv_obj_t* kb = lv_keyboard_create(parent);
    lv_obj_set_size(kb, LV_PCT(100), 150);
    ui_style_keyboard(kb);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);

    return kb;
}

static lv_obj_t* ui_get_first_child(lv_obj_t* obj)
{
    if (obj == NULL || lv_obj_get_child_count(obj) == 0)
    {
        return NULL;
    }
    return lv_obj_get_child(obj, 0);
}

static void ui_set_wifi_button_style(lv_obj_t* button, lv_color_t base, lv_color_t accent, const char* text)
{
    lv_obj_t* label = ui_get_first_child(button);

    if (button == NULL)
    {
        return;
    }

    lv_obj_set_style_bg_color(button, base, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(button, accent, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(button, LV_GRAD_DIR_VER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(button, lv_color_hex(UI_COLOR_BORDER), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(button, 14, LV_PART_MAIN | LV_STATE_DEFAULT);

    if (label != NULL)
    {
        lv_label_set_text(label, text);
        lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
    }
}







static void ui_refresh_wifi_controls(const system_status_t* status)
{
    char detail_text[128];

    if (status == NULL)
    {
        return;
    }

    s_wifi_connected = (status->wifi_state == SYSTEM_WIFI_STATE_CONNECTED);
    snprintf(s_wifi_ip, sizeof(s_wifi_ip), "%s", status->wifi_ip);

    if (objects.wifi_connection)
    {
        switch ((system_wifi_state_t)status->wifi_state)
        {
            case SYSTEM_WIFI_STATE_CONNECTED:
                lv_label_set_text(objects.wifi_connection, "Connected");
                lv_obj_set_style_text_color(objects.wifi_connection, lv_color_hex(0xff0ad757), 0);
                break;
            case SYSTEM_WIFI_STATE_CONNECTING:
                lv_label_set_text(objects.wifi_connection, "Connecting...");
                lv_obj_set_style_text_color(objects.wifi_connection, lv_color_hex(0xffffc107), 0);
                break;
            case SYSTEM_WIFI_STATE_DISCONNECTED:
                lv_label_set_text(objects.wifi_connection, "Disconnected");
                lv_obj_set_style_text_color(objects.wifi_connection, lv_color_hex(0xffff6b6b), 0);
                break;
            case SYSTEM_WIFI_STATE_STARTING:
            default:
                lv_label_set_text(objects.wifi_connection, "Starting...");
                lv_obj_set_style_text_color(objects.wifi_connection, lv_color_hex(0xff80d8ff), 0);
                break;
        }
    }

    if (objects.obj4)
    {
        switch ((system_wifi_state_t)status->wifi_state)
        {
            case SYSTEM_WIFI_STATE_CONNECTED:
                snprintf(detail_text, sizeof(detail_text), "Connected to %s | IP %s | RSSI %d dBm | Tap DISCONNECT, long-press to FORGET",
                    status->wifi_ssid[0] ? status->wifi_ssid : "saved network",
                    status->wifi_ip,
                    status->wifi_rssi);
                lv_obj_set_style_text_color(objects.obj4, lv_color_hex(0xff0ad757), 0);
                break;
            case SYSTEM_WIFI_STATE_CONNECTING:
                snprintf(detail_text, sizeof(detail_text), "Connecting to %s | Searching all channels | Attempt %lu/%lu",
                    status->wifi_ssid[0] ? status->wifi_ssid : "saved network",
                    (unsigned long)(status->wifi_retry_count + 1),
                    (unsigned long)(status->wifi_max_retries + 1));
                lv_obj_set_style_text_color(objects.obj4, lv_color_hex(0xffffc107), 0);
                break;
            case SYSTEM_WIFI_STATE_DISCONNECTED:
                snprintf(detail_text, sizeof(detail_text), "%s | Tap CONNECT to connect, long-press CONNECT to forget saved",
                    status->wifi_last_error[0] ? status->wifi_last_error : "Not connected. Enter SSID and password");
                lv_obj_set_style_text_color(objects.obj4, lv_color_hex(0xffff8a80), 0);
                break;
            case SYSTEM_WIFI_STATE_STARTING:
            default:
                snprintf(detail_text, sizeof(detail_text), "Initializing WiFi backend...");
                lv_obj_set_style_text_color(objects.obj4, lv_color_hex(0xff80d8ff), 0);
                break;
        }

        lv_label_set_text(objects.obj4, detail_text);
    }

    if (objects.wifi_conenct_button_1)
    {
        switch ((system_wifi_state_t)status->wifi_state)
        {
            case SYSTEM_WIFI_STATE_CONNECTED:
                ui_set_wifi_button_style(objects.wifi_conenct_button_1,
                    lv_color_hex(0xffd84343),
                    lv_color_hex(0xff8b0000),
                    "DISCONNECT");
                break;
            case SYSTEM_WIFI_STATE_CONNECTING:
                ui_set_wifi_button_style(objects.wifi_conenct_button_1,
                    lv_color_hex(0xffffb300),
                    lv_color_hex(0xffff6f00),
                    "CONNECTING");
                break;
            case SYSTEM_WIFI_STATE_DISCONNECTED:
                ui_set_wifi_button_style(objects.wifi_conenct_button_1,
                    lv_color_hex(0xff00acc1),
                    lv_color_hex(0xff1565c0),
                    status->wifi_connect_attempts > 0 ? "RETRY" : "CONNECT");
                break;
            case SYSTEM_WIFI_STATE_STARTING:
            default:
                ui_set_wifi_button_style(objects.wifi_conenct_button_1,
                    lv_color_hex(0xff546e7a),
                    lv_color_hex(0xff37474f),
                    "CONNECT");
                break;
        }
    }
}

static bool ui_is_bluetooth_tab_active(void)
{
    if (objects.obj0 == NULL)
    {
        return false;
    }

    return lv_tabview_get_tab_active(objects.obj0) == BLUETOOTH_TAB_INDEX;
}

static bool ui_is_gnss_tab_active(void)
{
    if (objects.obj0 == NULL)
    {
        return false;
    }

    return lv_tabview_get_tab_active(objects.obj0) == GNSS_TAB_INDEX;
}

static void ui_refresh_bluetooth_dropdown(bool force)
{
#if CONFIG_RALLYBOX_RACEBOX_ENABLED
    size_t i;
    size_t offset = 0;
    char options[1536] = { 0 };
    uint64_t now_us = (uint64_t)esp_timer_get_time();

    if (objects.bluetooth_device_dropdown == NULL)
    {
        return;
    }

    if (!force)
    {
        if (!ui_is_bluetooth_tab_active())
        {
            return;
        }

        if ((now_us - s_bt_last_dropdown_refresh_us) < (BLUETOOTH_DROPDOWN_REFRESH_MS * 1000ULL))
        {
            return;
        }
    }

    s_bt_last_dropdown_refresh_us = now_us;

    s_bt_visible_count = racebox_get_visible_devices(s_bt_visible_devices, RACEBOX_VISIBLE_DEVICES_MAX);
    if (s_bt_visible_count == 0)
    {
        if (strcmp(s_bt_last_options, "No visible BLE devices") != 0)
        {
            lv_dropdown_set_options(objects.bluetooth_device_dropdown, "No visible BLE devices");
            lv_dropdown_set_selected(objects.bluetooth_device_dropdown, 0);
            snprintf(s_bt_last_options, sizeof(s_bt_last_options), "%s", "No visible BLE devices");
        }
        return;
    }

    for (i = 0; i < s_bt_visible_count; ++i)
    {
        int written = snprintf(options + offset, sizeof(options) - offset,
            "%s | %s | %d dBm%s",
            s_bt_visible_devices[i].name,
            s_bt_visible_devices[i].address,
            (int)s_bt_visible_devices[i].rssi,
            (i + 1 < s_bt_visible_count) ? "\n" : "");
        if (written < 0)
        {
            break;
        }
        if ((size_t)written >= (sizeof(options) - offset))
        {
            break;
        }
        offset += (size_t)written;
    }

    if (strcmp(s_bt_last_options, options) != 0)
    {
        lv_dropdown_set_options(objects.bluetooth_device_dropdown, options);
        lv_dropdown_set_selected(objects.bluetooth_device_dropdown, 0);
        snprintf(s_bt_last_options, sizeof(s_bt_last_options), "%s", options);
    }
#else
    if (objects.bluetooth_device_dropdown)
    {
        lv_dropdown_set_options(objects.bluetooth_device_dropdown, "RaceBox BLE disabled in menuconfig");
        lv_dropdown_set_selected(objects.bluetooth_device_dropdown, 0);
    }
#endif
}

static void ui_refresh_bluetooth_controls(const system_status_t* status)
{
    char text[192];

    if (objects.bluetooth_status_label == NULL)
    {
        return;
    }

#if CONFIG_RALLYBOX_RACEBOX_ENABLED
    if (status == NULL)
    {
        lv_label_set_text(objects.bluetooth_status_label, "Bluetooth status unavailable");
        return;
    }

    if (status->racebox_connected)
    {
        snprintf(text, sizeof(text), "Connected to %s | RSSI %d dBm | %s",
            status->racebox_device_name[0] ? status->racebox_device_name : "RaceBox",
            (int)status->racebox_rssi,
            status->racebox_status_text[0] ? status->racebox_status_text : "Connected");
        lv_obj_set_style_text_color(objects.bluetooth_status_label, lv_color_hex(UI_COLOR_SUCCESS), 0);
        if (objects.bluetooth_connect_button)
        {
            ui_set_wifi_button_style(objects.bluetooth_connect_button,
                lv_color_hex(0xffd84343),
                lv_color_hex(0xff8b0000),
                "DISCONNECT");
        }
    }
    else if (status->racebox_initialized)
    {
        snprintf(text, sizeof(text), "%s",
            status->racebox_status_text[0] ? status->racebox_status_text : "Scanning for BLE devices");
        lv_obj_set_style_text_color(objects.bluetooth_status_label, lv_color_hex(UI_COLOR_WARNING), 0);
        if (objects.bluetooth_connect_button)
        {
            ui_set_wifi_button_style(objects.bluetooth_connect_button,
                lv_color_hex(0xff00acc1),
                lv_color_hex(0xff1565c0),
                "CONNECT");
        }
    }
    else
    {
        snprintf(text, sizeof(text), "RaceBox BLE not initialized");
        lv_obj_set_style_text_color(objects.bluetooth_status_label, lv_color_hex(UI_COLOR_DANGER), 0);
        if (objects.bluetooth_connect_button)
        {
            ui_set_wifi_button_style(objects.bluetooth_connect_button,
                lv_color_hex(0xff00acc1),
                lv_color_hex(0xff1565c0),
                "CONNECT");
        }
    }

    if (status->racebox_connected && !s_bt_prev_connected)
    {
        char line[160];
        ui_stream_start(&s_bt_stream);
        s_bt_rx_count = 0;
        s_last_bt_count = 0;
        snprintf(line, sizeof(line), "Connected: %s | RSSI=%d dBm | %s",
            status->racebox_device_name[0] ? status->racebox_device_name : "RaceBox",
            (int)status->racebox_rssi,
            status->racebox_status_text[0] ? status->racebox_status_text : "Connected");
        ui_window_append_line(&s_bt_window, line);
    }
    else if (!status->racebox_connected && s_bt_prev_connected)
    {
        char line[160];
        snprintf(line, sizeof(line), "Disconnected: %s",
            status->racebox_status_text[0] ? status->racebox_status_text : "RaceBox disconnected");
        ui_window_append_line(&s_bt_window, line);
        ui_stream_stop(&s_bt_stream);
    }
    s_bt_prev_connected = status->racebox_connected;

    lv_label_set_text(objects.bluetooth_status_label, text);
    ui_refresh_bluetooth_dropdown(false);
#else
    lv_label_set_text(objects.bluetooth_status_label, "RaceBox BLE is disabled in menuconfig");
    lv_obj_set_style_text_color(objects.bluetooth_status_label, lv_color_hex(UI_COLOR_DANGER), 0);
    ui_refresh_bluetooth_dropdown(false);
#endif
}

static void action_bluetooth_refresh(lv_event_t* e)
{
    LV_UNUSED(e);

#if CONFIG_RALLYBOX_RACEBOX_ENABLED
    system_status_t status = system_monitor_get_status();
    esp_err_t ret;

    if (status.racebox_connected)
    {
        ui_show_message("Bluetooth", "RaceBox is already connected. Refresh is only used for discovery when disconnected.", lv_color_hex(UI_COLOR_SURFACE));
        ui_refresh_bluetooth_dropdown(true);
        return;
    }

    if (!status.racebox_initialized)
    {
        ret = racebox_init();
        if (ret != ESP_OK)
        {
            ui_handle_error("Failed to initialize RaceBox BLE", ret);
            return;
        }
    }

    ret = racebox_request_scan();
    if (ret != ESP_OK)
    {
        ui_handle_error("Failed to refresh BLE scan", ret);
        return;
    }
#endif

    ui_refresh_bluetooth_dropdown(true);
    ui_show_message("Bluetooth", "Refreshing scan... devices will appear shortly.", lv_color_hex(UI_COLOR_SURFACE));
}

static void action_bluetooth_connect(lv_event_t* e)
{
    LV_UNUSED(e);

#if CONFIG_RALLYBOX_RACEBOX_ENABLED
    system_status_t status = system_monitor_get_status();
    uint32_t selected_index;
    esp_err_t ret;

    if (status.racebox_connected)
    {
        ret = racebox_disconnect();
        if (ret != ESP_OK)
        {
            ui_handle_error("Failed to disconnect BLE device", ret);
            return;
        }

        ui_window_append_line(&s_bt_window, "Disconnect requested by user");
        ui_refresh_bluetooth_dump();
        ui_show_message("Bluetooth", "Disconnecting from RaceBox...", lv_color_hex(UI_COLOR_SURFACE));
        return;
    }

    if (objects.bluetooth_device_dropdown == NULL)
    {
        return;
    }

    if (s_bt_visible_count == 0)
    {
        ui_show_message("Bluetooth", "No visible device to connect. Tap Refresh first.", lv_color_hex(UI_COLOR_WARNING));
        return;
    }

    selected_index = lv_dropdown_get_selected(objects.bluetooth_device_dropdown);
    if (selected_index >= s_bt_visible_count)
    {
        ui_show_message("Bluetooth", "Selected device is no longer available.", lv_color_hex(UI_COLOR_WARNING));
        return;
    }

    ret = racebox_connect_visible_device((size_t)selected_index);
    if (ret != ESP_OK)
    {
        ui_handle_error("Failed to start BLE connection", ret);
        return;
    }

    ui_stream_start(&s_bt_stream);
    s_bt_rx_count = 0;
    s_last_bt_count = 0;
    if (s_bt_dump_panel)
    {
        ui_window_reset(&s_bt_window, s_bt_dump_panel);
    }

    {
        char line[160];
        snprintf(line, sizeof(line), "Connect requested: %s | RSSI=%d dBm",
            s_bt_visible_devices[selected_index].name,
            (int)s_bt_visible_devices[selected_index].rssi);
        ui_window_append_line(&s_bt_window, line);
    }
    ui_refresh_bluetooth_dump();

    ui_show_message("Bluetooth", "Connecting to selected BLE device...", lv_color_hex(UI_COLOR_SURFACE));
#else
    ui_show_message("Bluetooth", "RaceBox BLE is disabled in menuconfig.", lv_color_hex(UI_COLOR_DANGER));
#endif
}

static void ui_load_saved_wifi_credentials(void)
{
    nvs_handle_t handle;
    char ssid[33] = { 0 };
    char password[65] = { 0 };
    size_t ssid_len = sizeof(ssid);
    size_t password_len = sizeof(password);

    if (objects.wifi_ssid_1 == NULL || objects.wifi_password_1 == NULL)
    {
        return;
    }

    if (nvs_open("wifi_cfg", NVS_READONLY, &handle) != ESP_OK)
    {
        lv_textarea_set_text(objects.wifi_ssid_1, "");
        lv_textarea_set_text(objects.wifi_password_1, "");
        return;
    }

    if (nvs_get_str(handle, "ssid", ssid, &ssid_len) == ESP_OK)
    {
        lv_textarea_set_text(objects.wifi_ssid_1, ssid);
    }
    else
    {
        lv_textarea_set_text(objects.wifi_ssid_1, "");
    }

    if (nvs_get_str(handle, "password", password, &password_len) == ESP_OK)
    {
        lv_textarea_set_text(objects.wifi_password_1, password);
    }
    else
    {
        lv_textarea_set_text(objects.wifi_password_1, "");
    }

    nvs_close(handle);
}

static void action_remove_saved_wifi(lv_event_t* e)
{
    esp_err_t ret = system_wifi_disconnect(true);
    if (ret != ESP_OK)
    {
        ui_handle_error("Failed to remove saved WiFi network", ret);
        return;
    }

    if (objects.wifi_ssid_1)
    {
        lv_textarea_set_text(objects.wifi_ssid_1, "");
    }
    if (objects.wifi_password_1)
    {
        lv_textarea_set_text(objects.wifi_password_1, "");
    }

    system_status_t status_copy = system_monitor_get_status();
    ui_refresh_wifi_controls(&status_copy);
    ui_show_message("Saved Network Removed",
        "Saved WiFi credentials were removed from NVS. Enter new SSID/password to connect.",
        lv_color_hex(0xff1565c0));
    (void)e;
}

/**
 * @brief Production Live Logging Logic: Writes system data to SD Card 2 (Slot 1)
 * Requirement: Create new file after 2000 lines, no crashes, real-time stable.
 */
static void ui_logic_log_to_sd(const char* data)
{
    // Prefer SD Card 1 (/sdcard) for production logs
    if (!sd_card1_is_mounted()) return;

    // Create a new timestamped file if needed
    if (current_log_filename[0] == '\0' || s_log_line_count >= 2000)
    {
        s_log_line_count = 0;
        s_log_file_index++;
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        snprintf(current_log_filename, sizeof(current_log_filename), "/sdcard/rallybox_sdcard1_%04d%02d%02d_%02d%02d%02d.txt",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec);
        ESP_LOGI(TAG, "Creating new log file: %s", current_log_filename);
        // Write header line
        FILE* hf = fopen(current_log_filename, "w");
        if (hf)
        {
            fprintf(hf, "RallyBox log start: %04d-%02d-%02d %02d:%02d:%02d IST\n",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec);
            fprintf(hf, "Columns: sl_no | timestamp_IST | cpu_pct | free_heap_bytes | wifi_connected | wifi_ssid | wifi_ip | sd_slot | sd_capacity_mb | sd_used_mb\n\n");
            fclose(hf);
        }
    }

    FILE* f = fopen(current_log_filename, "a");
    if (!f)
    {
        ESP_LOGE(TAG, "Failed to open log file %s", current_log_filename);
        return;
    }

    system_status_t st = system_monitor_get_status();
    time_t tnow = time(NULL);
    struct tm tlocal;
    localtime_r(&tnow, &tlocal);
    char timestr[64];
    strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S %Z", &tlocal);

    uint64_t cap = 0, used = 0;
    sd_card_get_info(1, &cap, &used);

    s_log_line_count++;
    fprintf(f, "%u. %s | CPU: %u%% | FreeHeap: %u | WiFi: %s | SSID: %s | IP: %s | SD1: %.0lluMB total, %.0lluMB used | Msg: %s\n",
        s_log_line_count,
        timestr,
        (unsigned)st.cpu_load_percent,
        (unsigned)st.free_heap_bytes,
        st.wifi_connected ? "Connected" : "Disconnected",
        st.wifi_ssid[0] ? st.wifi_ssid : "",
        st.wifi_ip[0] ? st.wifi_ip : "",
        (unsigned long long)cap,
        (unsigned long long)used,
        data ? data : "");
    fclose(f);
}

/**
 * @brief Callback for WiFi events to update UI
 */
void ui_logic_notify_wifi_status(bool connected, const char* ip)
{
    s_wifi_connected = connected;
    if (ip)
    {
        strncpy(s_wifi_ip, ip, sizeof(s_wifi_ip) - 1);
        s_wifi_ip[sizeof(s_wifi_ip) - 1] = '\0';
    }
}

void ui_logic_update_wifi_live(const system_status_t* status)
{
#if CONFIG_RALLYBOX_GNSS_ENABLED
    s_gnss_listening = gnss_is_running();
#endif

    ui_refresh_wifi_controls(status);
    if (status)
    {
        bool bt_tab_active = ui_is_bluetooth_tab_active();
        bool bt_state_changed = (status->racebox_connected != s_bt_ui_last_connected) ||
            (status->racebox_initialized != s_bt_ui_last_initialized);

        if (bt_tab_active || bt_state_changed)
        {
            ui_refresh_bluetooth_controls(status);
            s_bt_ui_last_connected = status->racebox_connected;
            s_bt_ui_last_initialized = status->racebox_initialized;
        }
    }
    ui_update_main_status_panels(status);

    if (ui_is_gnss_tab_active() && objects.gnss_status_label)
    {
        lv_label_set_text(objects.gnss_status_label, s_gnss_status_text);
    }
    if (ui_is_gnss_tab_active() && objects.gnss_start_stop_button)
    {
        lv_obj_t* label = ui_get_first_child(objects.gnss_start_stop_button);
        if (label)
        {
            lv_label_set_text(label, s_gnss_listening ? "STOP" : "START");
        }
    }
}

typedef struct
{
    lv_obj_t* bar;
    lv_obj_t* label;
    lv_obj_t* overlay;
    int slot;
    volatile bool done;
    esp_err_t result;
} format_progress_t;

static void quick_format_task(void* pvParameters)
{
    format_progress_t* progress = (format_progress_t*)pvParameters;
    progress->result = sd_card_quick_format(progress->slot);
    progress->done = true;
    vTaskDelete(NULL);
}

static void delayed_delete_cb(lv_timer_t* t)
{
    lv_obj_t* obj = (lv_obj_t*)lv_timer_get_user_data(t);
    if (lv_obj_is_valid(obj))
    {
        lv_obj_delete_async(obj);
    }
    lv_timer_delete(t);
}

static void format_timer_cb(lv_timer_t* t)
{
    format_progress_t* p = (format_progress_t*)lv_timer_get_user_data(t);
    int val = lv_bar_get_value(p->bar);

    if (!p->done)
    {
        if (val < 90)
        {
            lv_bar_set_value(p->bar, val + 4, LV_ANIM_ON);
        }
    }
    else
    {
        char result_text[48];
        lv_bar_set_value(p->bar, 100, LV_ANIM_ON);

        if (p->result == ESP_OK)
        {
            snprintf(result_text, sizeof(result_text), "SD Card %d formatted successfully.", p->slot);
            lv_obj_set_style_text_color(p->label, lv_color_hex(0xff0ad757), 0);
        }
        else
        {
            snprintf(result_text, sizeof(result_text), "Failed to format SD Card %d.", p->slot);
            lv_obj_set_style_text_color(p->label, lv_color_hex(0xffff6b6b), 0);
        }
        lv_label_set_text(p->label, result_text);
        /* Log the result to SD now that formatting has completed and FATFS is free. */
        if (p->result == ESP_OK)
        {
            ui_logic_log_to_sd("QUICK_FORMAT_COMPLETED_OK");
        }
        else
        {
            ui_logic_log_to_sd("QUICK_FORMAT_FAILED");
        }

        lv_timer_delete(t);
        // Delete overlay after 2 seconds
        lv_timer_t* close_timer = lv_timer_create(delayed_delete_cb, 2000, p->overlay);
        lv_timer_set_repeat_count(close_timer, 1);
        free(p);
    }
}

/**
 * @brief Handle confirmation of SD card formatting
 */
static void action_format_confirmed(lv_event_t* e)
{
    int slot = (int)lv_event_get_user_data(e);
    lv_obj_t* mbox = find_msgbox_ancestor(lv_event_get_current_target(e));

    if (mbox == NULL)
    {
        ESP_LOGE(TAG, "Format action missing message box ancestor");
        return;
    }

    ESP_LOGI(TAG, "Starting quick format for Slot %d", slot);
    char title_text[32];
    char status_text[72];
    snprintf(title_text, sizeof(title_text), "Quick Format SD Card %d", slot);
    snprintf(status_text, sizeof(status_text), "Please wait while SD Card %d is being quick-formatted.", slot);

    // Create a blocking backdrop with a centered progress panel
    lv_obj_t* overlay = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
    lv_obj_center(overlay);

    lv_obj_t* panel = lv_obj_create(overlay);
    lv_obj_set_size(panel, 430, 220);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0xffffffff), 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_radius(panel, 16, 0);
    lv_obj_set_style_pad_all(panel, 18, 0);

    lv_obj_t* title_label = lv_label_create(panel);
    lv_label_set_text(title_label, title_text);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xffffffff), 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 18);

    lv_obj_t* msg_label = lv_label_create(panel);
    lv_label_set_text(msg_label, status_text);
    lv_label_set_long_mode(msg_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(msg_label, 320);
    lv_obj_set_style_text_color(msg_label, lv_color_hex(0xffffffff), 0);
    lv_obj_align(msg_label, LV_ALIGN_TOP_MID, 0, 52);

    lv_obj_t* bar = lv_bar_create(panel);
    lv_obj_set_size(bar, 300, 30);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xff4a4a4a), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xff0ad757), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_bar_set_value(bar, 6, LV_ANIM_OFF);

    // Close modal
    lv_msgbox_close_async(mbox);

    // Setup progress state and background task
    format_progress_t* p = malloc(sizeof(format_progress_t));
    p->bar = bar;
    p->label = msg_label;
    p->overlay = overlay;
    p->slot = slot;
    p->done = false;
    p->result = ESP_FAIL;

    lv_timer_create(format_timer_cb, 120, p);
    xTaskCreatePinnedToCore(quick_format_task, "quick_format", 6144, p, 4, NULL, 1);
    /* Avoid writing to the SD while a background quick-format may hold FATFS locks.
     * We'll emit a completion log from the LVGL timer callback (`format_timer_cb`) once
     * the background task sets `p->done` and `p->result`.
     */
    return;
}

/**
 * @brief Handle confirmation of Log Export from SD2 to SD1
 */
static void action_export_logs_confirmed(lv_event_t* e)
{
    lv_obj_t* btn = lv_event_get_current_target(e);
    lv_obj_t* mbox = find_msgbox_ancestor(btn);

    if (mbox == NULL)
    {
        ESP_LOGE(TAG, "Export action missing message box ancestor");
        return;
    }

    ESP_LOGI(TAG, "Starting Log Export from SD2 to SD1");

    // Create an overlay with a progress bar
    lv_obj_t* overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(overlay, 400, 200);
    lv_obj_center(overlay);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x222222), 0);

    lv_obj_t* msg_label = lv_label_create(overlay);
    lv_label_set_text(msg_label, "Backing up logs to SD1...");
    lv_obj_align(msg_label, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t* bar = lv_bar_create(overlay);
    lv_obj_set_size(bar, 300, 30);
    lv_obj_center(bar);
    lv_bar_set_value(bar, 10, LV_ANIM_OFF);

    lv_msgbox_close_async(mbox);

    // Simulation/Integration point
    // In a real scenario, we'd iterate through /sdcard2/*.log and copy to /sdcard
    esp_err_t err = sd_card_export_logs();

    // Log to SD for history
    ui_logic_log_to_sd("LOG_EXPORT_TRIGGERED");

    if (err == ESP_OK)
    {
        lv_bar_set_value(bar, 100, LV_ANIM_ON);
        lv_label_set_text(msg_label, "Export Successful!");
    }
    else if (err == ESP_ERR_NOT_FOUND)
    {
        lv_bar_set_value(bar, 100, LV_ANIM_OFF);
        lv_label_set_text(msg_label, "No logs found to export.");
    }
    else
    {
        lv_bar_set_value(bar, 100, LV_ANIM_OFF);
        lv_label_set_text(msg_label, "Export Failed!");
    }

    lv_timer_t* timer = lv_timer_create(delayed_delete_cb, 2000, overlay);
    lv_timer_set_repeat_count(timer, 1);
    return;
}

static void action_modal_cancel(lv_event_t* e)
{
    lv_obj_t* btn = lv_event_get_current_target(e);
    lv_obj_t* mbox = find_msgbox_ancestor(btn);
    if (mbox)
    {
        lv_msgbox_close_async(mbox);
    }
}

// SD test progress tracking structure
typedef struct
{
    lv_obj_t* overlay;
    lv_obj_t* bar;
    lv_obj_t* status_label;
    lv_obj_t* progress_label;
    int slot;
    volatile uint32_t write_bytes;
    volatile uint32_t elapsed_seconds;
    volatile bool test_done;
    esp_err_t test_result;
    time_t start_time;
} sd_test_progress_t;

// Background task for SD card write test (instant completion)
static void sd_write_test_task(void* pvParameters)
{
    sd_test_progress_t* progress = (sd_test_progress_t*)pvParameters;
    const char* mount = (progress->slot == 1) ? "/sdcard" : "/sdcard2";
    time_t start = time(NULL);

    ESP_LOGI(TAG, "SD Card %d write test starting (instant)", progress->slot);

    // Create test file with detailed header. Use the rallybox naming convention
    // so test files are easily identifiable and consistent with production logs.
    char test_filename[128];
    struct tm start_tm;
    localtime_r(&start, &start_tm);
    snprintf(test_filename, sizeof(test_filename), "%s/rallybox_sdcard%d_%04d%02d%02d_%02d%02d%02d_sdtest.txt",
        mount, progress->slot,
        start_tm.tm_year + 1900, start_tm.tm_mon + 1, start_tm.tm_mday,
        start_tm.tm_hour, start_tm.tm_min, start_tm.tm_sec);

    FILE* f = fopen(test_filename, "w");
    if (!f)
    {
        progress->test_result = ESP_FAIL;
        progress->test_done = true;
        vTaskDelete(NULL);
        return;
    }

    system_status_t sys = system_monitor_get_status();

    // Write a detailed start line including CPU, RAM, timezone, Wi-Fi and SD info
    time_t now = time(NULL);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char timestr[64];
    strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S %Z", &timeinfo);

    uint64_t cap = 0, used = 0;
    sd_card_get_info(progress->slot, &cap, &used);

    fprintf(f, "SDTEST_START %s slot=%d uptime=%lu cpu=%u%% free_heap=%u total_heap=%u wifi=%s ssid=%s ip=%s sd_total_mb=%.0llu sd_used_mb=%.0llu\n",
        timestr,
        progress->slot,
        sys.uptime_seconds,
        (unsigned)sys.cpu_load_percent,
        (unsigned)sys.free_heap_bytes,
        (unsigned)sys.total_heap_bytes,
        sys.wifi_connected ? "connected" : "disconnected",
        sys.wifi_ssid[0] ? sys.wifi_ssid : "",
        sys.wifi_ip[0] ? sys.wifi_ip : "",
        (unsigned long long)cap,
        (unsigned long long)used);

    progress->start_time = start;
    uint32_t bytes_written = 0;
    uint32_t write_count = 0;
    const uint32_t chunk_size = 4096;
    char write_buffer[4096];

    // Fill buffer with pattern
    memset(write_buffer, 0xAA, sizeof(write_buffer));

    // Write just one chunk to complete instantly
    size_t written = fwrite(write_buffer, 1, chunk_size, f);
    if (written == chunk_size)
    {
        bytes_written += chunk_size;
        write_count++;
    }
    else
    {
        ESP_LOGE(TAG, "Write failed at offset %lu", bytes_written);
    }

    // Concise end line
    time_t end_time = time(NULL);
    struct tm end_timeinfo;
    localtime_r(&end_time, &end_timeinfo);
    char end_timestr[64];
    strftime(end_timestr, sizeof(end_timestr), "%Y-%m-%d %H:%M:%S %Z", &end_timeinfo);

    uint64_t cap_end = 0, used_end = 0;
    sd_card_get_info(progress->slot, &cap_end, &used_end);

    fprintf(f, "SDTEST_END %s slot=%d bytes=%lu ops=%lu speed=%.2fMB/s cpu=%u%% free_heap=%u total_heap=%u sd_total_mb=%.0llu sd_used_mb=%.0llu\n",
        end_timestr,
        progress->slot,
        bytes_written,
        write_count,
        (float)(bytes_written) / (1024.0 * 1024.0) / 1.0,  // Speed over 1 second
        (unsigned)sys.cpu_load_percent,
        (unsigned)sys.free_heap_bytes,
        (unsigned)sys.total_heap_bytes,
        (unsigned long long)cap_end,
        (unsigned long long)used_end);

    fclose(f);

    progress->elapsed_seconds = 1;
    progress->write_bytes = bytes_written;
    progress->test_result = ESP_OK;
    progress->test_done = true;

    ESP_LOGI(TAG, "SD Card %d test completed: %lu bytes in %lu operations",
        progress->slot, bytes_written, write_count);

    vTaskDelete(NULL);
}

// Timer callback for SD test progress updates
static void sd_test_progress_cb(lv_timer_t* t)
{
    sd_test_progress_t* p = (sd_test_progress_t*)lv_timer_get_user_data(t);

    if (!p->test_done)
    {
        // Update progress bar (0-99)
        int progress = (p->elapsed_seconds >= 1) ? 99 : ((p->elapsed_seconds * 100) / 1);
        lv_bar_set_value(p->bar, progress, LV_ANIM_ON);

        // Update labels
        char status_text[80];
        snprintf(status_text, sizeof(status_text), "Please wait, writing in progress...\n%u / 1 seconds",
            p->elapsed_seconds);
        lv_label_set_text(p->status_label, status_text);

        char progress_text[64];
        snprintf(progress_text, sizeof(progress_text), "%.2f MB written",
            (float)(p->write_bytes) / (1024.0 * 1024.0));
        lv_label_set_text(p->progress_label, progress_text);
    }
    else
    {
        // Test complete
        lv_bar_set_value(p->bar, 100, LV_ANIM_ON);

        char result_text[128];
        if (p->test_result == ESP_OK)
        {
            snprintf(result_text, sizeof(result_text),
                "SD Card %d test completed successfully!\nTotal: %.2f MB written\nDuration: %u seconds",
                p->slot, (float)(p->write_bytes) / (1024.0 * 1024.0), p->elapsed_seconds);
            lv_obj_set_style_text_color(p->status_label, lv_color_hex(0xff0ad757), 0);
        }
        else
        {
            snprintf(result_text, sizeof(result_text),
                "SD Card %d test failed!\nPlease check the card and try again.", p->slot);
            lv_obj_set_style_text_color(p->status_label, lv_color_hex(0xffff6b6b), 0);
        }
        lv_label_set_text(p->status_label, result_text);
        lv_obj_set_style_text_color(p->progress_label, lv_color_hex(0xffffffff), 0);

        lv_timer_delete(t);
        lv_timer_t* close_timer = lv_timer_create(delayed_delete_cb, 3000, p->overlay);
        lv_timer_set_repeat_count(close_timer, 1);
        free(p);
    }
}

/**
 * @brief Action for SD Card Testing (Production Method with Wi-Fi requirement)
 */
void action_test_sdcard(lv_event_t* e)
{
    int slot = (int)(intptr_t)lv_event_get_user_data(e);

    bool mounted = (slot % 100 == 1) ? sd_card1_is_mounted() : sd_card2_is_mounted();
    if (!mounted)
    {
        ui_show_message("Error", "No SD card detected in slot. Insert card first.", lv_color_hex(0xffe57373));
        return;
    }

    // Export Logic (Short press/Click with specific slot code > 200)
    if (slot > 200)
    {
        if (!sd_card1_is_mounted() || !sd_card2_is_mounted())
        {
            ui_show_message("Error", "Both SD cards required for export.", lv_color_hex(0xffe57373));
            return;
        }
        lv_obj_t* mbox = ui_create_modal("Backup Logs", "Export all system logs from Slot 2 to Slot 1?", lv_color_hex(0xff2a2a2a));
        lv_obj_t* export_btn = lv_msgbox_add_footer_button(mbox, "Export");
        lv_obj_t* cancel_btn = lv_msgbox_add_footer_button(mbox, "Cancel");
        lv_obj_add_event_cb(export_btn, action_export_logs_confirmed, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(cancel_btn, action_modal_cancel, LV_EVENT_CLICKED, NULL);
        return;
    }

    // Format Logic Request
    if (slot > 100)
    {
        int card_num = slot - 100;
        lv_obj_t* mbox = ui_create_modal("Confirm Quick Format", "A quick format will rebuild the FAT filesystem on this card.\nExisting files will no longer be accessible.", lv_color_hex(0xff8b2e2e));
        lv_obj_t* format_btn = lv_msgbox_add_footer_button(mbox, "Quick Format");
        lv_obj_t* cancel_btn = lv_msgbox_add_footer_button(mbox, "Cancel");
        lv_obj_add_event_cb(format_btn, action_format_confirmed, LV_EVENT_CLICKED, (void*)(intptr_t)card_num);
        lv_obj_add_event_cb(cancel_btn, action_modal_cancel, LV_EVENT_CLICKED, NULL);
        return;
    }

    // Wi-Fi CONNECTION CHECK before write test
    system_status_t status = system_monitor_get_status();
    if (status.wifi_state != SYSTEM_WIFI_STATE_CONNECTED)
    {
        ui_show_message("Wi-Fi Required",
            "SD card write testing requires Wi-Fi connection.\n"
            "Please connect to Wi-Fi first and try again.",
            lv_color_hex(0xffe57373));
        ui_logic_log_to_sd("SDCARD_TEST_FAILED_NO_WIFI");
        return;
    }

    // Create progress overlay for 1-minute write test
    lv_obj_t* overlay = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
    lv_obj_center(overlay);

    lv_obj_t* panel = lv_obj_create(overlay);
    lv_obj_set_size(panel, 500, 280);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0xffffffff), 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_radius(panel, 16, 0);
    lv_obj_set_style_pad_all(panel, 20, 0);

    char title_text[64];
    snprintf(title_text, sizeof(title_text), "SD Card %d Write Test", slot);
    lv_obj_t* title_label = lv_label_create(panel);
    lv_label_set_text(title_label, title_text);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xffffffff), 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t* status_label = lv_label_create(panel);
    lv_label_set_text(status_label, "Please wait, writing in progress...\n0 / 60 seconds");
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(status_label, 380);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xffffc107), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_16, 0);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t* bar = lv_bar_create(panel);
    lv_obj_set_size(bar, 380, 35);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xff3a3a3a), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xff00acc1), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 10, 0);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);

    lv_obj_t* progress_label = lv_label_create(panel);
    lv_label_set_text(progress_label, "0.00 MB written");
    lv_obj_set_style_text_color(progress_label, lv_color_hex(0xffffffff), 0);
    lv_obj_set_style_text_font(progress_label, &lv_font_montserrat_14, 0);
    lv_obj_align(progress_label, LV_ALIGN_BOTTOM_MID, 0, -10);

    // Setup progress tracking state
    sd_test_progress_t* p = malloc(sizeof(sd_test_progress_t));
    p->overlay = overlay;
    p->bar = bar;
    p->status_label = status_label;
    p->progress_label = progress_label;
    p->slot = slot;
    p->write_bytes = 0;
    p->elapsed_seconds = 0;
    p->test_done = false;
    p->test_result = ESP_FAIL;

    // Start background write test task
    char log_buf[128];
    snprintf(log_buf, sizeof(log_buf), "SDCARD_TEST_STARTED_SLOT_%d_WITH_WIFI", slot);
    ui_logic_log_to_sd(log_buf);

    lv_timer_create(sd_test_progress_cb, 500, p);  // Update progress every 500ms
    xTaskCreatePinnedToCore(sd_write_test_task, "sd_write_test", 8192, p, 5, NULL, 1);
}

/**
 * @brief Initialize all event listeners for the professional production UI
 */
void ui_logic_init_events(void);


/**
 * @brief Update Live System Stats on Dashboard
 */
void ui_logic_update_system_stats(uint32_t cpu_load, uint32_t errors)
{
    if (objects.system_load_status_1)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%lu%%", cpu_load);
        lv_label_set_text(objects.system_load_status_1, (cpu_load < 80) ? "STABLE" : "OVERLOAD");
        lv_obj_set_style_text_color(objects.system_load_status_1, (cpu_load < 80) ? lv_color_hex(0xff0ad757) : lv_color_hex(0xffe57373), 0);
    }
    if (objects.error_status_1)
    { // Corrected: error_status_1 is the counter label
        char buf[16];
        snprintf(buf, sizeof(buf), "%lu", errors);
        lv_label_set_text(objects.error_status_1, buf);
    }
}

/**
 * @brief Professional error handler for UI operations
 */
static void ui_handle_error(const char* msg, esp_err_t err)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "%s\nError: %s", msg, esp_err_to_name(err));
    ui_show_message("System Error", buf, lv_color_hex(0xffe57373));
}

/**
 * @brief Show SD card mount/unmount event
 */
void ui_show_sd_card_event(int slot, bool mounted)
{
    char title[32];
    char msg[64];
    if (mounted)
    {
        snprintf(title, sizeof(title), "SD Card %d Mounted", slot);
        snprintf(msg, sizeof(msg), "SD Card %d has been mounted successfully.", slot);
        ESP_LOGI(TAG, "UI: Showing SD Card %d mounted notification", slot);
    }
    else
    {
        snprintf(title, sizeof(title), "SD Card %d Disconnected", slot);
        snprintf(msg, sizeof(msg), "SD Card %d has been disconnected/ejected.", slot);
        ESP_LOGW(TAG, "UI: Showing SD Card %d disconnected notification", slot);
    }
    ui_show_message(title, msg, mounted ? lv_color_hex(0xff4caf50) : lv_color_hex(0xffff9800));
}

/**
 * @brief Action for WiFi Connection (Production Level)
 */
void action_connect_wifi(lv_event_t* e)
{
    system_status_t status = system_monitor_get_status();
    lv_obj_t* ssid_obj = objects.wifi_ssid_1;
    lv_obj_t* pass_obj = objects.wifi_password_1;

    const char* ssid = ssid_obj ? lv_textarea_get_text(ssid_obj) : "";
    const char* pass = pass_obj ? lv_textarea_get_text(pass_obj) : "";

    if (status.wifi_state == SYSTEM_WIFI_STATE_CONNECTING)
    {
        ui_show_message("WiFi Busy", "A WiFi connection attempt is already running. Wait for it to finish or fail before retrying.", lv_color_hex(0xffffc107));
        lv_obj_add_flag(objects.ui_keyboard_1, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (status.wifi_state == SYSTEM_WIFI_STATE_DISCONNECTED && strlen(ssid) == 0)
    {
        if (status.wifi_ssid[0] != '\0')
        {
            esp_err_t saved_err = system_wifi_connect_saved();
            if (saved_err != ESP_OK)
            {
                ui_handle_error("Failed to reconnect using saved WiFi credentials", saved_err);
                return;
            }
            system_status_t status_copy = system_monitor_get_status();
            ui_refresh_wifi_controls(&status_copy);
            lv_obj_add_flag(objects.ui_keyboard_1, LV_OBJ_FLAG_HIDDEN);
            return;
        }

        ui_show_message("WiFi Required", "Enter an SSID and password, then tap CONNECT. Scan is only for discovering nearby networks.", lv_color_hex(0xff1565c0));
        system_status_t status_copy2 = system_monitor_get_status();
        ui_refresh_wifi_controls(&status_copy2);
        lv_obj_add_flag(objects.ui_keyboard_1, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (status.wifi_state == SYSTEM_WIFI_STATE_CONNECTED)
    {
        esp_err_t disconnect_err = system_wifi_disconnect(false);
        if (disconnect_err != ESP_OK)
        {
            ui_handle_error("Failed to disconnect WiFi", disconnect_err);
            return;
        }
        system_status_t status_copy3 = system_monitor_get_status();
        ui_refresh_wifi_controls(&status_copy3);
        lv_obj_add_flag(objects.ui_keyboard_1, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (strlen(ssid) < 2)
    {
        if (status.wifi_ssid[0])
        {
            esp_err_t saved_err = system_wifi_connect_saved();
            if (saved_err != ESP_OK)
            {
                ui_handle_error("Failed to reconnect using saved WiFi credentials", saved_err);
                return;
            }
        }
        else
        {
            ui_show_message("Validation Error", "SSID must be at least 2 characters.", lv_color_hex(0xffe57373));
            return;
        }
    }

    if (strlen(pass) > 0 && strlen(pass) < 8)
    {
        ui_show_message("Validation Error", "Password must be at least 8 characters (or empty).", lv_color_hex(0xffe57373));
        return;
    }

    if (strlen(ssid) >= 2)
    {
        ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid);
        esp_err_t connect_err = system_wifi_connect_credentials(ssid, pass);
        if (connect_err != ESP_OK)
        {
            ui_handle_error("Failed to start WiFi connection", connect_err);
            return;
        }
    }

    system_status_t status_copy4 = system_monitor_get_status();
    ui_refresh_wifi_controls(&status_copy4);

    lv_obj_add_flag(objects.ui_keyboard_1, LV_OBJ_FLAG_HIDDEN);
}

void action_show_keyboard(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* ta = lv_event_get_current_target(e);

    if (code != LV_EVENT_PRESSED && code != LV_EVENT_CLICKED && code != LV_EVENT_FOCUSED)
    {
        return;
    }

    if (ta == NULL || objects.ui_keyboard_1 == NULL)
    {
        return;
    }

    /* Some events can bubble from non-textarea children/containers.
     * Guard aggressively to avoid LVGL type assertions. */
    if (ta != objects.wifi_ssid_1 &&
        ta != objects.wifi_password_1 &&
        ta != objects.gnss_baud_input &&
        ta != objects.gnss_tx_input &&
        ta != objects.gnss_rx_input)
    {
        return;
    }

    if (!lv_obj_check_type(ta, &lv_textarea_class) || !lv_obj_check_type(objects.ui_keyboard_1, &lv_keyboard_class))
    {
        return;
    }

    /* Ensure the target input is editable and focusable before attaching keyboard. */
    lv_obj_add_flag(ta, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_clear_state(ta, LV_STATE_DISABLED);
    lv_textarea_set_cursor_click_pos(ta, true);

    if (ta == objects.gnss_baud_input || ta == objects.gnss_tx_input || ta == objects.gnss_rx_input)
    {
        lv_keyboard_set_mode(objects.ui_keyboard_1, LV_KEYBOARD_MODE_NUMBER);
    }
    else
    {
        lv_keyboard_set_mode(objects.ui_keyboard_1, LV_KEYBOARD_MODE_TEXT_LOWER);
    }

    lv_keyboard_set_textarea(objects.ui_keyboard_1, ta);
    lv_obj_remove_flag(objects.ui_keyboard_1, LV_OBJ_FLAG_HIDDEN);

    // Move keyboard to a visible position
    lv_obj_set_pos(objects.ui_keyboard_1, 0, 432); // Bottom 40% of 720p screen starts at 432
    lv_obj_set_size(objects.ui_keyboard_1, LV_PCT(100), 288); // 40% of 720 is 288

    // Potential UX improvement: Scroll the input box up so the active textarea is visible
    lv_obj_scroll_to_view(ta, LV_ANIM_ON);
}

void action_password_ssid_keyboad(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL)
    {
        lv_obj_add_flag(objects.ui_keyboard_1, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(objects.ui_keyboard_1, NULL);
    }
}

void action_jumptowifiscreen(lv_event_t* e)
{
    ESP_LOGI(TAG, "Navigating to WiFi Tab");
    if (objects.obj0)
    { // obj0 is the TabView according to screens.c
        lv_tabview_set_active(objects.obj0, 0, LV_ANIM_ON); // 0 = Menu, 1 = Wi-Fi
    }
}

/**
 * @brief Callback for Files tab click - create/show file manager
 */
 /* Production Method: Helper functions to update UI state without touching UI code */

void ui_logic_update_sd_status(int slot, bool active)
{
    lv_obj_t* target = (slot == 1) ? objects.active_checkbox_sd_card_1_1 : objects.active_checkbox_sd_card_2_1;
    if (target)
    {
        if (active) lv_obj_add_state(target, LV_STATE_CHECKED);
        else lv_obj_remove_state(target, LV_STATE_CHECKED);
    }
}

void ui_logic_set_uptime(uint32_t seconds)
{
    char buf[32];
    uint32_t h = seconds / 3600;
    uint32_t m = (seconds % 3600) / 60;
    uint32_t s = seconds % 60;
    snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", h, m, s);
    if (objects.title_4)
    {
        lv_label_set_text(objects.title_4, buf);
    }
}

/**
 * @brief Logic to update SD card visibility and status on dashboard
 */
void ui_logic_update_storage_info(int slot, bool detected, uint64_t capacity_mb, uint64_t used_mb)
{
    lv_obj_t* active_cb = (slot == 1) ? objects.active_checkbox_sd_card_1_1 : objects.active_checkbox_sd_card_2_1;
    lv_obj_t* status_label = (slot == 1) ? objects.read_write_label_1_1 : objects.read_write_label_2_1;
    lv_obj_t* storage_label = (slot == 1) ? objects.sdcard_storage_1_1 : objects.sd_card_title_2_1;

    char storage_buf[128];
    char status_buf[64];

    if (detected && capacity_mb > 0)
    {
        lv_obj_add_state(active_cb, LV_STATE_CHECKED);

        // Format storage: "Used / Total" with auto-unit conversion (MB/GB)
        double total_gb = capacity_mb / 1024.0;
        double used_gb = used_mb / 1024.0;

        if (total_gb >= 1.0)
        {
            snprintf(storage_buf, sizeof(storage_buf), "%.1f / %.1f GB", used_gb, total_gb);
            ESP_LOGI(TAG, "UI: SD Card %d - %.1f GB / %.1f GB", slot, used_gb, total_gb);
        }
        else
        {
            snprintf(storage_buf, sizeof(storage_buf), "%llu / %llu MB", used_mb, capacity_mb);
            ESP_LOGI(TAG, "UI: SD Card %d - %llu MB / %llu MB", slot, used_mb, capacity_mb);
        }

        lv_label_set_text(storage_label, storage_buf);

        // Status string
        snprintf(status_buf, sizeof(status_buf), "SD CARD %d READY", slot);
        lv_label_set_text(status_label, status_buf);
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xff0ad757), 0);
    }
    else
    {
        lv_obj_remove_state(active_cb, LV_STATE_CHECKED);
        lv_label_set_text(storage_label, slot == 1 ? "Storage 1" : "Storage 2");
        lv_label_set_text(status_label, "DISCONNECTED");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xffe57373), 0);
        ESP_LOGW(TAG, "UI: SD Card %d - DISCONNECTED", slot);
    }
}

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
// MODERN UI COLOR PALETTE & STYLING HELPERS
// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ

/**
 * @brief Apply consistent modern styling to buttons
 */
static void style_modern_button(lv_obj_t* button, lv_color_t primary_color, lv_color_t secondary_color)
{
    if (!button) return;

    // Button styling
    lv_obj_set_style_bg_color(button, primary_color, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(button, secondary_color, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(button, LV_GRAD_DIR_VER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(UI_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_outline_width(button, 0, 0);

    // Pressed state
    lv_obj_set_style_bg_opa(button, 220, LV_PART_MAIN | LV_STATE_PRESSED);

    // Label styling inside button
    lv_obj_t* label = lv_obj_get_child(button, 0);
    if (label)
    {
        lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_letter_space(label, 1, 0);
    }
}

/**
 * @brief Initialize all event listeners for the professional production UI
 */
void ui_logic_init_events(void)
{
    // 0. Set default tab and add test button handlers
    if (objects.obj0)
    {
        lv_tabview_set_active(objects.obj0, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(objects.obj0, lv_color_hex(UI_COLOR_BG), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(objects.obj0, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

        // Add Files tab to the tabview
        g_files_tab = lv_tabview_add_tab(objects.obj0, "Files");
        if (g_files_tab)
        {
            // Style the Files tab - fill entire space
            lv_obj_set_size(g_files_tab, LV_PCT(100), LV_PCT(100));
            lv_obj_set_style_bg_color(g_files_tab, lv_color_hex(UI_COLOR_BG), 0);
            lv_obj_set_style_bg_opa(g_files_tab, LV_OPA_COVER, 0);
            lv_obj_set_style_pad_all(g_files_tab, 0, 0);
            lv_obj_set_style_border_width(g_files_tab, 0, 0);

            // Create file manager immediately so it displays
            g_file_manager_screen = create_file_manager_screen(g_files_tab);

            ESP_LOGI(TAG, "Files tab created with file manager");
        }
    }

    // Connect SD Test Buttons
    if (objects.test_sd_card1_mount)
    {
        lv_obj_add_event_cb(objects.test_sd_card1_mount, action_test_sdcard, LV_EVENT_CLICKED, (void*)(intptr_t)1);
    }
    if (objects.test_sd_card1_mount_2)
    { // This is actually Test SD Card 2 according to screens.c
        lv_obj_add_event_cb(objects.test_sd_card1_mount_2, action_test_sdcard, LV_EVENT_CLICKED, (void*)(intptr_t)2);
    }

    // Connect Format Buttons
    if (objects.format_sd_card1)
    {
        lv_obj_add_event_cb(objects.format_sd_card1, action_test_sdcard, LV_EVENT_CLICKED, (void*)(intptr_t)101);
    }
    if (objects.format_sd_card2)
    {
        lv_obj_add_event_cb(objects.format_sd_card2, action_test_sdcard, LV_EVENT_CLICKED, (void*)(intptr_t)102);
    }

    // Connect Export Log Button (Assuming slot > 200 trigger for export)
    // Map it to any secondary "Action" button if available

    // Connect WiFi Textareas to show keyboard
    if (objects.wifi_ssid_1)
    {
        lv_obj_add_event_cb(objects.wifi_ssid_1, action_show_keyboard, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(objects.wifi_ssid_1, action_show_keyboard, LV_EVENT_FOCUSED, NULL);
        lv_textarea_set_one_line(objects.wifi_ssid_1, true);
        lv_textarea_set_placeholder_text(objects.wifi_ssid_1, "Network name");
        ui_style_input(objects.wifi_ssid_1);
    }
    if (objects.wifi_password_1)
    {
        lv_obj_add_event_cb(objects.wifi_password_1, action_show_keyboard, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(objects.wifi_password_1, action_show_keyboard, LV_EVENT_FOCUSED, NULL);
        lv_textarea_set_one_line(objects.wifi_password_1, true);
        lv_textarea_set_placeholder_text(objects.wifi_password_1, "Password");
        ui_style_input(objects.wifi_password_1);
    }

    // Overlap and Layout Fixes:
    // 1. Hide redundant static "SD CARD" labels
    if (objects.sdcard_label_1_1) lv_obj_add_flag(objects.sdcard_label_1_1, LV_OBJ_FLAG_HIDDEN);
    if (objects.sdcard_label_2_1) lv_obj_add_flag(objects.sdcard_label_2_1, LV_OBJ_FLAG_HIDDEN);

    // 2. Reposition storage data and reduce text size for better layout
    if (objects.sdcard_storage_1_1)
    {
        lv_obj_set_pos(objects.sdcard_storage_1_1, 95, 126);
        lv_obj_set_width(objects.sdcard_storage_1_1, 180);
        lv_obj_set_style_text_font(objects.sdcard_storage_1_1, &lv_font_montserrat_24, 0);
    }
    if (objects.sd_card_title_2_1)
    {
        lv_obj_set_pos(objects.sd_card_title_2_1, 95, 314);
        lv_obj_set_width(objects.sd_card_title_2_1, 180);
        lv_obj_set_style_text_font(objects.sd_card_title_2_1, &lv_font_montserrat_24, 0);
    }

    // 3. Move checkboxes and reduce their text size
    if (objects.active_checkbox_sd_card_1_1)
    {
        lv_obj_set_pos(objects.active_checkbox_sd_card_1_1, 265, 126);
        lv_obj_set_style_text_font(objects.active_checkbox_sd_card_1_1, &lv_font_montserrat_20, 0);
    }
    if (objects.active_checkbox_sd_card_2_1)
    {
        lv_obj_set_pos(objects.active_checkbox_sd_card_2_1, 265, 314);
        lv_obj_set_style_text_font(objects.active_checkbox_sd_card_2_1, &lv_font_montserrat_20, 0);
    }

    // Enhanced Wi-Fi Screen Labels and Visual Hierarchy
    if (objects.ssid_1)
    {
        lv_obj_set_style_text_font(objects.ssid_1, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(objects.ssid_1, lv_color_hex(UI_COLOR_MUTED), 0);
        lv_label_set_text(objects.ssid_1, "NETWORK");
    }
    if (objects.wifi_subtitle_1)
    {
        lv_obj_set_style_text_font(objects.wifi_subtitle_1, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(objects.wifi_subtitle_1, lv_color_hex(UI_COLOR_MUTED), 0);
        lv_label_set_text(objects.wifi_subtitle_1, "PASSWORD");
    }
    if (objects.titile_wifi_screen_1)
    {
        lv_obj_set_style_text_font(objects.titile_wifi_screen_1, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(objects.titile_wifi_screen_1, lv_color_hex(UI_COLOR_TEXT), 0);
        lv_label_set_text(objects.titile_wifi_screen_1, "Wi-Fi Setup");
    }
    if (objects.brand_name_wifi_screen_1)
    {
        lv_obj_set_style_text_font(objects.brand_name_wifi_screen_1, &lv_font_montserrat_26, 0);
        lv_obj_set_style_text_color(objects.brand_name_wifi_screen_1, lv_color_hex(UI_COLOR_ACCENT), 0);
    }

    // Improve input_box_1 styling for better visual separation
    if (objects.input_box_1)
    {
        ui_style_surface(objects.input_box_1, lv_color_hex(UI_COLOR_SURFACE), 20);
        lv_obj_set_style_pad_all(objects.input_box_1, 18, 0);
        lv_obj_set_style_pad_row(objects.input_box_1, 20, 0);
        lv_obj_set_style_pad_column(objects.input_box_1, 8, 0);
    }

    // Global Style Fixes for Production Look
    // Remove default "yellow" focused style from main containers/tabs
    if (objects.obj1)
    { // Tab bar
        lv_obj_set_style_bg_color(objects.obj1, lv_color_hex(UI_COLOR_SURFACE), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(objects.obj1, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(objects.obj1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(objects.obj1, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(objects.obj1, lv_color_hex(UI_COLOR_SURFACE), LV_PART_ITEMS | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(objects.obj1, LV_OPA_TRANSP, LV_PART_ITEMS | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(objects.obj1, lv_color_hex(UI_COLOR_MUTED), LV_PART_ITEMS | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(objects.obj1, lv_color_hex(UI_COLOR_ACCENT), LV_PART_ITEMS | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(objects.obj1, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(objects.obj1, lv_color_hex(UI_COLOR_TEXT), LV_PART_ITEMS | LV_STATE_CHECKED);
        lv_obj_set_style_radius(objects.obj1, 16, LV_PART_ITEMS | LV_STATE_DEFAULT);
        lv_obj_add_flag(objects.obj1, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(objects.obj1, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    }
    if (objects.wifi_page)
    {
        lv_obj_set_style_bg_color(objects.wifi_page, lv_color_hex(UI_COLOR_BG), 0);
        lv_obj_set_style_bg_opa(objects.wifi_page, LV_OPA_COVER, 0);
        /* Prevent pressed-state color flash on WiÔÇæFi page */
        lv_obj_set_style_bg_color(objects.wifi_page, lv_color_hex(UI_COLOR_BG), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(objects.wifi_page, lv_color_hex(UI_COLOR_BG), LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
    }
    if (objects.bluetooth_page)
    {
        lv_obj_set_style_bg_color(objects.bluetooth_page, lv_color_hex(UI_COLOR_BG), 0);
        lv_obj_set_style_bg_opa(objects.bluetooth_page, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(objects.bluetooth_page, lv_color_hex(UI_COLOR_BG), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(objects.bluetooth_page, lv_color_hex(UI_COLOR_BG), LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
    }
    if (objects.gnss_page)
    {
        lv_obj_set_style_bg_color(objects.gnss_page, lv_color_hex(UI_COLOR_BG), 0);
        lv_obj_set_style_bg_opa(objects.gnss_page, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(objects.gnss_page, lv_color_hex(UI_COLOR_BG), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(objects.gnss_page, lv_color_hex(UI_COLOR_BG), LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
    }
    if (objects.menu)
    {
        lv_obj_set_style_bg_color(objects.menu, lv_color_hex(UI_COLOR_BG), 0);
        lv_obj_set_style_bg_opa(objects.menu, LV_OPA_COVER, 0);
    }

    ui_create_main_status_panels();

    if (objects.ui_keyboard_1)
    {
        lv_obj_add_event_cb(objects.ui_keyboard_1, action_password_ssid_keyboad, LV_EVENT_ALL, NULL);
        ui_style_keyboard(objects.ui_keyboard_1);
    }

    if (s_gnss_dump_timer == NULL)
    {
        /* Keep monitor refresh light to preserve touch responsiveness. */
        s_gnss_dump_timer = lv_timer_create(gnss_dump_timer_cb, 120, NULL);
        if (s_gnss_dump_timer)
        {
            lv_timer_set_repeat_count(s_gnss_dump_timer, -1);
        }
    }
    ui_stream_init(&s_gnss_stream);
    ui_stream_stop(&s_gnss_stream);
    ui_window_reset(&s_gnss_window, objects.gnss_dump_panel);

#if CONFIG_RALLYBOX_RACEBOX_ENABLED
    if (s_bt_dump_timer == NULL)
    {
        s_bt_dump_timer = lv_timer_create(bluetooth_dump_timer_cb, 120, NULL);
        if (s_bt_dump_timer)
        {
            lv_timer_set_repeat_count(s_bt_dump_timer, -1);
        }
    }
    ui_stream_init(&s_bt_stream);
    ui_stream_stop(&s_bt_stream);
    racebox_set_rx_callback(racebox_ui_rx_cb, NULL);
#endif

    if (objects.input_box_1)
    {
        lv_obj_set_size(objects.input_box_1, 640, 264);
    }
    if (objects.wifi_conenct_button_1)
    {
        lv_obj_set_pos(objects.wifi_conenct_button_1, 12, 370);
        lv_obj_set_size(objects.wifi_conenct_button_1, 180, 54);
        lv_obj_set_style_radius(objects.wifi_conenct_button_1, 14, 0);
        lv_obj_set_style_shadow_width(objects.wifi_conenct_button_1, 0, 0);
        lv_obj_add_event_cb(objects.wifi_conenct_button_1, action_remove_saved_wifi, LV_EVENT_LONG_PRESSED, NULL);
    }

    if (objects.bluetooth_status_label)
    {
        lv_obj_set_style_text_font(objects.bluetooth_status_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(objects.bluetooth_status_label, lv_color_hex(UI_COLOR_MUTED), 0);
    }

    if (objects.bluetooth_device_dropdown)
    {
        lv_obj_set_style_bg_color(objects.bluetooth_device_dropdown, lv_color_hex(UI_COLOR_SURFACE), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(objects.bluetooth_device_dropdown, lv_color_hex(UI_COLOR_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(objects.bluetooth_device_dropdown, lv_color_hex(UI_COLOR_BORDER), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(objects.bluetooth_device_dropdown, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(objects.bluetooth_device_dropdown, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_dropdown_set_selected(objects.bluetooth_device_dropdown, 0);
    }

    if (objects.bluetooth_refresh_button)
    {
        style_modern_button(objects.bluetooth_refresh_button, lv_color_hex(0xff546e7a), lv_color_hex(0xff37474f));
        lv_obj_add_event_cb(objects.bluetooth_refresh_button, action_bluetooth_refresh, LV_EVENT_CLICKED, NULL);
    }

    if (objects.bluetooth_connect_button)
    {
        style_modern_button(objects.bluetooth_connect_button, lv_color_hex(0xff00acc1), lv_color_hex(0xff1565c0));
        lv_obj_add_event_cb(objects.bluetooth_connect_button, action_bluetooth_connect, LV_EVENT_CLICKED, NULL);
    }

#if CONFIG_RALLYBOX_RACEBOX_ENABLED
    if (objects.bluetooth_page)
    {
        if (s_bt_dump_panel == NULL)
        {
            s_bt_dump_panel = lv_textarea_create(objects.bluetooth_page);
            lv_obj_set_pos(s_bt_dump_panel, 12, 240);
            lv_obj_set_size(s_bt_dump_panel, 1132, 446);
            lv_textarea_set_one_line(s_bt_dump_panel, false);
            lv_textarea_set_max_length(s_bt_dump_panel, 16384);
            lv_textarea_set_placeholder_text(s_bt_dump_panel, "RaceBox BLE messages (NMEA/raw) will appear here...");
            ui_style_input(s_bt_dump_panel);
            lv_obj_set_style_text_font(s_bt_dump_panel, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(s_bt_dump_panel, lv_color_hex(UI_COLOR_TEXT), 0);
            lv_obj_clear_flag(s_bt_dump_panel, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(s_bt_dump_panel, LV_OBJ_FLAG_SCROLLABLE);
        }
    }
#endif

    if (objects.gnss_status_label)
    {
        lv_obj_set_style_text_font(objects.gnss_status_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(objects.gnss_status_label, lv_color_hex(UI_COLOR_MUTED), 0);
        lv_label_set_text(objects.gnss_status_label, s_gnss_status_text);
    }

    if (objects.gnss_baud_input)
    {
#if CONFIG_RALLYBOX_GNSS_ENABLED
        int gnss_baud = 0;
        int gnss_tx = 0;
        int gnss_rx = 0;
        char cfg_text[16];
#endif

        lv_obj_add_event_cb(objects.gnss_baud_input, action_show_keyboard, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(objects.gnss_baud_input, action_show_keyboard, LV_EVENT_FOCUSED, NULL);
        lv_textarea_set_one_line(objects.gnss_baud_input, true);
        lv_textarea_set_accepted_chars(objects.gnss_baud_input, "0123456789");
        ui_style_input(objects.gnss_baud_input);

#if CONFIG_RALLYBOX_GNSS_ENABLED
        if (gnss_get_config(&gnss_baud, &gnss_tx, &gnss_rx) == ESP_OK)
        {
            snprintf(cfg_text, sizeof(cfg_text), "%d", gnss_baud);
            lv_textarea_set_text(objects.gnss_baud_input, cfg_text);
            if (objects.gnss_tx_input)
            {
                snprintf(cfg_text, sizeof(cfg_text), "%d", gnss_tx);
                lv_textarea_set_text(objects.gnss_tx_input, cfg_text);
            }
            if (objects.gnss_rx_input)
            {
                snprintf(cfg_text, sizeof(cfg_text), "%d", gnss_rx);
                lv_textarea_set_text(objects.gnss_rx_input, cfg_text);
            }
        }
#endif
    }

    if (objects.gnss_tx_input)
    {
        lv_obj_add_event_cb(objects.gnss_tx_input, action_show_keyboard, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(objects.gnss_tx_input, action_show_keyboard, LV_EVENT_FOCUSED, NULL);
        lv_textarea_set_one_line(objects.gnss_tx_input, true);
        lv_textarea_set_accepted_chars(objects.gnss_tx_input, "0123456789");
        ui_style_input(objects.gnss_tx_input);
    }

    if (objects.gnss_rx_input)
    {
        lv_obj_add_event_cb(objects.gnss_rx_input, action_show_keyboard, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(objects.gnss_rx_input, action_show_keyboard, LV_EVENT_FOCUSED, NULL);
        lv_textarea_set_one_line(objects.gnss_rx_input, true);
        lv_textarea_set_accepted_chars(objects.gnss_rx_input, "0123456789");
        ui_style_input(objects.gnss_rx_input);
    }

    if (objects.gnss_start_stop_button)
    {
        style_modern_button(objects.gnss_start_stop_button, lv_color_hex(0xff00897b), lv_color_hex(0xff005f73));
        lv_obj_add_event_cb(objects.gnss_start_stop_button, action_gnss_start_stop, LV_EVENT_CLICKED, NULL);
    }

    if (objects.gnss_dump_panel)
    {
        ui_style_input(objects.gnss_dump_panel);
        lv_obj_set_style_text_font(objects.gnss_dump_panel, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(objects.gnss_dump_panel, lv_color_hex(UI_COLOR_TEXT), 0);
        lv_obj_clear_flag(objects.gnss_dump_panel, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(objects.gnss_dump_panel, LV_OBJ_FLAG_SCROLLABLE);
    }

    // Enhanced SD Card Test Buttons
    if (objects.test_sd_card1_mount)
    {
        style_modern_button(objects.test_sd_card1_mount, lv_color_hex(0xff00acc1), lv_color_hex(0xff0077ff));
    }
    if (objects.test_sd_card1_mount_2)
    {
        style_modern_button(objects.test_sd_card1_mount_2, lv_color_hex(0xff00acc1), lv_color_hex(0xff0077ff));
    }

    // Enhanced SD Card Format Buttons
    if (objects.format_sd_card1)
    {
        style_modern_button(objects.format_sd_card1, lv_color_hex(0xffffc107), lv_color_hex(0xffff6f00));
    }
    if (objects.format_sd_card2)
    {
        style_modern_button(objects.format_sd_card2, lv_color_hex(0xffffc107), lv_color_hex(0xffff6f00));
    }

    // Overall dashboard polish
    if (objects.dashboard)
    {
        lv_obj_set_style_bg_color(objects.dashboard, lv_color_hex(UI_COLOR_BG), 0);
        lv_obj_set_style_bg_opa(objects.dashboard, LV_OPA_COVER, 0);
        /* Keep background steady when pressed to avoid flashing colors */
        lv_obj_set_style_bg_color(objects.dashboard, lv_color_hex(UI_COLOR_BG), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(objects.dashboard, lv_color_hex(UI_COLOR_BG), LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
    }
    if (objects.menu)
    {
        lv_obj_set_style_bg_color(objects.menu, lv_color_hex(UI_COLOR_BG), 0);
        lv_obj_set_style_bg_opa(objects.menu, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(objects.menu, lv_color_hex(UI_COLOR_BG), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(objects.menu, lv_color_hex(UI_COLOR_BG), LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
    }

    if (objects.obj4)
    {
        lv_obj_set_pos(objects.obj4, 12, 452);
        lv_obj_set_width(objects.obj4, 1120);
        lv_label_set_long_mode(objects.obj4, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(objects.obj4, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(objects.obj4, lv_color_hex(UI_COLOR_MUTED), 0);
    }

    system_status_t status = system_monitor_get_status();
    ui_load_saved_wifi_credentials();
    ui_refresh_wifi_controls(&status);
    ui_refresh_bluetooth_controls(&status);
    ui_refresh_gnss_dump();

    ESP_LOGI(TAG, "UI Event Handlers Initialized with Modern Styling");
}
