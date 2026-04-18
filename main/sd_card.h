#pragma once

/**
 * @file sd_card.h
 * @brief Dual SD card controller for ESP32-P4 (SDMMC + SDSPI)
 * @author Rallybox Development Team
 * @developer Akhil
 *
 * Manages two independent SD card interfaces for the Rallybox system:
 * 1. **SD Card 1 (Internal)**: SDMMC 4-wire slot (GPIO 39-44)
 *    - Direct connection to internal slot
 *    - High-speed 40 MHz operation
 *    - Powered via on-chip LDO channel 4
 * 2. **SD Card 2 (External)**: SDSPI via SPI3 bus (GPIO 13,14,21,23)
 *    - External expansion card
 *    - Medium-speed 20 MHz operation
 *    - Useful for logging while SD1 handles application data
 *
 * Key Features:
 * - Automatic FAT filesystem detection & mount
 * - Concurrent operation of both slots (bus arbitration handled by driver)
 * - Format/quick-format capability
 * - Capacity & usage statistics
 * - Proper error handling with esp_err_t returns
 * - Data logging support via CSV writer
 *
 * Typical Usage:
 * @code
 * // Initialize both cards
 * sd_card1_init();  // Mount at /sdcard
 * sd_card2_init();  // Mount at /sdcard2
 *
 * // Get capacity info
 * uint64_t cap_mb, used_mb;
 * sd_card_get_info(1, &cap_mb, &used_mb);
 * printf("SD1: %llu MB used of %llu MB\n", used_mb, cap_mb);
 *
 * // Write/read files via standard C
 * FILE *f = fopen("/sdcard/data.bin", "wb");
 * fwrite(buffer, 1, size, f);
 * fclose(f);
 * @endcode
 *
 * @note Thread-safe: Each slot has independent state; concurrent access is OK
 * @note Both slots use FatFS VFS layer (standard file I/O compatible)
 * @note SDMMC host is shared with ESP-Hosted WiFi (Slot 1) - init order matters
 */

#include "esp_err.h"
#include "sdmmc_cmd.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

      /* ─── SD Card 1 — SDMMC 4-wire internal slot ─────────────────────────────────
       *
       * Waveshare ESP32-P4-WIFI6-Touch-LCD-X pin assignment (from official docs):
       *   CLK  → GPIO 43
       *   CMD  → GPIO 44
       *   D0   → GPIO 39
       *   D1   → GPIO 40
       *   D2   → GPIO 41
       *   D3   → GPIO 42
       *
       * Bus width  : 4-wire
       * Speed      : High-speed 40 MHz (SDMMC_FREQ_HIGHSPEED)
       * Power ctrl : On-chip LDO channel 4
       * Mount point: /sdcard
       * ─────────────────────────────────────────────────────────────────────────── */
#define SD1_MOUNT_POINT         "/sdcard"
#define SD1_SDMMC_SLOT          SDMMC_HOST_SLOT_0
#define SD1_PIN_CLK             43
#define SD1_PIN_CMD             44
#define SD1_PIN_D0              39
#define SD1_PIN_D1              40
#define SD1_PIN_D2              41
#define SD1_PIN_D3              42
#define SD1_BUS_WIDTH           4
#define SD1_LDO_CHAN            4       /* on-chip LDO that powers the SD slot */
#define SD1_MAX_FREQ_KHZ        SDMMC_FREQ_HIGHSPEED  

       /* ─── SD Card Control Pins (Independent) ──────────────────────────────────────
        * Two separate GPIO pins for independent mount/unmount control:
        *
        * GPIO45 (SD Card 1):
        *   - LOW (0V): Mount SD Card 1
        *   - HIGH (3.3V): Unmount SD Card 1
        *
        * GPIO29 (SD Card 2):
        *   - LOW (0V): Mount SD Card 2
        *   - HIGH (3.3V): Unmount SD Card 2
        * ─────────────────────────────────────────────────────────────────────────── */
