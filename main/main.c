/**
 * @file main.c
 * @brief Rallybox ESP32-P4 Main Application Entry Point
 * @author Rallybox Development Team
 * @developer Akhil
 *
 * This file implements the core application initialization and system orchestration
 * for the Rallybox rally computer firmware running on ESP32-P4. It coordinates:
 * - Display initialization (MIPI DSI to ILI9881C LCD via LVGL v9)
 * - WiFi stack initialization and event handling
 * - SD card dual-slot management (SDMMC + SDSPI)
 * - Background system monitoring and periodic updates
 * - SNTP time synchronization (IST timezone)
 * - Touch input event processing
 *
 * System Architecture:
 * - Core 0: LVGL rendering pipeline (real-time, priority 5)
 * - Core 1: System monitoring & peripheral testing (background, priority 4)
 *
 * Boot Sequence:
 * 1. NVS Flash initialization
 * 2. Display & LVGL setup with boot screen
 * 3. Background task spawned for system init (SD cards, WiFi, SNTP)
 * 4. 10-second boot animation, then transition to main dashboard
 * 5. WiFi restoration from saved credentials (non-blocking)
 *
 * File Dependencies:
 * - system_monitor.h: System status tracking, peripherals
 * - sd_card.h: Dual SD card initialization & management
 * - display_brightness.h: Backlight PWM control
 * - ui_logic.c: UI update functions (linked externally)
 * - bsp/esp32_p4_wifi6_touch_lcd_x.h: Hardware abstraction layer
 *
 * @note This firmware uses FreeRTOS with esp-idf v6.x framework.
 * @note WiFi and SNTP operations are non-blocking to ensure smooth UI.
 * @note All system updates are protected by display mutex to prevent TEARING.
 */

#include "lvgl.h"                             ///< LVGL graphics library (v9.4)
#include "ui.h"                               ///< EEZ Studio generated UI definitions
#include "screens.h"                          ///< Screen switching functions
#include "bsp/esp32_p4_wifi6_touch_lcd_x.h"   ///< Board support package - LCD driver
#include "system_monitor.h"                   ///< System status monitoring module
#include "sd_card.h"                          ///< Dual SD card control (SDMMC + SDSPI)
#include "display_brightness.h"               ///< Display backlight PWM control
#if CONFIG_RALLYBOX_RACEBOX_ENABLED
#include "racebox.h"                          ///< RaceBox BLE transport module
#endif
#if CONFIG_RALLYBOX_GNSS_ENABLED
#include "gnss.h"                             ///< GNSS UART transport module
#endif
#include "freertos/FreeRTOS.h"                ///< Real-time OS kernel
#include "freertos/task.h"                    ///< Task management (multitasking)
#include "esp_log.h"                          ///< ESP-IDF logging framework
#include "nvs_flash.h"                        ///< Non-volatile storage (flash config)
#include <string.h>                           ///< C standard string utilities
#include "freertos/event_groups.h"            ///< Event group synchronization
#include "esp_system.h"                       ///< ESP-IDF system utilities
#include "esp_wifi.h"                         ///< WiFi driver & stack
#include "esp_event.h"                        ///< Event loop framework
#include "lwip/err.h"                         ///< LWIP TCP/IP error codes
#include "lwip/sys.h"                         ///< LWIP system definitions
#include "esp_sntp.h"                         ///< SNTP time synchronization
#include "esp_hosted_transport_config.h"     ///< ESP-Hosted SDIO transport tuning
#include <time.h>                             ///< C standard time library
#include "driver/gpio.h"                      ///< GPIO driver
#include "debug_flags.h"
#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_RALLYBOX_WIFI_MAXIMUM_RETRY  ///< Maximum WiFi connection retry attempts

 /// WiFi Security Configuration (WPA3-PSK with H2E)
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER ""
/// Authentication threshold: require WPA2 or higher
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK

/// Event group for WiFi connection synchronization
static EventGroupHandle_t s_wifi_event_group;

/// Event bits for WiFi status
#define WIFI_CONNECTED_BIT BIT0              ///< WiFi connected successfully
#define WIFI_FAIL_BIT      BIT1              ///< WiFi connection failed

/// Retry counter (synchronized with system monitor)
int s_retry_num = 0;

/// Log tag for this module (used by ESP_LOGI, ESP_LOGW, etc.)
static const char* TAG = "app_main";

