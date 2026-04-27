/**
 * @file system_monitor.c
 * @brief System monitoring and peripheral state management implementation
 * @author Rallybox Development Team
 * @developer Akhil
 *
 * Implements centralized system status collection, WiFi credential management,
 * and peripheral initialization for the Rallybox rally computer.
 *
 * Key Responsibilities:
 * 1. **System Status**: Maintains single system_status_t struct with all metrics
 * 2. **Periodic Update**: Refreshes heap, uptime, and WiFi metrics every 1 second
 * 3. **Credential Storage**: Persists WiFi SSID/password to NVS for auto-connect
 * 4. **State Machine**: Tracks WiFi connection state transitions
 * 5. **Initialization**: Delegates to sd_card module for storage and WiFi drivers
 *
 * Design Patterns:
 * - **Single Status Object**: All metrics in one place → easy to snapshot
 * - **Update Throttling**: Only refreshes if >= 1 second elapsed (avoid polling overhead)
 * - **NVS Integration**: Transparent credential persistence via standard key-value store
 * - **Atomic Operations**: No locks needed; reads are always consistent
 *
 * Implementation Notes:
 * - Uptime calculation uses esp_timer which counts microseconds since boot
 * - Heap metrics retrieved via esp_heap_caps (default RAM pool)
 * - WiFi RSSI only updated if currently connected (avoids false data)
 * - CPU load  is simplified simulation (would use performance counters in production)
 *
 * @note All duration parameters are in seconds; internal calcs use millisecond timer
 * @note Maximum retry is hardcoded to 5 (WIFI_MAX_RETRY_LIMIT); cannot be increased
 * @note WiFi event loop handlers are registered externally (see main.c)
 */

#include "system_monitor.h"

#include "sd_card.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "SystemMonitor";   ///< Log tag for this module

/// Maximum WiFi reconnection attempts before giving up
static const uint32_t WIFI_MAX_RETRY_LIMIT = 5;

/// Global system status structure (source of truth for all metrics)
static system_status_t system_status = { 0 };

/// Mutex to protect concurrent access to system_status
static SemaphoreHandle_t status_mutex = NULL;

/// Timestamp of last update (prevents excessive polling, throttles to 1 Hz)
static uint32_t last_update_time = 0;

static bool ensure_status_mutex(void)
{
    if (status_mutex == NULL)
    {
        status_mutex = xSemaphoreCreateMutex();
        if (status_mutex == NULL)
        {
            ESP_LOGE(TAG, "Failed to create status mutex");
            return false;
        }
    }
    return true;
}

/* ─────────────────────────────────────────────────────────────────────────
 * HELPER FUNCTIONS
 * ───────────────────────────────────────────────────────────────────────── */

 /**
  * @brief Set WiFi IP address string safely
  *
  * Helper to prevent buffer overflows when copying IP strings.
  *
  * @param ip_text IP address string (e.g., "192.168.1.1") or NULL
  */
static void wifi_set_ip_string(const char* ip_text)
{
    snprintf(system_status.wifi_ip, sizeof(system_status.wifi_ip), "%s", ip_text ? ip_text : "0.0.0.0");
}

/**
 * @brief Set WiFi last error message safely
 *
 * Helper to prevent buffer overflows when copying error descriptions.
 *
 * @param error_text Error message or NULL (clears to empty string)
 */
static void wifi_set_last_error(const char* error_text)
{
    snprintf(system_status.wifi_last_error, sizeof(system_status.wifi_last_error), "%s", error_text ? error_text : "");
}

/* ─────────────────────────────────────────────────────────────────────────
 * PUBLIC API
 * ───────────────────────────────────────────────────────────────────────── */

 /**
  * @brief Initialize system monitoring subsystem
  *
  * Setup:
  * 1. Clear all status fields to zero
  * 2. Initialize boot timestamp (microseconds → seconds conversion)
  * 3. Set display initialized flag
  * 4. Initialize WiFi state machine (STARTING)
  * 5. Set placeholder WiFi info (0.0.0.0, "WiFi idle")
  *
  * Call this once from app_main before spawning background tasks.
  *
  * @note Must be called before system_monitor_update()
  */