#define SD1_CONTROL_PIN         CONFIG_RALLYBOX_SD1_CONTROL_GPIO  /* SD Card 1 control GPIO */
#define SD2_CONTROL_PIN         CONFIG_RALLYBOX_SD2_CONTROL_GPIO  /* SD Card 2 control GPIO */

        /* ─── SD Card 2 — SDSPI external slot (SPI3 / FSPPI) ────────────────────────
         *
         *   CLK  → GPIO 48
         *   MOSI → GPIO 47
         *   MISO → GPIO 34
         *   CS   → GPIO 32
        //  *
        //  * Note: GPIO34-39 are input-only on ESP32 family (GPIO36 included),
        //  * which means they cannot be used for CLK/MOSI/CS outputs. MISO may
        //  * be an input-only pin and is safe to use as MISO only.
        //  *
        //  * Bus mode   : SDSPI (SPI3 host)
        //  *
        //  * Speed      : 20 MHz (SDMMC_FREQ_DEFAULT)
        //  * Mount point: /sdcard2
        //  * ─────────────────────────────────────────────────────────────────────────── */
#define SD2_MOUNT_POINT         "/sdcard2"
#define SD2_SPI_HOST            SPI3_HOST
#define SD2_PIN_CLK             48
#define SD2_PIN_MOSI            47
#define SD2_PIN_MISO            34
#define SD2_PIN_CS              32

#define SD2_MAX_FREQ_KHZ        SDMMC_FREQ_DEFAULT      /* 20 MHz */

        /**
         * @brief Initialise SD Card 1 (SDMMC 4-wire, internal slot 0).
         *
         * Powers the slot via on-chip LDO, configures bus with explicit GPIO
         * assignments, enables internal pull-ups, and mounts FAT/VFS at
         * SD1_MOUNT_POINT.
         *
         * @return ESP_OK on success, or an esp_err_t error code.
         */
      esp_err_t sd_card1_init(void);

      /**
       * @brief Unmount and power-off SD Card 1.
       * @return ESP_OK on success.
       */
      esp_err_t sd_card1_deinit(void);

      /**
       * @return true if SD Card 1 is currently mounted.
       */
      bool sd_card1_is_mounted(void);

      /**
       * @return VFS mount point string for SD Card 1 ("/sdcard").
       */
      const char* sd_card1_mount_point(void);

      /**
       * @brief Initialise SD Card 2 (SDSPI, SPI3 host).
       *
       * Initialises the SPI3 bus (once), adds the SD device, and mounts FAT/VFS
       * at SD2_MOUNT_POINT.
       *
       * @return ESP_OK on success, or an esp_err_t error code.
       */
      esp_err_t sd_card2_init(void);

      /**
       * @brief Unmount and release SPI bus for SD Card 2.
       * @return ESP_OK on success.
       */
      esp_err_t sd_card2_deinit(void);

      /**
       * @return true if SD Card 2 is currently mounted.
       */
      bool sd_card2_is_mounted(void);

      /**
       * @return VFS mount point string for SD Card 2 ("/sdcard2").
       */
      const char* sd_card2_mount_point(void);

      /**
       * @brief Get total and used space of SD Card in MB
       */
      esp_err_t sd_card_get_info(int slot, uint64_t* out_capacity_mb, uint64_t* out_used_mb);

      /**
       * @brief Format an SD card with FAT filesystem.
       * @param slot 1 or 2
       * @return ESP_OK on success, or an esp_err_t error code.
       */
      esp_err_t sd_card_format(int slot);

      /**
       * @brief Quick-format an SD card with a performance-oriented FAT configuration.
       * @param slot 1 or 2
       * @return ESP_OK on success, or an esp_err_t error code.
       */
      esp_err_t sd_card_quick_format(int slot);

      /**
       * @brief Export all system logs from SD2 to SD1.
       * Copies all files matching "rallybox_log_*.txt" from SD2 to SD1.
       * @return ESP_OK on success.
       */
      esp_err_t sd_card_export_logs(void);

      /**
       * @brief Start a periodic datalogger that writes CSV rows to the SD card.
       *
       * The logger will create a file named `rallybox_sdcard<slot>_YYYYMMDD_HHMMSS.txt`
       * on the specified slot's mount point and append `rows` lines at
       * `interval_seconds` intervals. If `rows` is 0 the logger runs indefinitely.
       *
       * Each row is a human-readable text line containing: sl_no, timestamp (IST), CPU %, free heap bytes,
       * Wi-Fi connected state, Wi-Fi SSID, IP, SD slot, SD capacity (MB), SD used (MB).
       */
      esp_err_t sd_card_start_datalogger(int slot, int interval_seconds, int rows);

#ifdef __cplusplus
}
#endif