static void configure_hosted_sdio_transport(void)
{
    struct esp_hosted_sdio_config sdio_config;
    esp_hosted_transport_err_t result;

    if (esp_hosted_transport_is_config_valid())
    {
        ESP_LOGI(TAG, "ESP-Hosted transport config already set; keeping existing SDIO settings");
        return;
    }

    sdio_config = INIT_DEFAULT_HOST_SDIO_CONFIG();
    sdio_config.clock_freq_khz = 25000;
    sdio_config.bus_width = 4;
    sdio_config.tx_queue_size = 64;
    sdio_config.rx_queue_size = 64;

    result = esp_hosted_sdio_set_config(&sdio_config);
    if (result == ESP_TRANSPORT_OK)
    {
        ESP_LOGI(TAG,
            "Configured ESP-Hosted SDIO transport: %lu kHz, %u-bit, TX queue=%u, RX queue=%u",
            (unsigned long)sdio_config.clock_freq_khz,
            (unsigned)sdio_config.bus_width,
            (unsigned)sdio_config.tx_queue_size,
            (unsigned)sdio_config.rx_queue_size);
    }
    else
    {
        ESP_LOGW(TAG, "Unable to override ESP-Hosted SDIO transport config: %d", (int)result);
    }
}

/* ═════════════════════════════════════════════════════════════════════════
 * EXTERNAL UI UPDATE FUNCTION PROTOTYPES
 *
 * These functions bridge the system monitoring layer with the UI layer,
 * allowing real-time dashboard updates without tight coupling.
 * All are called from within display_lock() context to prevent tearing.
 * ═════════════════════════════════════════════════════════════════════════ */

 /// Update uptime clock display (HH:MM:SS format)
extern void ui_logic_set_uptime(uint32_t seconds);

/// Update SD card activity indicator (active/inactive animation)
extern void ui_logic_update_sd_status(int slot, bool active);

/// Update SD card capacity and usage bars
extern void ui_logic_update_storage_info(int slot, bool detected, uint64_t capacity_mb, uint64_t used_mb);

/// Update system load indicators (CPU %, free heap)
extern void ui_logic_update_system_stats(uint32_t cpu_load, uint32_t errors);

/// Update WiFi status live (connection state, RSSI, IP)
extern void ui_logic_update_wifi_live(const system_status_t* status);

/// Show SD card event message
extern void ui_show_sd_card_event(int slot, bool mounted);

/**
 * @brief Touch event callback for debugging coordinates
 *
 * This callback logs touch coordinates to the serial log whenever the screen
 * is pressed. Can be attached to LVGL objects or used for system-wide touch
 * event debugging. Coordinates are in screen pixels (0-720 x, 0-1280 y).
 *
 * @param e LVGL event structure containing touch information
 */
static void touch_debug_callback(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED)
    {
        lv_indev_t* indev = lv_indev_get_act();
        lv_point_t point;
        lv_indev_get_point(indev, &point);
        TOUCH_DEBUG_LOGI(TAG, "Touch pressed at: x=%d, y=%d", point.x, point.y);
    }
}

/**
 * @brief Background task for system monitoring and peripheral testing
 *
 * PRIORITY: 4 (background task, lower than UI rendering which is priority 5)
 * CORE: 1 (runs on core 1 to keep core 0 free for LVGL)
 * STACK: 8192 bytes
 *
 * Responsibilities:
 * 1. Boot Screen Timing: Holds boot screen for exactly 10 seconds for visual feedback
 * 2. SD Card Detection: Initializes dual SD card slots (SDMMC + SDSPI) with error handling
 * 3. System Monitoring: Periodic update of system status (once per second at 1Hz)
 * 4. Dashboard Updates: Sends uptime, storage info, WiFi stats to UI layer
 * 5. Storage Statistics: Retrieves and displays SD card capacity/usage information
 *
 * Boot Timeline:
 * - T+0s: Boot screen active (lock display, show splash)
 * - T+1s: Attempt SD1 (SDMMC) initialization
 * - T+1.5s: Attempt SD2 (SDSPI) initialization
 * - T+10s: Begin transition to main dashboard
 * - T+10s+: Continuous 4x-divider (250ms * 4 = 1Hz) monitoring loop
 *
 * Update Rate:
 * - Base loop: 250ms (4Hz ticks)
 * - Slow updates: Every 4 ticks = 1Hz (uptime, storage, stats)
 * - WiFi live: Every tick = 4Hz (signal indicator updates)
 *
 * @param pvParameters Unused (FreeRTOS task parameter)
 */
