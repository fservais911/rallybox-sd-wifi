#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H

/**
 * @file system_monitor.h
 * @brief System monitoring and peripheral status tracking
 * @author Rallybox Development Team
 * @developer Akhil
 *
 * Provides centralized system status collection and WiFi credential management
 * for the Rallybox rally computer system. All system state is exposed via a
 * single `system_status_t` structure that is updated periodically.
 *
 * Key Responsibilities:
 * 1. **Uptime Tracking**: Boot timestamp reference for elapsed time calculation
 * 2. **Memory Statistics**: Free/total heap metrics for resource monitoring
 * 3. **SD Card State**: Per-slot initialization, R/W counters, total bytes transferred
 * 4. **WiFi Management**: Connection state, RSSI, IP address, saved credentials in NVS
 * 5. **System Metrics**: CPU load percentage, error counters
 *
 * Architecture:
 * - **Single Status Struct**: All metrics in one place (thread-safe atomic reads)
 * - **NVS Credential Store**: Persistent WiFi SSID/password in flash
 * - **Periodic Updates**: Call system_monitor_update() every 1 Hz for fresh data
 * - **Event-Driven WiFi**: WiFi events drive connection state machine, not polling
 *
 * Usage Pattern:
 * @code
 * // Initialization (call once in app_main)
 * system_monitor_init();
 *
 * // Periodic polling (e.g., in background task, every 250ms with divider)
 * system_monitor_update();
 * const system_status_t *status = system_monitor_get_status();
 * printf("Uptime: %u sec, Heap: %u KB\n",
 *        status->uptime_seconds,
 *        status->free_heap_bytes / 1024);
 *
 * // WiFi connectivity (called from UI logic or auto at startup)
 * system_wifi_connect_credentials("MySSID", "MyPassword");  // Try to connect
 * // ... later, when event_handler gets WIFI_EVENT_STA_GOT_IP ...
 * // user gets IP callback
 * @endcode
 *
 * @note Struct members are safe to read from any context (no synchronization needed)
 * @note WiFi operations are async; check wifi_state in event handler callbacks
 * @note All errors return esp_err_t for consistency with ESP-IDF conventions
 */

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"
#include "sd_card.h"   /* SD card module — pin defs and init API */

 /// WiFi connection state machine enumerations
typedef enum
{
    SYSTEM_WIFI_STATE_STARTING = 0,        ///< WiFi stack initializing
    SYSTEM_WIFI_STATE_CONNECTING,          ///< Active connection attempt in progress
    SYSTEM_WIFI_STATE_CONNECTED,           ///< Successfully connected, IP acquired
    SYSTEM_WIFI_STATE_DISCONNECTED,        ///< Not connected (idle or failed)
} system_wifi_state_t;

/**
 * @brief Comprehensive system status structure
 *
 * This structure is the single source of truth for all system metrics.
 * All fields are atomic reads (safe without mutex from any task/ISR).
 * Updated by system_monitor_update() call (typically 1 Hz).
 */
typedef struct
{
    // ─── Initialization Flags ───────────────────────────────────────────
    uint8_t sdcard1_initialized;            ///< SD Card 1 mount succeeded
    uint8_t sdcard2_initialized;            ///< SD Card 2 mount succeeded
    uint8_t wifi_initialized;               ///< WiFi stack initialized
    uint8_t racebox_initialized;            ///< RaceBox BLE subsystem initialized
    uint8_t gnss_initialized;               ///< GNSS UART subsystem initialized
    uint8_t display_initialized;            ///< Display/LVGL initialized

    // ─── Timing ─────────────────────────────────────────────────────────
    uint32_t uptime_seconds;                ///< Seconds elapsed since boot
    uint32_t boot_timestamp;                ///< Unix timestamp (sec) at boot

    // ─── SD Card 1 Metrics (SDMMC Internal) ──────────────────────────────
    uint32_t sdcard1_read_count;            ///< Total read operations
    uint32_t sdcard1_write_count;           ///< Total write operations
    uint32_t sdcard1_error_count;           ///< Failed operations (I/O errors)
    uint64_t sdcard1_total_read_bytes;      ///< Cumulative bytes read
    uint64_t sdcard1_total_write_bytes;     ///< Cumulative bytes written

    // ─── SD Card 2 Metrics (SDSPI External) ──────────────────────────────
    uint32_t sdcard2_read_count;            ///< Total read operations
    uint32_t sdcard2_write_count;           ///< Total write operations
    uint32_t sdcard2_error_count;           ///< Failed operations (I/O errors)
    uint64_t sdcard2_total_read_bytes;      ///< Cumulative bytes read
    uint64_t sdcard2_total_write_bytes;     ///< Cumulative bytes written

    // ─── WiFi Connectivity ───────────────────────────────────────────────
    int16_t wifi_rssi;                      ///< Signal strength (-30 to -120 dBm)
    uint8_t wifi_connected;                 ///< 1=connected, 0=disconnected
    uint8_t wifi_state;                     ///< system_wifi_state_t value
    uint32_t wifi_connect_attempts;         ///< Total connection attempts
    uint32_t wifi_retry_count;              ///< Current retry attempt
    uint32_t wifi_max_retries;              ///< Maximum retries allowed
    uint32_t wifi_disconnect_count;         ///< Disconnections counted
    char wifi_ip[16];                       ///< Current IP ("0.0.0.0" if not connected)
    char wifi_ssid[33];                     ///< Target/current SSID string
    char wifi_last_error[64];               ///< Last error message (diagnostic)

    // ─── RaceBox BLE Connectivity ───────────────────────────────────────
    uint8_t racebox_connected;              ///< 1=connected to RaceBox, 0=not connected
    int16_t racebox_rssi;                   ///< Last observed RaceBox RSSI
    char racebox_device_name[32];           ///< Connected/discovered RaceBox device name
    char racebox_status_text[64];           ///< Current RaceBox state text

    // ─── GNSS Metrics ────────────────────────────────────────────────────
    uint8_t gnss_fix_valid;                 ///< 1=valid fix, 0=no valid fix
    uint8_t gnss_fix_quality;               ///< NMEA GGA quality indicator
    uint8_t gnss_satellites;                ///< Satellites used in solution
    double gnss_latitude_deg;               ///< Decimal latitude
    double gnss_longitude_deg;              ///< Decimal longitude
    float gnss_speed_kph;                   ///< Ground speed in km/h
    float gnss_heading_deg;                 ///< Course over ground in degrees
    float gnss_altitude_m;                  ///< Altitude above mean sea level in meters
    char gnss_source[16];                   ///< Source label, e.g. "UART NMEA"
    char gnss_last_sentence[8];             ///< Last parsed NMEA sentence type
    char gnss_status_text[64];              ///< Current GNSS state text

    // ─── System Metrics ──────────────────────────────────────────────────
    uint32_t free_heap_bytes;               ///< Current free heap (updated 1 Hz)
    uint32_t total_heap_bytes;              ///< Total allocatable heap
    uint8_t cpu_load_percent;               ///< Estimated CPU utilization (0-100)

    // ─── Testing Metrics ────────────────────────────────────────────────
    uint8_t test_load_percent;              ///< Load generated by tests (0-100)
    uint32_t test_operations_count;         ///< Operations executed in tests
    uint32_t test_errors_count;             ///< Errors occurred in tests
} system_status_t;