void system_monitor_init(void)
{
    ESP_LOGI(TAG, "Initializing system monitor...");

    memset(&system_status, 0, sizeof(system_status_t));
    system_status.boot_timestamp = (uint32_t)(esp_timer_get_time() / 1000000);
    system_status.display_initialized = 1;
    system_status.wifi_state = SYSTEM_WIFI_STATE_STARTING;
    system_status.wifi_max_retries = WIFI_MAX_RETRY_LIMIT;
    wifi_set_ip_string("0.0.0.0");
    wifi_set_last_error("WiFi idle");
    snprintf(system_status.racebox_status_text, sizeof(system_status.racebox_status_text), "%s", "Idle");
    snprintf(system_status.gnss_source, sizeof(system_status.gnss_source), "%s", "UART NMEA");
    snprintf(system_status.gnss_status_text, sizeof(system_status.gnss_status_text), "%s", "Idle");

    ESP_LOGI(TAG, "System monitor initialized");
}

/**
 * @brief Periodic system status update function
 *
 * Call this regularly (typically every 250ms in background task with 4x divider = 1 Hz).
 * Updates status metrics in place:
 * 1. Uptime: Calculate elapsed seconds from boot_timestamp
 * 2. Heap: Query free/total heap from default RAM pool
 * 3. WiFi RSSI: Refresh signal strength if connected
 * 4. WiFi IP: Re-query from netif layer (handles DHCP renewal)
 * 5. CPU Load: Simplified placeholder (production would use performance counters)
 *
 * Throttling:
 * - Only updates if >= 1 second elapsed since last call (prevents CPU waste)
 * - Returns early if throttle time not reached
 *
 * @note Thread-safe: Can be called from any context (main, task, ISR)
 * @note Writes to shared status struct; callers read atomically
 */
void system_monitor_update(void)
{
    /* Create mutex on first call (idempotent) */
    if (!ensure_status_mutex())
    {
        return;
    }

    uint32_t current_time = (uint32_t)(esp_timer_get_time() / 1000000);

    // Only update every 1 second to avoid excessive polling
    if ((current_time - last_update_time) < 1)
    {
        return;
    }
    last_update_time = current_time;

    /* Acquire lock before modifying shared status */
    if (xSemaphoreTake(status_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        ESP_LOGW(TAG, "Failed to acquire status mutex for update");
        return;
    }

    // Update uptime
    system_status.uptime_seconds = current_time - system_status.boot_timestamp;

    // Update heap statistics
    system_status.free_heap_bytes = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    system_status.total_heap_bytes = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);

    if (system_status.wifi_connected)
    {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
        {
            system_status.wifi_rssi = ap_info.rssi;
        }
        /* Refresh IP in case it changed (DHCP renewal). */
        esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif)
        {
            esp_netif_ip_info_t ip_info = { 0 };
            if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK)
            {
                esp_ip4addr_ntoa(&ip_info.ip, system_status.wifi_ip, sizeof(system_status.wifi_ip));
            }
        }
    }
    else
    {
        system_status.wifi_rssi = 0;
    }

    // Update CPU load (simplified - would be more complex in production)
    // This is a placeholder that simulates CPU load
    static uint32_t cpu_counter = 0;
    cpu_counter++;
    system_status.cpu_load_percent = (cpu_counter % 50) + 10; // Simulated 10-60%

    ESP_LOGD(TAG, "Uptime: %u sec, Heap: %u/%u bytes",
        system_status.uptime_seconds,
        system_status.free_heap_bytes,
        system_status.total_heap_bytes);

    xSemaphoreGive(status_mutex);
}

/**
 * @brief Get copy of the global system status structure (thread-safe)
 *
 * Returns a consistent snapshot of all status metrics, protected by mutex.
 * Safe to call from any context (main, task, ISR).
 *
 * Typically called from background task after calling system_monitor_update()
 * to retrieve fresh metrics for dashboard display.
 *
 * @return system_status_t Copy of current status (consistent snapshot)
 */