static void system_monitor_task(void* pvParameters)
{
    /* Record task-start so we can hold the boot screen for exactly 10 s */
    const TickType_t boot_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(10000);

    ESP_LOGI(TAG, "System monitor task started");

    /* ── Initial SD card detection ──────────────────────────────────────── */
    // Delayed SD detection to let I2C bus settle first
    vTaskDelay(pdMS_TO_TICKS(1000));
    uint8_t sd1_ok = system_sdcard1_init();   // SDMMC slot 0  (GPIO 39-44)
    vTaskDelay(pdMS_TO_TICKS(500));
    // Re-enable SD2 (SDSPI on SPI3). Short delay after init to let bus settle.
    uint8_t sd2_ok = system_sdcard2_init();   // SDSPI SPI3    (GPIO 13,14,21,23)
    vTaskDelay(pdMS_TO_TICKS(300));

    if (!sd1_ok && !sd2_ok)
    {
        ESP_LOGE(TAG, "CRITICAL: No SD card detected — at least one is required.");
    }
    else if (!sd1_ok)
    {
        ESP_LOGW(TAG, "SD Card 1 absent — running on SD Card 2 only.");
    }
    else if (!sd2_ok)
    {
        ESP_LOGW(TAG, "SD Card 2 absent — running on SD Card 1 only.");
    }
    else
    {
        ESP_LOGI(TAG, "Both SD cards mounted successfully.");
    }

    bool sd1_auto_control_enabled = (sd1_ok != 0);
    bool sd2_auto_control_enabled = (sd2_ok != 0);

    /* ── WiFi ────────────────────────────────────────────────────────────── */
    /* WiFi initialization moved to app_main to avoid race with UI callbacks */
    // system_wifi_init(); // was here; initialized from app_main

    /* ── Wait for boot screen duration, then switch to main dashboard ───── */
    TickType_t now = xTaskGetTickCount();
    if ((TickType_t)(boot_deadline - now) <= pdMS_TO_TICKS(10000))
    {
        vTaskDelay(boot_deadline - now);
    }

    ESP_LOGI(TAG, "Boot complete — loading main dashboard");
    if (bsp_display_lock((uint32_t)-1) == ESP_OK)
    {
        loadScreen(SCREEN_ID_DASHBOARD);
        // ui_update_sdcard_status(sd1_ok, sd2_ok);
        bsp_display_unlock();
        ESP_LOGI(TAG, "Main dashboard active");
    }
    else
    {
        ESP_LOGE(TAG, "FATAL: display lock failed — screen stuck on boot screen");
    }

    /* ── Periodic 1 Hz monitoring loop ──────────────────────────────────── */
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(250);
    uint8_t slow_tick_divider = 0;
    uint32_t gpio_read_count = 0;
    int prev_gpio45_level = gpio_get_level(SD1_CONTROL_PIN);
    int prev_gpio29_level = gpio_get_level(SD2_CONTROL_PIN);

    while (1)
    {
        xTaskDelayUntil(&xLastWakeTime, xFrequency);

        // Check GPIO45 and GPIO29 for independent SD card control
        int gpio45_level = gpio_get_level(SD1_CONTROL_PIN);
        int gpio29_level = gpio_get_level(SD2_CONTROL_PIN);

        gpio_read_count++;

        // Log GPIO states every 10 cycles (every 2.5 seconds) for diagnostics
        if (gpio_read_count % 10 == 0)
        {
            GPIO_SD_DEBUG_LOGI(TAG, "GPIO states - SD1(GPIO45)=%d, SD2(GPIO29)=%d", gpio45_level, gpio29_level);
        }

        // Handle SD Card 1 control (GPIO45)
        if (sd1_auto_control_enabled && gpio45_level != prev_gpio45_level)
        {
            GPIO_SD_DEBUG_LOGI(TAG, "*** GPIO45 CHANGED: %d -> %d (LOW=mount SD1, HIGH=unmount SD1) ***", prev_gpio45_level, gpio45_level);

            if (bsp_display_lock((uint32_t)-1) == ESP_OK)
            {
                if (gpio45_level == 0)
                {
                    // Mount SD Card 1
                    ESP_LOGI(TAG, "Action: GPIO45 LOW - Mounting SD Card 1");

                    if (!sd_card1_is_mounted())
                    {
                        if (sd_card1_init() == ESP_OK)
                        {
                            ESP_LOGI(TAG, "SD Card 1 mounted successfully");
                            ui_show_sd_card_event(1, true);
                            uint64_t cap1 = 0, used1 = 0;
                            sd_card_get_info(1, &cap1, &used1);
                            ESP_LOGI(TAG, "SD Card 1: %llu MB total, %llu MB used", cap1, used1);
                            ui_logic_update_storage_info(1, true, cap1, used1);
                        }
                        else
                        {
                            ESP_LOGE(TAG, "Failed to mount SD Card 1");
                            ui_show_sd_card_event(1, false);
                            ui_logic_update_storage_info(1, false, 0, 0);
                        }
                    }
                    else
                    {
                        ESP_LOGI(TAG, "SD Card 1 already mounted");
                    }
                }
                else
                {
                    // Unmount SD Card 1
                    ESP_LOGI(TAG, "Action: GPIO45 HIGH - Unmounting SD Card 1");

                    if (sd_card1_is_mounted())
                    {
                        sd_card1_deinit();
                        ESP_LOGI(TAG, "SD Card 1 unmounted successfully");
                        ui_show_sd_card_event(1, false);
                        ui_logic_update_storage_info(1, false, 0, 0);
                    }
                    else
                    {
                        ESP_LOGI(TAG, "SD Card 1 already unmounted");
                    }
                }
                bsp_display_unlock();
            }

            prev_gpio45_level = gpio45_level;
            vTaskDelay(pdMS_TO_TICKS(100)); // Debounce delay
        }
        else if (!sd1_auto_control_enabled)
        {
            prev_gpio45_level = gpio45_level;
        }

        // Handle SD Card 2 control (GPIO29)
        if (sd2_auto_control_enabled && gpio29_level != prev_gpio29_level)
        {
            GPIO_SD_DEBUG_LOGI(TAG, "*** GPIO29 CHANGED: %d -> %d (LOW=mount SD2, HIGH=unmount SD2) ***", prev_gpio29_level, gpio29_level);

            if (bsp_display_lock((uint32_t)-1) == ESP_OK)
            {
                if (gpio29_level == 0)
                {
                    // Mount SD Card 2
                    ESP_LOGI(TAG, "Action: GPIO29 LOW - Mounting SD Card 2");

                    if (!sd_card2_is_mounted())
                    {
                        if (sd_card2_init() == ESP_OK)
                        {
                            ESP_LOGI(TAG, "SD Card 2 mounted successfully");
                            ui_show_sd_card_event(2, true);
                            uint64_t cap2 = 0, used2 = 0;
                            sd_card_get_info(2, &cap2, &used2);
                            ESP_LOGI(TAG, "SD Card 2: %llu MB total, %llu MB used", cap2, used2);
                            ui_logic_update_storage_info(2, true, cap2, used2);
                        }
                        else
                        {
                            ESP_LOGE(TAG, "Failed to mount SD Card 2");
                            ui_show_sd_card_event(2, false);
                            ui_logic_update_storage_info(2, false, 0, 0);
                        }
                    }
                    else
                    {
                        ESP_LOGI(TAG, "SD Card 2 already mounted");
                    }
                }
                else
                {
                    // Unmount SD Card 2
                    ESP_LOGI(TAG, "Action: GPIO29 HIGH - Unmounting SD Card 2");

                    if (sd_card2_is_mounted())
                    {
                        sd_card2_deinit();
                        ESP_LOGI(TAG, "SD Card 2 unmounted successfully");
                        ui_show_sd_card_event(2, false);
                        ui_logic_update_storage_info(2, false, 0, 0);
                    }
                    else
                    {
                        ESP_LOGI(TAG, "SD Card 2 already unmounted");
                    }
                }
                bsp_display_unlock();
            }

            prev_gpio29_level = gpio29_level;
            vTaskDelay(pdMS_TO_TICKS(100)); // Debounce delay
        }
        else if (!sd2_auto_control_enabled)
        {
            prev_gpio29_level = gpio29_level;
        }

        slow_tick_divider++;
        if (slow_tick_divider >= 4)
        {
            slow_tick_divider = 0;
            system_monitor_update();
        }

        /* Push uptime and SD status to the dashboard label */
        if (bsp_display_lock((uint32_t)-1) == ESP_OK)
        {
            system_status_t status = system_monitor_get_status();
            ui_logic_update_wifi_live(&status);

            if (slow_tick_divider == 0)
            {
                ui_logic_set_uptime(status.uptime_seconds);

                // Total system errors
                uint32_t total_errors = status.sdcard1_error_count + status.sdcard2_error_count;
                ui_logic_update_system_stats(status.cpu_load_percent, total_errors);

                // Update SD Card 1 Info (Slot 0)
                uint64_t cap1 = 0, used1 = 0;
                sd_card_get_info(1, &cap1, &used1);
                ui_logic_update_storage_info(1, sd1_ok, cap1, used1);

                // Update SD Card 2 Info (Slot 1)
                uint64_t cap2 = 0, used2 = 0;
                sd_card_get_info(2, &cap2, &used2);
                ui_logic_update_storage_info(2, sd2_ok, cap2, used2);
            }

            bsp_display_unlock();
        }

        /* Periodic SD card I/O tests (staggered to avoid bus conflicts) */
        static uint32_t test_counter = 0;
        test_counter++;
    }
}