/**
 * @brief Initialize system monitoring
 */
void system_monitor_init(void);

/**
 * @brief Update system status collection
 */
void system_monitor_update(void);

/**
 * @brief Get current system status (thread-safe copy)
 *
 * Returns a copy of the system_status struct protected by mutex.
 * Callers receive a consistent snapshot of all metrics.
 *
 * @return system_status_t Copy of current status (not a pointer)
 */
system_status_t system_monitor_get_status(void);

/**
 * @brief Set WiFi connection status (thread-safe)
 *
 * @param connected True if WiFi is connected, false otherwise
 */
void system_monitor_set_wifi_connected(bool connected);

/**
 * @brief Set WiFi state (thread-safe)
 *
 * @param state WiFi state (SYSTEM_WIFI_STATE_xxx)
 */
void system_monitor_set_wifi_state(system_wifi_state_t state);

/**
 * @brief Set WiFi IP address (thread-safe)
 *
 * @param ip_str IP address string (e.g., "192.168.1.100")
 */
void system_monitor_set_wifi_ip(const char* ip_str);

/**
 * @brief Update RaceBox BLE connection status (thread-safe)
 */
void system_monitor_set_racebox_status(bool initialized, bool connected, int16_t rssi,
    const char* device_name, const char* status_text);

/**
 * @brief Update GNSS status snapshot (thread-safe)
 */
void system_monitor_set_gnss_status(bool initialized, bool fix_valid, uint8_t fix_quality,
    uint8_t satellites, double latitude_deg,
    double longitude_deg, float speed_kph,
    float heading_deg, float altitude_m,
    const char* source, const char* last_sentence,
    const char* status_text);

/**
 * @brief Initialize SD card (SDMMC - internal)
 */
uint8_t system_sdcard1_init(void);

/**
 * @brief Initialize SD card (SPI - external)
 */
uint8_t system_sdcard2_init(void);

/**
 * @brief Initialize Wi-Fi
 */
uint8_t system_wifi_init(void);

/**
 * @brief Connect Wi-Fi using the provided credentials and persist them to NVS.
 */
esp_err_t system_wifi_connect_credentials(const char* ssid, const char* password);

/**
 * @brief Connect Wi-Fi using credentials stored in NVS.
 */
esp_err_t system_wifi_connect_saved(void);

/**
 * @brief Disconnect Wi-Fi. Optionally clear saved credentials.
 */
esp_err_t system_wifi_disconnect(bool clear_credentials);

/**
 * @brief Test SD card read/write
 */
void system_sdcard_test(uint8_t card_number);

/**
 * @brief Get uptime string
 */
void system_monitor_get_uptime_string(char* buffer, size_t len);

/**
 * @brief Get SD card status string
 */
void system_monitor_get_sdcard_status(uint8_t card_num, char* buffer, size_t len);

/**
 * @brief Get Wi-Fi status string
 */
void system_monitor_get_wifi_status(char* buffer, size_t len);

/**
 * @brief Get system load string
 */
void system_monitor_get_load_status(char* buffer, size_t len);

#endif // SYSTEM_MONITOR_H