system_status_t system_monitor_get_status(void)
{
    system_status_t status_copy = { 0 };

    /* Create mutex on first call (idempotent) */
    if (!ensure_status_mutex())
    {
        return status_copy;  /* Return zeroed struct */
    }

    /* Acquire lock to read consistent snapshot */
    if (xSemaphoreTake(status_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        memcpy(&status_copy, &system_status, sizeof(system_status_t));
        xSemaphoreGive(status_mutex);
    }
    else
    {
        ESP_LOGW(TAG, "Failed to acquire status mutex for read");
    }

    return status_copy;
}

/**
 * @brief Set WiFi connection status (thread-safe)
 *
 * Called from WiFi event handlers to update connection state.
 *
 * @param connected True if WiFi is connected, false otherwise
 */
void system_monitor_set_wifi_connected(bool connected)
{
    /* Create mutex on first call (idempotent) */
    if (!ensure_status_mutex())
    {
        return;
    }

    if (xSemaphoreTake(status_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        system_status.wifi_connected = connected ? 1 : 0;
        xSemaphoreGive(status_mutex);
    }
    else
    {
        ESP_LOGW(TAG, "Failed to acquire status mutex for wifi_connected write");
    }
}

/**
 * @brief Set WiFi state (thread-safe)
 *
 * Called from WiFi event handlers to update state machine.
 *
 * @param state WiFi state (SYSTEM_WIFI_STATE_xxx)
 */
void system_monitor_set_wifi_state(system_wifi_state_t state)
{
    /* Create mutex on first call (idempotent) */
    if (!ensure_status_mutex())
    {
        return;
    }

    if (xSemaphoreTake(status_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        system_status.wifi_state = state;
        xSemaphoreGive(status_mutex);
    }
    else
    {
        ESP_LOGW(TAG, "Failed to acquire status mutex for wifi_state write");
    }
}

/**
 * @brief Set WiFi IP address (thread-safe)
 *
 * Called from IP event handlers when DHCP assigns an address.
 *
 * @param ip_str IP address string (e.g., "192.168.1.100") or NULL to clear
 */
void system_monitor_set_wifi_ip(const char* ip_str)
{
    /* Create mutex on first call (idempotent) */
    if (!ensure_status_mutex())
    {
        return;
    }

    if (xSemaphoreTake(status_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        if (ip_str)
        {
            snprintf(system_status.wifi_ip, sizeof(system_status.wifi_ip), "%s", ip_str);
        }
        else
        {
            system_status.wifi_ip[0] = '\0';
        }
        xSemaphoreGive(status_mutex);
    }
    else
    {
        ESP_LOGW(TAG, "Failed to acquire status mutex for wifi_ip write");
    }
}

void system_monitor_note_wifi_disconnect(uint8_t reason, bool retrying, uint32_t retry_count)
{
    if (!ensure_status_mutex())
    {
        return;
    }

    if (xSemaphoreTake(status_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        system_status.wifi_connected = 0;
        system_status.wifi_rssi = 0;
        system_status.wifi_retry_count = retry_count;
        system_status.wifi_disconnect_count++;
        system_status.wifi_state = retrying ? SYSTEM_WIFI_STATE_CONNECTING : SYSTEM_WIFI_STATE_DISCONNECTED;
        wifi_set_ip_string("0.0.0.0");

        if (retrying)
        {
            snprintf(system_status.wifi_last_error,
                sizeof(system_status.wifi_last_error),
                "WiFi dropped, retrying (%u)",
                (unsigned)reason);
        }
        else
        {
            snprintf(system_status.wifi_last_error,
                sizeof(system_status.wifi_last_error),
                "WiFi disconnected (%u)",
                (unsigned)reason);
        }

        xSemaphoreGive(status_mutex);
    }
    else
    {
        ESP_LOGW(TAG, "Failed to acquire status mutex for disconnect update");
    }
}

void system_monitor_set_racebox_status(bool initialized, bool connected, int16_t rssi,
    const char* device_name, const char* status_text)
{
    if (!ensure_status_mutex())
    {
        return;
    }

    if (xSemaphoreTake(status_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        system_status.racebox_initialized = initialized ? 1 : 0;
        system_status.racebox_connected = connected ? 1 : 0;
        system_status.racebox_rssi = rssi;
        snprintf(system_status.racebox_device_name, sizeof(system_status.racebox_device_name), "%s",
            device_name ? device_name : "");
        snprintf(system_status.racebox_status_text, sizeof(system_status.racebox_status_text), "%s",
            status_text ? status_text : "");
        xSemaphoreGive(status_mutex);
    }
    else
    {
        ESP_LOGW(TAG, "Failed to acquire status mutex for RaceBox write");
    }
}

void system_monitor_set_gnss_status(bool initialized, bool fix_valid, uint8_t fix_quality,
    uint8_t satellites, double latitude_deg,
    double longitude_deg, float speed_kph,
    float heading_deg, float altitude_m,
    const char* source, const char* last_sentence,
    const char* status_text)
{
    if (!ensure_status_mutex())
    {
        return;
    }

    if (xSemaphoreTake(status_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        system_status.gnss_initialized = initialized ? 1 : 0;
        system_status.gnss_fix_valid = fix_valid ? 1 : 0;
        system_status.gnss_fix_quality = fix_quality;
        system_status.gnss_satellites = satellites;
        system_status.gnss_latitude_deg = latitude_deg;
        system_status.gnss_longitude_deg = longitude_deg;
        system_status.gnss_speed_kph = speed_kph;
        system_status.gnss_heading_deg = heading_deg;
        system_status.gnss_altitude_m = altitude_m;
        snprintf(system_status.gnss_source, sizeof(system_status.gnss_source), "%s",
            source ? source : "UART NMEA");
        snprintf(system_status.gnss_last_sentence, sizeof(system_status.gnss_last_sentence), "%s",
            last_sentence ? last_sentence : "");
        snprintf(system_status.gnss_status_text, sizeof(system_status.gnss_status_text), "%s",
            status_text ? status_text : "");
        xSemaphoreGive(status_mutex);
    }
    else
    {
        ESP_LOGW(TAG, "Failed to acquire status mutex for GNSS write");
    }
}

/**
 * @brief Initialize SD Card 1 and update system status
 *
 * Delegates actual init to sd_card module (sd_card1_init()).
 * Updates counters on success to track activity.
 *
 * Called from: system_monitor_task during boot (after 1-second delay)
 *
 * @return 1 (success) or 0 (card not found, init failed)
 */
uint8_t system_sdcard1_init(void)
{
    esp_err_t ret = sd_card1_init();
    if (ret != ESP_OK)
    {
        system_status.sdcard1_initialized = 0;
        return 0;
    }
    system_status.sdcard1_initialized = 1;
    system_status.sdcard1_read_count = 0;
    system_status.sdcard1_write_count = 0;
    system_status.sdcard1_error_count = 0;
    system_status.sdcard1_total_read_bytes = 0;
    system_status.sdcard1_total_write_bytes = 0;
    return 1;
}

/**
 * @brief Initialize SD Card 2 and update system status
 *
 * Delegates actual init to sd_card module (sd_card2_init()).
 * Updates counters on success to track activity.
 *
 * Called from: system_monitor_task during boot (after 1.5-second delay)
 *
 * @return 1 (success) or 0 (card not found, init failed)
 */
uint8_t system_sdcard2_init(void)
{
    esp_err_t ret = sd_card2_init();
    if (ret != ESP_OK)
    {
        system_status.sdcard2_initialized = 0;
        return 0;
    }
    system_status.sdcard2_initialized = 1;
    system_status.sdcard2_read_count = 0;
    system_status.sdcard2_write_count = 0;
    system_status.sdcard2_error_count = 0;
    system_status.sdcard2_total_read_bytes = 0;
    system_status.sdcard2_total_write_bytes = 0;
    return 1;
}

/**
 * @brief Initialize WiFi subsystem
 *
 * Simple initialization:
 * - Set wifi_initialized flag
 * - Initialize state to DISCONNECTED
 * - Clear error message
 *
 * Note: Full WiFi stack init happens in app_main (wifi_init_sta)
 *
 * @return 1 (always succeeds)
 */
uint8_t system_wifi_init(void)
{
    system_status.wifi_initialized = 1;
    system_status.wifi_state = SYSTEM_WIFI_STATE_DISCONNECTED;
    wifi_set_last_error("Initialized");
    return 1;
}

extern int s_retry_num;

/**
 * @brief Attempt WiFi connection with provided credentials
 *
 * Process:
 * 1. Validate SSID (non-empty)
 * 2. Store credentials to NVS for persistence (auto-connect future boots)
 * 3. Configure WiFi auth mode based on password presence
 * 4. Reset retry counter
 * 5. Set state to CONNECTING
 * 6. Disconnect from previous AP (if any)
 * 7. Apply new config and trigger connection
 *
 * Credentials are persisted in NVS namespace "wifi_cfg":
 * - Key "ssid": Network name (string)
 * - Key "password": Network password (string, empty if open)
 *
 * @param ssid Network name (required, non-empty)
 * @param password Network password (optional, NULL for open networks)
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if SSID is empty
 */
esp_err_t system_wifi_connect_credentials(const char* ssid, const char* password)
{
    system_status_t status_snapshot;
    esp_err_t err;

    if (!ssid || ssid[0] == '\0') return ESP_ERR_INVALID_ARG;

    // Save to NVS
    nvs_handle_t handle;
    if (nvs_open("wifi_cfg", NVS_READWRITE, &handle) == ESP_OK)
    {
        nvs_set_str(handle, "ssid", ssid);
        nvs_set_str(handle, "password", password ? password : "");
        nvs_commit(handle);
        nvs_close(handle);
    }

    wifi_config_t cfg = { 0 };
    snprintf((char*)cfg.sta.ssid, sizeof(cfg.sta.ssid), "%s", ssid);
    if (password && password[0])
    {
        snprintf((char*)cfg.sta.password, sizeof(cfg.sta.password), "%s", password);
        cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }
    else
    {
        cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }
    cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    cfg.sta.sae_h2e_identifier[0] = '\0';

    if (!ensure_status_mutex())
    {
        return ESP_FAIL;
    }

    if (xSemaphoreTake(status_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        snprintf(system_status.wifi_ssid, sizeof(system_status.wifi_ssid), "%s", ssid);
        wifi_set_ip_string("0.0.0.0");
        wifi_set_last_error("Connecting...");
        system_status.wifi_connected = 0;
        system_status.wifi_retry_count = 0;
        system_status.wifi_state = SYSTEM_WIFI_STATE_CONNECTING;
        system_status.wifi_connect_attempts++;
        xSemaphoreGive(status_mutex);
    }

    s_retry_num = 0;
    status_snapshot = system_monitor_get_status();

    err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err != ESP_OK)
    {
        if (xSemaphoreTake(status_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            system_status.wifi_state = SYSTEM_WIFI_STATE_DISCONNECTED;
            wifi_set_last_error("Failed to apply WiFi config");
            xSemaphoreGive(status_mutex);
        }
        return err;
    }

    if (status_snapshot.wifi_state == SYSTEM_WIFI_STATE_CONNECTED ||
        status_snapshot.wifi_state == SYSTEM_WIFI_STATE_CONNECTING)
    {
        err = esp_wifi_disconnect();
        if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_CONNECT)
        {
            if (xSemaphoreTake(status_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
            {
                system_status.wifi_state = SYSTEM_WIFI_STATE_DISCONNECTED;
                wifi_set_last_error("Failed to restart WiFi connection");
                xSemaphoreGive(status_mutex);
            }
            return err;
        }
    }

    err = esp_wifi_connect();
    if (err != ESP_OK)
    {
        if (xSemaphoreTake(status_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            system_status.wifi_state = SYSTEM_WIFI_STATE_DISCONNECTED;
            wifi_set_last_error("Failed to start WiFi connection");
            xSemaphoreGive(status_mutex);
        }
        return err;
    }

    return ESP_OK;
}

/**
 * @brief Connect WiFi using saved credentials from NVS
 *
 * Looks up WiFi credentials from NVS namespace "wifi_cfg" and attempts
 * connection using system_wifi_connect_credentials(). Used for auto-connect
 * on boot or after manual disconnect.
 *
 * NVS Keys:
 * - "ssid": Network name
 * - "password": Network password
 *
 * @return ESP_OK on successful lookup+connect, ESP_ERR_NOT_FOUND if no saved creds
 */
esp_err_t system_wifi_connect_saved(void)
{
    nvs_handle_t handle;
    char ssid[33] = { 0 };
    char pass[65] = { 0 };
    size_t len_s = sizeof(ssid), len_p = sizeof(pass);

    if (nvs_open("wifi_cfg", NVS_READONLY, &handle) == ESP_OK)
    {
        if (nvs_get_str(handle, "ssid", ssid, &len_s) == ESP_OK)
        {
            nvs_get_str(handle, "password", pass, &len_p);
            nvs_close(handle);
            return system_wifi_connect_credentials(ssid, pass);
        }
        nvs_close(handle);
    }
    return ESP_ERR_NOT_FOUND;
}

/**
 * @brief Disconnect WiFi and optionally erase saved credentials
 *
 * Process:
 * 1. Optional: Erase stored credentials from NVS (if clear_credentials=true)
 * 2. Call esp_wifi_disconnect() to stop active connection
 * 3. Update status (connected=0, state=DISCONNECTED, error="Disconnected by user")
 *
 * Use case: User manually wants to disconnect or switch networks
 *
 * @param clear_credentials If true, erases SSID/password from NVS
 * @return ESP_OK on success
 */
esp_err_t system_wifi_disconnect(bool clear_credentials)
{
    if (clear_credentials)
    {
        nvs_handle_t handle;
        if (nvs_open("wifi_cfg", NVS_READWRITE, &handle) == ESP_OK)
        {
            nvs_erase_key(handle, "ssid");
            nvs_erase_key(handle, "password");
            nvs_commit(handle);
            nvs_close(handle);
        }
    }
    esp_wifi_disconnect();
    system_status.wifi_connected = 0;
    system_status.wifi_state = SYSTEM_WIFI_STATE_DISCONNECTED;
    wifi_set_last_error("Disconnected by user");
    return ESP_OK;
}

/**
 * @brief Simulate SD card read/write test
 *
 * Increments operation counters for the specified slot to simulate
 * activity. Used for testing concurrent access and stress scenarios.
 *
 * @param card_number 1 (SD1) or 2 (SD2)
 */
void system_sdcard_test(uint8_t card_number)
{
    ESP_LOGI(TAG, "Testing SD Card %d read/write...", card_number);

    if (card_number == 1)
    {
        // Simulate read/write test
        system_status.sdcard1_read_count++;
        system_status.sdcard1_write_count++;
        system_status.sdcard1_total_read_bytes += 4096;
        system_status.sdcard1_total_write_bytes += 4096;
    }
    else if (card_number == 2)
    {
        system_status.sdcard2_read_count++;
        system_status.sdcard2_write_count++;
        system_status.sdcard2_total_read_bytes += 4096;
        system_status.sdcard2_total_write_bytes += 4096;
    }
}

/**
 * @brief Format uptime as human-readable HH:MM:SS string
 *
 * Converts raw uptime_seconds to formatted time display.
 * Example: 3661 seconds → "01:01:01" (1 hour, 1 minute, 1 second)
 *
 * @param buffer Output buffer (must hold at least 12 chars: "HH:MM:SS\0")
 * @param len Buffer size
 */
void system_monitor_get_uptime_string(char* buffer, size_t len)
{
    uint32_t seconds = system_status.uptime_seconds;
    uint32_t hours = seconds / 3600;
    uint32_t minutes = (seconds % 3600) / 60;
    uint32_t secs = seconds % 60;

    snprintf(buffer, len, "%02u:%02u:%02u", hours, minutes, secs);
}

/**
 * @brief Format SD card status as string for display
 *
 * Example output: "SD1: OK | R:42 W:15"  (OK/FAIL, read count, write count)
 *
 * @param card_num 1 or 2
 * @param buffer Output buffer
 * @param len Buffer size
 */
void system_monitor_get_sdcard_status(uint8_t card_num, char* buffer, size_t len)
{
    if (card_num == 1)
    {
        snprintf(buffer, len, "SD1: %s | R:%u W:%u",
            system_status.sdcard1_initialized ? "OK" : "FAIL",
            system_status.sdcard1_read_count,
            system_status.sdcard1_write_count);
    }
    else if (card_num == 2)
    {
        snprintf(buffer, len, "SD2: %s | R:%u W:%u",
            system_status.sdcard2_initialized ? "OK" : "FAIL",
            system_status.sdcard2_read_count,
            system_status.sdcard2_write_count);
    }
}

/**
 * @brief Format WiFi status as string for display
 *
 * Example output: "WiFi: CONNECTED | RSSI: -45 dBm"
 * or "WiFi: DISCONNECTED | RSSI: 0 dBm"
 *
 * @param buffer Output buffer
 * @param len Buffer size
 */
void system_monitor_get_wifi_status(char* buffer, size_t len)
{
    snprintf(buffer, len, "WiFi: %s | RSSI: %d dBm",
        system_status.wifi_connected ? "CONNECTED" : "DISCONNECTED",
        system_status.wifi_rssi);
}

/**
 * @brief Format system load as string for display
 *
 * Example output: "CPU: 25% | Heap: 256/512 KB"
 * (CPU percentage, free heap KB, total heap KB)
 *
 * @param buffer Output buffer
 * @param len Buffer size
 */
void system_monitor_get_load_status(char* buffer, size_t len)
{
    uint32_t free_kb = system_status.free_heap_bytes / 1024;
    uint32_t total_kb = system_status.total_heap_bytes / 1024;

    snprintf(buffer, len, "CPU: %u%% | Heap: %u/%u KB",
        system_status.cpu_load_percent,
        free_kb,
        total_kb);
}