/**
 * @brief Background task that connects WiFi using saved credentials
 *
 * This task is spawned during WIFI_EVENT_STA_START to avoid blocking the
 * event loop thread. It retrieves stored WiFi credentials from NVS and
 * initiates connection. Running in a background task keeps the event loop
 * responsive and prevents watchdog timeouts.
 *
 * @param arg Unused parameter (FreeRTOS convention)
 */
static void wifi_connect_task(void* arg)
{
    /* Small delay so the event loop can fully settle before connecting */
    vTaskDelay(pdMS_TO_TICKS(200));
    system_wifi_connect_saved();
    vTaskDelete(NULL);
}

/**
 * @brief WiFi event handler for STA (Station) mode
 *
 * Key Events Handled:
 * - WIFI_EVENT_STA_START: WiFi stack started, trigger connection task
 * - WIFI_EVENT_STA_DISCONNECTED: Lost connection, retry with backoff
 * - IP_EVENT_STA_GOT_IP: Successfully acquired IP, update display
 *
 * Architecture:
 * - Non-blocking: All operations use events, no waits
 * - Retry Logic: Up to EXAMPLE_ESP_MAXIMUM_RETRY attempts with linear backoff
 * - State Tracking: Updates system_status for UI visibility
 *
 * @param arg Unused handler argument
 * @param event_base Event base (WIFI_EVENT or IP_EVENT)
 * @param event_id Specific event ID
 * @param event_data Event-specific data structure
 */
static void event_handler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        /* Offload credential lookup & connect to a background task so we never
           block the event-loop / LVGL task. */
        xTaskCreate(wifi_connect_task, "wifi_conn", 4096, NULL, 4, NULL);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_event_sta_disconnected_t* disconnect_event = (wifi_event_sta_disconnected_t*)event_data;
        system_status_t status = system_monitor_get_status();
        bool should_retry = (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) &&
            (status.wifi_state == SYSTEM_WIFI_STATE_CONNECTING ||
                status.wifi_state == SYSTEM_WIFI_STATE_CONNECTED);

        ESP_LOGW(TAG,
            "WiFi disconnected: reason=%u state=%u retry=%d/%d",
            disconnect_event ? (unsigned)disconnect_event->reason : 0U,
            (unsigned)status.wifi_state,
            s_retry_num,
            EXAMPLE_ESP_MAXIMUM_RETRY);

        system_monitor_note_wifi_disconnect(
            disconnect_event ? disconnect_event->reason : 0U,
            should_retry,
            should_retry ? (uint32_t)(s_retry_num + 1) : (uint32_t)s_retry_num);

        if (should_retry)
        {
            system_monitor_set_wifi_state(SYSTEM_WIFI_STATE_CONNECTING);
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP (%d/%d)", s_retry_num, EXAMPLE_ESP_MAXIMUM_RETRY);
        }
        else
        {
            system_monitor_set_wifi_state(SYSTEM_WIFI_STATE_DISCONNECTED);
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGI(TAG, "connect to the AP fail");
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;

        /* Update WiFi status immediately for UI responsiveness */
        system_monitor_set_wifi_connected(true);
        system_monitor_set_wifi_state(SYSTEM_WIFI_STATE_CONNECTED);

        char ip_str[16];
        esp_ip4addr_ntoa(&event->ip_info.ip, ip_str, sizeof(ip_str));
        system_monitor_set_wifi_ip(ip_str);

        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/**
 * @brief Initialize WiFi in Station (STA) mode
 *
 * Configuration:
 * - Auth Mode: WPA2-PSK minimum (WPA3 SAE if available)
 * - H2E Mode: Hash-to-Element mode for WPA3 compat
 * - Event Loop: Uses default event loop with async handlers
 * - IPv4: DHCP enabled
 *
 * Setup Steps:
 * 1. Create event group for sync points (CONNECTED_BIT, FAIL_BIT)
 * 2. Initialize netif (routing layer) and event loop
 * 3. Register WiFi and IP event handlers
 * 4. Configure STA mode with security parameters
 * 5. Start WiFi (non-blocking, events drive the flow)
 *
 * @note This function returns immediately; connection happens asynchronously
 * @note Credentials must be set separately via esp_wifi_set_config()
 */
void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &event_handler,
        NULL,
        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &event_handler,
        NULL,
        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished asynchronously.");
}

/**
 * @brief Background task that periodically prints system time
 *
 * Behavior:
 * - Logs current time every 1 second
 * - Waits for SNTP to sync (detects epoch year < 2020 as invalid)
 * - Respects timezone setting (IST = UTC+5:30 for India Standard Time)
 * - One-time warning if time not yet synced
 * - One-time confirmation when sync completes
 *
 * Format: "YYYY-MM-DD HH:MM:SS TZ"
 * Example: "2024-04-06 14:30:45 IST"
 *
 * @param pv Unused parameter (FreeRTOS convention)
 */
static void time_print_task(void* pv)
{
    time_t now;
    struct tm timeinfo;
    char strftime_buf[64];

    while (1)
    {
        time(&now);
        localtime_r(&now, &timeinfo);

        /* If the year is still near the epoch, SNTP hasn't set time yet. Skip
         * printing the bogus 1970 timestamp and log a single warning until
         * time is valid. */
        static bool warned_about_time = false;
        if (timeinfo.tm_year < (2020 - 1900))
        {
            if (!warned_about_time)
            {
                ESP_LOGW(TAG, "System time not set (year=%d). Waiting for SNTP sync...", timeinfo.tm_year + 1900);
                warned_about_time = true;
            }
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        /* If we had warned previously and now time is valid, inform once. */
        if (warned_about_time)
        {
            ESP_LOGI(TAG, "System time appears valid now (year=%d).", timeinfo.tm_year + 1900);
            warned_about_time = false;
        }

        strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S %Z", &timeinfo);
        IST_TIME_DEBUG_LOGI(TAG, "Current IST time: %s", strftime_buf);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

/* Forward declaration for the SNTP wait helper task. */
static void sntp_wait_task(void* pv);

/**
 * @brief Initialize SNTP time sync and spawn monitoring tasks
 *
 * Performs:
 * 1. SNTP Configuration: Sets NTP pool server to "in.pool.ntp.org"
 * 2. Timezone Setup: IST (UTC+5:30) for Indian Standard Time
 * 3. Background Tasks:
 *    - sntp_wait_task: Waits up to 20 seconds for sync with feedback
 *    - time_print_task: Periodic time logging every 1 second
 *
 * Configuration:
 * - SNTP Mode: POLL (periodic requests, no unicast)
 * - NTP Server: in.pool.ntp.org (round-robin pool)
 * - Timezone: IST (UTC+5:30)
 * - Update Interval: Every 60 minutes by default
 *
 * @note Both monitoring tasks are spawned immediately (non-blocking)
 * @note First sync typically takes 2-5 seconds on stable network
 */
static void start_sntp_and_log_time(void)
{
    ESP_LOGI(TAG, "Initializing SNTP Asynchronously");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "in.pool.ntp.org");
    esp_sntp_init();

    setenv("TZ", "IST-5:30", 1);
    tzset();

    /* Spawn a short-lived task to wait for SNTP to synchronize so we can
     * report fast feedback if timekeeping fails. */

    xTaskCreate(sntp_wait_task, "sntp_wait", 4096, NULL, 5, NULL);
    xTaskCreate(time_print_task, "time_print_task", 4096, NULL, 5, NULL);
}

/**
 * @brief Background task that waits for SNTP synchronization with timeout
 *
 * Purpose: Provide user feedback if time sync is delayed or failing
 *
 * Behavior:
 * - Polls sntp_get_sync_status() every 1 second
 * - Timeout: 20 seconds maximum wait
 * - Logs success/failure message to console
 * - Auto-terminates after reporting status
 *
 * Exit Conditions:
 * 1. SNTP_SYNC_STATUS_COMPLETED → "SNTP synchronized successfully"
 * 2. Timeout reached → "SNTP did not synchronize within timeout; system time may be incorrect"
 *
 * @param pv Unused parameter (FreeRTOS convention)
 *
 * @note Deletes itself after completion (no reuse)
 */
static void sntp_wait_task(void* pv)
{
    ESP_LOGI(TAG, "Waiting for SNTP sync (timeout 20s)...");
    int retries = 0;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && retries < 20)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        retries++;
    }
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET)
    {
        ESP_LOGW(TAG, "SNTP did not synchronize within timeout; system time may be incorrect.");
    }
    else
    {
        ESP_LOGI(TAG, "SNTP synchronized successfully.");
    }
    vTaskDelete(NULL);
}

/**
 * @brief Main application entry point - called by ESP-IDF after bootloader
 *
 * System Initialization Sequence:
 *
 * Phase 1: Foundation (Synchronous)
 * ├─ NVS Flash initialization (required for WiFi creds, config store)
 * ├─ GPIO routing table setup
 * └─ System clock verification
 *
 * Phase 2: Display Stack (Blocking until LVGL ready)
 * ├─ LCD driver initialization (MIPI DSI, GPIO config)
 * ├─ LVGL library setup (FreeRTOS tick initialization)
 * ├─ Display brightness init
 * ├─ Boot screen rendering (shows splash + system info)
 * └─ Touch input initialization (GT911 I2C calibration)
 *
 * Phase 3: Concurrency (Non-blocking)
 * ├─ WiFi stack init (event loop, handler registration)
 * ├─ Background task creation:
 * │  ├─ system_monitor_task (10s boot wait → SD/WiFi init → dashboard → monitoring)
 * │  ├─ sntp_wait_task (time sync with 20s timeout)
 * │  └─ time_print_task (periodic time logging)
 * └─ app_main returns to allow idle task + background processing
 *
 * Thread Safety:
 * - Display access protected by bsp_display_lock()
 * - WiFi operations via event loop (no blocking calls)
 * - System status via atomic reads (no mutex needed)
 *
 * Watchdog Considerations:
 * - No blocking operations exceed 2 seconds
 * - Background tasks cooperatively yield (vTaskDelay)
 * - Display lock timeout set to -1 (infinite, but non-blocking)
 *
 * @return Always 0 (main never exits)
 */
void app_main(void)
{
    /* Initialize NVS — required before any nvs_open() calls */
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // GT911 I2C reads fail permanently after the SD Card 1 LDO powers up
    // (brown-out on the touch supply; BSP_LCD_TOUCH_RST = NC so recovery is
    // impossible in software).  The resulting ~55 error-pairs/second saturate
    // the UART at 115200 baud, blocking the BSP LVGL task mutex and preventing
    // Suppress noisy logs to prevent UART saturation when touch reads fail
    esp_log_level_set("GT911", ESP_LOG_DEBUG);
    esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_DEBUG);

    bsp_display_cfg_t cfg = {
            .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
            .rotation = ESP_LV_ADAPTER_ROTATE_270,
            .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,
            .touch_flags = {
                .swap_xy = 1,
                .mirror_x = 0,
                .mirror_y = 1
            } };
    bsp_display_start_with_config(&cfg);
    // Keep the backlight OFF until LVGL has rendered the boot screen to
    // avoid a visible white flash during early initialization.
    bsp_display_backlight_off();
    bsp_display_lock(-1);

    ui_init();   // init EEZ UI

    // Professional: Initialize UI Logic and Event Listeners
    void ui_logic_init_events(void); // Prototype
    ui_logic_init_events();

    // Add touch debug callback to log coordinates
    lv_indev_t* touch_indev = lv_indev_get_next(NULL);
    while (touch_indev)
    {
        if (lv_indev_get_type(touch_indev) == LV_INDEV_TYPE_POINTER)
        {
            lv_indev_add_event_cb(touch_indev, touch_debug_callback, LV_EVENT_ALL, NULL);
            ESP_LOGI(TAG, "Touch debug callback added");
            break;
        }
        touch_indev = lv_indev_get_next(touch_indev);
    }

    // Initialize system monitoring
    system_monitor_init();
    configure_hosted_sdio_transport();
    system_wifi_init(); // The stub

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();
    start_sntp_and_log_time();
#if CONFIG_RALLYBOX_RACEBOX_ENABLED
    ESP_LOGI(TAG, "RaceBox auto-init deferred; initialize from Bluetooth Refresh to reduce startup load");
#endif
#if CONFIG_RALLYBOX_GNSS_ENABLED
    ESP_LOGI(TAG, "GNSS auto-init disabled; start GNSS from GNSS page using START");
#endif

    ESP_LOGI(TAG, "Displaying boot screen...");
    loadScreen(SCREEN_ID_BOOTINGSCREEN);

    bsp_display_unlock();

    /* Ensure LVGL flushes the initial boot screen to the panel before
     * enabling backlight. Call the LVGL timer handler once and give the
     * display driver a short moment to commit the frame. */
    lv_timer_handler();
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Initialise brightness hardware now that the boot screen has been
     * rendered. Start at 0% to avoid flashing, then ramp to the normal
     * brightness level smoothly. */
    display_brightness_init();
    const int target = 55;
    for (int p = 0; p <= target; p += 20)
    {
        display_brightness_set(p);
        vTaskDelay(pdMS_TO_TICKS(60));
    }
    /* Ensure final target reached */
    display_brightness_set(target);

    // Configure GPIO45 for SD Card 1 control
    // Note: Pull-down default ensures LOW state (mount) when GPIO45 is floating/not connected
    gpio_config_t sd1_control_conf = {
        .pin_bit_mask = (1ULL << SD1_CONTROL_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&sd1_control_conf);

    // Configure GPIO29 for SD Card 2 control
    // Note: Pull-down default ensures LOW state (mount) when GPIO29 is floating/not connected
    gpio_config_t sd2_control_conf = {
        .pin_bit_mask = (1ULL << SD2_CONTROL_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&sd2_control_conf);

    GPIO_SD_DEBUG_LOGI(TAG, "SD Card control pins configured:");
    GPIO_SD_DEBUG_LOGI(TAG, "  GPIO%d (SD Card 1): LOW=mount, HIGH=unmount", SD1_CONTROL_PIN);
    GPIO_SD_DEBUG_LOGI(TAG, "  GPIO%d (SD Card 2): LOW=mount, HIGH=unmount", SD2_CONTROL_PIN);

    // Diagnostic: Read GPIO states immediately to verify they're accessible
    int initial_gpio45 = gpio_get_level(SD1_CONTROL_PIN);
    int initial_gpio29 = gpio_get_level(SD2_CONTROL_PIN);
    GPIO_SD_DEBUG_LOGI(TAG, "GPIO initial states after config: GPIO45=%d, GPIO29=%d", initial_gpio45, initial_gpio29);
    if (initial_gpio45 == 0 && initial_gpio29 == 0)
    {
        GPIO_SD_DEBUG_LOGI(TAG, "Both GPIO45 and GPIO29 are LOW (default mount state) - cards ready for independent control");
    }
    else
    {
        GPIO_SD_DEBUG_LOGW(TAG, "GPIO states: GPIO45=%d, GPIO29=%d - verify your control circuit connections", initial_gpio45, initial_gpio29);
    }

    // Create system monitor background task (core 1, priority 5)
    ESP_LOGI(TAG, "Creating system monitor task...");
    xTaskCreatePinnedToCore(
        system_monitor_task,           // Task function
        "system_monitor",              // Task name
        4096,                          // Stack size
        NULL,                          // Parameters
        5,                             // Priority
        NULL,                          // Task handle
        1                              // Core (1 = second core)
    );
}

/* Forward declaration so start_sntp_and_log_time can create the task. */
static void sntp_wait_task(void* pv);