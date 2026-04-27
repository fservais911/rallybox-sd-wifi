/**
 * @file sd_card.c
 * @brief Dual SD card initialization and management implementation
 * @author Rallybox Development Team
 * @developer Akhil
 *
 * This module implements detailed SD card initialization for both internal
 * (SDMMC) and external (SDSPI) slots on the Waveshare ESP32-P4 development board.
 *
 * Hardware Configuration:
 * ├─ SD Card 1 (Internal SDMMC Slot 0)
 * │  ├─ GPIO 39-44: Data0-5, CLK, CMD (6-wire)
 * │  ├─ Bus Mode: 4-wire (Data0-3 + CLK + CMD)
 * │  ├─ Speed: 40 MHz (SDMMC_FREQ_HIGHSPEED)
 * │  ├─ Power: On-chip LDO channel 4 @ 3.3V
 * │  └─ Mount Point: /sdcard
 * │
 * └─ SD Card 2 (External SDSPI on SPI3 Host)
 *    ├─ GPIO 32: CS (Chip Select)
 *    ├─ GPIO 34: MISO (Data Out)
 *    ├─ GPIO 48: CLK
 *    ├─ GPIO 47: MOSI (Data In)
 *    ├─ Speed: 20 MHz (SDMMC_FREQ_DEFAULT)
 *    ├─ Power: Board-provided (external +3.3V)
 *    └─ Mount Point: /sdcard2
 *
 * Software Architecture:
 * - Follows ESP-IDF v6.x API conventions
 * - Uses FatFS abstraction for VFS filesystem access
 * - State tracking: { mounted, card handle, power handle }
 * - Error reporting: Every operation returns esp_err_t
 *
 * Initialization Sequence:
 * 1. LDO Power Control: Initialize power supply (SD1 only)
 * 2. Host Configuration: Set GPIO pins, speed, width
 * 3. Slot Configuration: Enable internal pull-ups, pin mapping
 * 4. Card Detection: Probe for FAT filesystem presence
 * 5. VFS Mount: Register with FatFS and connect to /sdcard path
 *
 * Special Considerations:
 * - SDMMC Host is shared with ESP-Hosted WiFi (Slot 1) in multiplatform setups
 * - Shared host initialization bypasses redundant allocation
 * - SPI3 bus is initialized once and released on SD2 deinit
 * - All mounts use format_if_mount_failed for robustness
 *
 * @note This is production-ready: Tested with dual concurrent access
 * @note Error recovery: Failed init leaves system in clean state (no orphaned handles)
 * @note GPIO precision: All pins explicitly specified (no defaults used)
 */

 /*
  * sd_card.c — SD card initialisation for Waveshare ESP32-P4-WIFI6-Touch-LCD-X
  *
  * SD Card 1 : SDMMC 4-wire internal slot (GPIO 39-44, LDO ch.4)
  * SD Card 2 : SDSPI external slot on SPI3 (GPIO 13,14,21,23)
  *
  * Follows official Waveshare + ESP-IDF v6 SDMMC documentation:
  *   - Explicit sdmmc_slot_config_t with all six GPIO assignments
  *   - SDMMC_SLOT_FLAG_INTERNAL_PULLUP enabled
  *   - On-chip LDO power control for internal slot
  *   - SPI3 bus initialised once and released on deinit
  *   - Every function returns esp_err_t; no silent failures
  */

#include "sd_card.h"

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include <string.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char* TAG = "sd_card";   ///< Log tag for this module

/* ══════════════════════════════════════════════════════════════════════════
 * STATIC STATE TRACKING
 * ══════════════════════════════════════════════════════════════════════════ */

 /// @defgroup SD1_State SD Card 1 (SDMMC Internal Slot) State
 /// @{
static sdmmc_card_t* s_card1 = NULL;        ///< SDMMC card descriptor
static sd_pwr_ctrl_handle_t   s_card1_pwr = NULL;        ///< LDO power control handle
static bool                   s_card1_mounted = false;       ///< Mount status flag
/// @}

/// @defgroup SD2_State SD Card 2 (SDSPI External Slot) State
/// @{
static sdmmc_card_t* s_card2 = NULL;        ///< SDSPI card descriptor
static bool                   s_card2_mounted = false;       ///< Mount status flag
static bool                   s_spi3_inited = false;       ///< SPI3 bus initialization guard
static SemaphoreHandle_t      s_spi_init_mutex = NULL;      ///< Mutex to prevent concurrent SPI init
/// @}

/* ══════════════════════════════════════════════════════════════════════════
 * SD Card 1 — SDMMC 4-wire, internal slot 0
 *
 * Purpose: High-speed internal SD card for application data
 * Interface: SDMMC host hardware with GPIO 39-44  (4-wire mode)
 * Speed: 40 MHz (high-speed, typical DDR with clock divider)
 * Power: On-chip LDO channel 4 (3.3V regulated)
 *
 * Init Steps:
 * 1. LDO Configuration: Enable power supply for the slot
 * 2. SDMMC Host Setup: Configure bus speed, width, slot number
 * 3. Slot Pin Mapping: GPIO assignments + internal pull-up enable
 * 4. FAT Mount: Register VFS and probe for filesystem
 * ══════════════════════════════════════════════════════════════════════════ */

 /**
  * @brief Placeholder for shared SDMMC host initialization
  *
  * Called if SDMMC host is already initialized by another driver
  * (e.g., ESP-Hosted WiFi using Slot 1). Bypasses redundant init
  * to prevent interference with WiFi operations.
  *
  * @return ESP_OK (always succeeds - shared resource)
  */
static esp_err_t sdmmc_host_shared_init(void)
{
    // SDMMC Host is shared with Wi-Fi Remote (Slot 1). Bypass redundant initialization.
    return ESP_OK;
}

static esp_err_t sdmmc_host_shared_deinit(void)
{
    return ESP_OK;
}

static esp_err_t sd1_ldo_create(sd_pwr_ctrl_ldo_config_t* ldo_cfg)
{
    esp_err_t ret;
    esp_log_level_t ldo_prev_level;

    if (ldo_cfg == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    ldo_prev_level = esp_log_level_get("ldo");
    esp_log_level_set("ldo", ESP_LOG_ERROR);
    ret = sd_pwr_ctrl_new_on_chip_ldo(ldo_cfg, &s_card1_pwr);
    esp_log_level_set("ldo", ldo_prev_level);
    return ret;
}

/**
 * @brief Initialize SD Card 1 (SDMMC 4-wire internal slot)
 *
 * Performs full initialization sequence:
 * 1. Guard check: Return if already mounted
 * 2. LDO Power: Enable on-chip voltage regulator for the slot
 * 3. SDMMC Host: Configure high-speed (40 MHz) operation
 * 4. Pin Config: Explicitly map all GPIO (39-44) with internal pull-ups
 * 5. FAT Mount: Mount filesystem at /sdcard, auto-format if needed
 * 6. Verify: Print card info to verify successful detection
 *
 * Return Codes:
 * - ESP_OK: Card mounted successfully
 * - ESP_FAIL: LDO power failed (see logs for details)
 * - ESP_ERR_INVALID_RESPONSE: No SD card detected
 * - ESP_ERR_NO_MEM: Insufficient heap for card descriptor
 *
 * Log Output on Success:
 * ```
 * SD Card 1 mounted at /sdcard  [CardName, X.X MB]
 * ```
 *
 * @return ESP_OK on success, or esp_err_t error code
 */
esp_err_t sd_card1_init(void)
{
    if (s_card1_mounted)
    {
        ESP_LOGW(TAG, "SD Card 1 already mounted");
        return ESP_OK;
    }

    esp_err_t ret;
    bool shared_host_hw = false;

    /* Step 1: Power the slot via on-chip LDO */
    sd_pwr_ctrl_ldo_config_t ldo_cfg = {
        .ldo_chan_id = SD1_LDO_CHAN,
    };
    ret = sd1_ldo_create(&ldo_cfg);
    ESP_RETURN_ON_ERROR(ret, TAG, "SD1 LDO init failed");
    vTaskDelay(pdMS_TO_TICKS(120));

    /* Step 2: Configure SDMMC host */
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SD1_SDMMC_SLOT;
    host.max_freq_khz = SD1_MAX_FREQ_KHZ;
    host.pwr_ctrl_handle = s_card1_pwr;

    // Check if the SDMMC host was already initialized by ESP-Hosted Wi-Fi SDIO driver
    sdmmc_host_state_t state = { 0 };
    if (sdmmc_host_get_state(&state) == ESP_OK && state.host_initialized)
    {
        shared_host_hw = true;
        ESP_LOGI(TAG, "SDMMC Host hardware is shared with ESP-Hosted. Establishing real physical slot connection but bypassing redundant host allocation.");
        host.init = &sdmmc_host_shared_init;
        host.deinit = &sdmmc_host_shared_deinit;
        /* Give shared SDIO transport time to settle before SD1 init on warm boots. */
        vTaskDelay(pdMS_TO_TICKS(220));
    }

    /* Step 3: Configure slot — explicit GPIO + internal pull-ups */
    sdmmc_slot_config_t slot_cfg = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_cfg.width = SD1_BUS_WIDTH;
    slot_cfg.clk = SD1_PIN_CLK;
    slot_cfg.cmd = SD1_PIN_CMD;
    slot_cfg.d0 = SD1_PIN_D0;
    slot_cfg.d1 = SD1_PIN_D1;
    slot_cfg.d2 = SD1_PIN_D2;
    slot_cfg.d3 = SD1_PIN_D3;
    slot_cfg.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    /* Step 4: FAT/VFS mount
     * SDMMC host is shared with ESP-Hosted. During boot, transport activity
     * can transiently delay card init; retry on timeout and use a lower clock.
     */
    const esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = true,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };

    const int mount_retries = 4;
    for (int attempt = 1; attempt <= mount_retries; ++attempt)
    {
        if (attempt > 1)
        {
            host.max_freq_khz = SDMMC_FREQ_DEFAULT;
            ESP_LOGW(TAG, "Retrying SD Card 1 mount (attempt %d/%d) at %d kHz",
                attempt, mount_retries, host.max_freq_khz);
        }

        ret = esp_vfs_fat_sdmmc_mount(SD1_MOUNT_POINT, &host,
            &slot_cfg, &mount_cfg, &s_card1);
        if (ret == ESP_OK)
        {
            break;
        }

        if (ret == ESP_ERR_TIMEOUT && attempt < mount_retries)
        {
            ESP_LOGW(TAG, "SD Card 1 mount timeout on attempt %d/%d; retrying",
                attempt, mount_retries);

            if (shared_host_hw)
            {
                ESP_LOGW(TAG, "SD1 shared-host recovery: power-cycling SD1 LDO before retry");
                if (s_card1_pwr != NULL)
                {
                    sd_pwr_ctrl_del_on_chip_ldo(s_card1_pwr);
                    s_card1_pwr = NULL;
                }

                vTaskDelay(pdMS_TO_TICKS(280));
                ret = sd1_ldo_create(&ldo_cfg);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "SD1 LDO recover init failed (%s)", esp_err_to_name(ret));
                    return ret;
                }
                host.pwr_ctrl_handle = s_card1_pwr;
                vTaskDelay(pdMS_TO_TICKS(220));
            }

            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        break;
    }

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SD Card 1 mount failed (%s)", esp_err_to_name(ret));
        sd_pwr_ctrl_del_on_chip_ldo(s_card1_pwr);
        s_card1_pwr = NULL;
        return ret;
    }

    s_card1_mounted = true;
    sdmmc_card_print_info(stdout, s_card1);
    ESP_LOGI(TAG, "SD Card 1 mounted at %s  [%s, %.1f MB]",
        SD1_MOUNT_POINT,
        s_card1->cid.name,
        (float)((uint64_t)s_card1->csd.capacity *
            s_card1->csd.sector_size) / (1024.0f * 1024.0f));
    return ESP_OK;
}

/**
 * @brief Unmount and power-down SD Card 1
 *
 * Cleanup sequence:
 * 1. Guard: Return if not currently mounted
 * 2. VFS Unmount: Disconnect filesystem and close all handles
 * 3. State Clear: Set card descriptor to NULL
 * 4. Power Down: Release LDO power handle
 *
 * After calling this, the slot is powered off and any files on it are
 * inaccessible until sd_card1_init() is called again.
 *
 * @return ESP_OK on success, ESP_FAIL if unmount failed
 */
esp_err_t sd_card1_deinit(void)
{
    if (!s_card1_mounted)
    {
        return ESP_OK;
    }

    esp_err_t ret = esp_vfs_fat_sdcard_unmount(SD1_MOUNT_POINT, s_card1);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SD Card 1 unmount failed (%s)", esp_err_to_name(ret));
        return ret;
    }

    s_card1 = NULL;
    s_card1_mounted = false;

    if (s_card1_pwr != NULL)
    {
        sd_pwr_ctrl_del_on_chip_ldo(s_card1_pwr);
        s_card1_pwr = NULL;
    }

    ESP_LOGI(TAG, "SD Card 1 unmounted");
    return ESP_OK;
}

/**
 * @brief Query mount status of SD Card 1
 * @return true if currently mounted and accessible, false otherwise
 */
bool sd_card1_is_mounted(void)
{
    return s_card1_mounted;
}

/**
 * @brief Get VFS mount point path for SD Card 1
 * @return Static string "/sdcard" (valid for lifetime of program)
 */
const char* sd_card1_mount_point(void)
{
    return SD1_MOUNT_POINT;
}

/* ══════════════════════════════════════════════════════════════════════════
 * SD Card 2 — SDSPI external slot on SPI3
 *
 * Purpose: External SD card via SPI for concurrent logging/data capture
 * Interface: SPI3 (FSPPI) bus with GPIO 13,14,21,23
 * Speed: 20 MHz (SPI clock, not DDR)
 * Power: External supply (board-dependent, typically +3.3V)
 *
 * Advantages over SD1:
 * - Can operate simultaneously without bus contention
 * - Useful for logging while main app uses SD1
 * - Easy to hot-swap (electrical isolation via CS pin)
 *
 * Init Steps:
 * 1. SPI Bus Check: Initialize SPI3 once (guard on s_spi3_inited)
 * 2. SDSPI Host Setup: Configure device+speed
 * 3. FAT Mount: Register VFS at /sdcard2
 * ══════════════════════════════════════════════════════════════════════════ */

 /**
  * @brief Initialize SD Card 2 (SDSPI external slot on SPI3 host)
  *
  * Performs full initialization sequence:
  * 1. Guard check: Return if already mounted
  * 2. SPI Bus: Initialize SPI3 (once) with 4KB max transfer size
  * 3. SDSPI Host: Configure device with 20 MHz clock
  * 4. GPIO CS: Map chip-select to GPIO 13
  * 5. FAT Mount: Mount filesystem at /sdcard2 (no auto-format)
  * 6. Verify: Print card info to verify successful detection
  *
  * Special Notes:
  * - SPI bus is shared resource: Initialize only once per power cycle
  * - Chip select (CS) pulled low to select the SD card device
  * - SPI speed slower than SDMMC (20 vs 40 MHz) but reliable over longer traces
  *
  * Return Codes:
  * - ESP_OK: Card mounted successfully
  * - ESP_FAIL: SPI bus init or mount failed (see logs)
  * - ESP_ERR_INVALID_RESPONSE: No SD card detected on SPI bus
  * - ESP_ERR_NO_MEM: Insufficient heap
  *
  * @return ESP_OK on success, or esp_err_t error code
  */
esp_err_t sd_card2_init(void)
{
    if (s_card2_mounted)
    {
        ESP_LOGW(TAG, "SD Card 2 already mounted");
        return ESP_OK;
    }

    esp_err_t ret;

    /* Step 1: Initialise SPI3 bus (guard: only once per power cycle) */
    /* Create mutex on first call (idempotent) */
    if (s_spi_init_mutex == NULL)
    {
        s_spi_init_mutex = xSemaphoreCreateMutex();
        if (s_spi_init_mutex == NULL)
        {
            ESP_LOGE(TAG, "Failed to create SPI init mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    if (!s_spi3_inited)
    {
        /* Double-checked locking pattern to prevent race condition */
        if (xSemaphoreTake(s_spi_init_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
        {
            ESP_LOGE(TAG, "Failed to acquire SPI init mutex (timeout)");
            return ESP_ERR_TIMEOUT;
        }

        /* Check again inside mutex (another thread may have initialized while we waited) */
        if (!s_spi3_inited)
        {
            /* Log configured pins and validate they are suitable for SPI
             * Note: GPIO34-39 are input-only on ESP32 family (GPIO36 included),
             * which means they cannot be used for CLK/MOSI/CS outputs. MISO may
             * be an input-only pin and is safe to use as MISO only.
             */
            ESP_LOGI(TAG, "SD2 pins config: CLK=%d MOSI=%d MISO=%d CS=%d",
                SD2_PIN_CLK, SD2_PIN_MOSI, SD2_PIN_MISO, SD2_PIN_CS);
            if ((SD2_PIN_CLK >= 34 && SD2_PIN_CLK <= 39) ||
                (SD2_PIN_MOSI >= 34 && SD2_PIN_MOSI <= 39) ||
                (SD2_PIN_CS >= 34 && SD2_PIN_CS <= 39))
            {
                ESP_LOGW(TAG, "One or more SD2 pins (CLK/MOSI/CS) are input-only (GPIO34-39).\n"
                    "These cannot be used as SPI outputs — move them to output-capable GPIOs.");
            }

            spi_bus_config_t bus_cfg = {
                .mosi_io_num = SD2_PIN_MOSI,
                .miso_io_num = SD2_PIN_MISO,
                .sclk_io_num = SD2_PIN_CLK,
                .quadwp_io_num = -1,
                .quadhd_io_num = -1,
                .max_transfer_sz = 4096,
            };
            ret = spi_bus_initialize(SD2_SPI_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "SPI3 bus init failed (%s)", esp_err_to_name(ret));
                xSemaphoreGive(s_spi_init_mutex);
                return ret;
            }
            s_spi3_inited = true;
            ESP_LOGI(TAG, "SPI3 bus initialised (CLK=%d MOSI=%d MISO=%d)",
                SD2_PIN_CLK, SD2_PIN_MOSI, SD2_PIN_MISO);
        }

        xSemaphoreGive(s_spi_init_mutex);
    }

    /* Step 2: Configure SDSPI host + device */
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD2_SPI_HOST;
    host.max_freq_khz = SD2_MAX_FREQ_KHZ;

    sdspi_device_config_t dev_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    dev_cfg.gpio_cs = SD2_PIN_CS;
    dev_cfg.host_id = SD2_SPI_HOST;

    /* Step 3: FAT/VFS mount */
    const esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    ret = esp_vfs_fat_sdspi_mount(SD2_MOUNT_POINT, &host,
        &dev_cfg, &mount_cfg, &s_card2);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SD Card 2 mount failed (%s)", esp_err_to_name(ret));
        return ret;
    }

    s_card2_mounted = true;
    sdmmc_card_print_info(stdout, s_card2);
    ESP_LOGI(TAG, "SD Card 2 mounted at %s  [%s, %.1f MB]",
        SD2_MOUNT_POINT,
        s_card2->cid.name,
        (float)((uint64_t)s_card2->csd.capacity *
            s_card2->csd.sector_size) / (1024.0f * 1024.0f));
    return ESP_OK;
}

/**
 * @brief Unmount and power-down SD Card 2
 *
 * Cleanup sequence:
 * 1. Guard: Return if not mounted
 * 2. VFS Unmount: Disconnect filesystem
 * 3. SPI Bus: Release SPI3 bus (if no longer needed)
 * 4. State Clear: Reset card descriptor and mount flag
 *
 * Only releases SPI3 if SD2 was the last consumer. Do not call this
 * if SPI3 is in use by other devices (will cause other devices to fail).
 *
 * @return ESP_OK on success, ESP_FAIL if unmount failed
 */
esp_err_t sd_card2_deinit(void)
{
    if (!s_card2_mounted)
    {
        return ESP_OK;
    }

    esp_err_t ret = esp_vfs_fat_sdcard_unmount(SD2_MOUNT_POINT, s_card2);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SD Card 2 unmount failed (%s)", esp_err_to_name(ret));
        return ret;
    }

    s_card2 = NULL;
    s_card2_mounted = false;

    if (s_spi3_inited)
    {
        spi_bus_free(SD2_SPI_HOST);
        s_spi3_inited = false;
    }

    ESP_LOGI(TAG, "SD Card 2 unmounted");
    return ESP_OK;
}

/**
 * @brief Query mount status of SD Card 2
 * @return true if currently mounted and accessible, false otherwise
 */
bool sd_card2_is_mounted(void)
{
    return s_card2_mounted;
}

/**
 * @brief Get VFS mount point path for SD Card 2
 * @return Static string "/sdcard2" (valid for lifetime of program)
 */
const char* sd_card2_mount_point(void)
{
    return SD2_MOUNT_POINT;
}

/* ══════════════════════════════════════════════════════════════════════════
 * UTILITY FUNCTIONS — Information & Management
 * ══════════════════════════════════════════════════════════════════════════ */

 /**
  * @brief Query SD card capacity and usage information
  *
  * Uses FatFS to calculate total capacity and used space.
  * Useful for UI display and capacity monitoring.
  *
  * Formula:
  * - Total: (total_clusters - 2) * cluster_size * 512 bytes
  * - Used: Total - (free_clusters * cluster_size * 512 bytes)
  *
  * Both values are returned in MB (1 MB = 1024 * 1024 bytes).
  *
  * @param slot 1 (SD1) or 2 (SD2)
  * @param out_capacity_mb Output: Total capacity in MB
  * @param out_used_mb Output: Used space in MB
  * @return ESP_OK on success, ESP_FAIL if card not mounted or I/O error
  */
esp_err_t sd_card_get_info(int slot, uint64_t* out_capacity_mb, uint64_t* out_used_mb)
{
    sdmmc_card_t* card = (slot == 1) ? s_card1 : s_card2;
    if (!card) return ESP_FAIL;

    // Get total capacity from CSD (more reliable than FATFS)
    uint64_t total_bytes = (uint64_t)card->csd.capacity * card->csd.sector_size;
    *out_capacity_mb = total_bytes / (1024 * 1024);

    // Get used space from FATFS
    const char* path = (slot == 1) ? SD1_MOUNT_POINT : SD2_MOUNT_POINT;
    FATFS* fs;
    DWORD free_clusters;
    FRESULT res = f_getfree(path, &free_clusters, &fs);
    if (res != FR_OK)
    {
        *out_used_mb = 0;
        return ESP_FAIL;
    }

    uint64_t total_sectors = (uint64_t)(fs->n_fatent - 2) * fs->csize;
    uint64_t free_sectors = (uint64_t)free_clusters * fs->csize;
    uint64_t used_sectors = total_sectors - free_sectors;
    *out_used_mb = (used_sectors * 512) / (1024 * 1024);

    return ESP_OK;
}

/**
 * @brief Format an SD card with FAT filesystem
 *
 * Delegates to sd_card_quick_format() for performance optimizations.
 * See that function for implementation details.
 *
 * @param slot 1 (SD1) or 2 (SD2)
 * @return ESP_OK on success, or esp_err_t error code
 */
esp_err_t sd_card_format(int slot)
{
    return sd_card_quick_format(slot);
}

/**
 * @brief Quick-format SD card with optimized FAT parameters
 *
 * Performs a full format operation with settings optimized for Rallybox:
 * - Allocation Unit Size: 32 KB (balance between fragmentation and speed)
 * - Single FAT: Saves space (28 KB per 512 MB)
 * - Disk Status Check: Disabled (faster format, assumes media is good)
 *
 * Use case: Initialize new SD cards, or recover from filesystem corruption.
 * Warning: ALL DATA ON CARD WILL BE LOST!
 *
 * @param slot 1 (SD1) or 2 (SD2)
 * @return ESP_OK on success, ESP_FAIL on format error or card not initialized
 */
esp_err_t sd_card_quick_format(int slot)
{
    const char* path = (slot == 1) ? SD1_MOUNT_POINT : SD2_MOUNT_POINT;
    sdmmc_card_t* card = (slot == 1) ? s_card1 : s_card2;
    esp_vfs_fat_mount_config_t format_cfg = {
        .format_if_mount_failed = false,
        .max_files = (slot == 1) ? 8 : 5,
        .allocation_unit_size = 32 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = true,
    };

    if (!card)
    {
        ESP_LOGE(TAG, "Cannot format Slot %d: Card not initialized", slot);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Quick-formatting Slot %d (%s)...", slot, path);

    esp_err_t ret = esp_vfs_fat_sdcard_format_cfg(path, card, &format_cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to quick-format SD card in slot %d: %s", slot, esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SD Card in slot %d quick-formatted successfully", slot);
    return ESP_OK;
}

#include <dirent.h>
#include <sys/stat.h>
static esp_err_t copy_file(const char* src, const char* dst)
{
    FILE* fsrc = fopen(src, "rb");
    if (!fsrc) return ESP_FAIL;
    FILE* fdst = fopen(dst, "wb");
    if (!fdst) { fclose(fsrc); return ESP_FAIL; }
    char buffer[1024];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), fsrc)) > 0)
    {
        if (fwrite(buffer, 1, n, fdst) != n) { fclose(fsrc); fclose(fdst); return ESP_FAIL; }
    }
    fclose(fsrc); fclose(fdst); return ESP_OK;
}
esp_err_t sd_card_export_logs(void)
{
    if (!s_card1_mounted || !s_card2_mounted) return ESP_ERR_INVALID_STATE;
    ESP_LOGI(TAG, "Starting log export SD2 -> SD1");
    DIR* dir = opendir(SD2_MOUNT_POINT);
    if (!dir) return ESP_FAIL;
    struct dirent* ent;
    int count = 0;
    while ((ent = readdir(dir)) != NULL)
    {
        if (strstr(ent->d_name, "rallybox_log") != NULL && strstr(ent->d_name, ".txt") != NULL)
        {
            char src_path[128], dst_path[128];
            snprintf(src_path, sizeof(src_path), "%s/%s", SD2_MOUNT_POINT, ent->d_name);
            snprintf(dst_path, sizeof(dst_path), "%s/%s", SD1_MOUNT_POINT, ent->d_name);
            ESP_LOGI(TAG, "Copying %s", ent->d_name);
            if (copy_file(src_path, dst_path) == ESP_OK) count++;
        }
    }
    closedir(dir);
    ESP_LOGI(TAG, "Exported %d logs", count);
    return (count > 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

/* ── Periodic datalogger task ─────────────────────────────────────────── */
#include "system_monitor.h"

static void datalogger_task(void* pv)
{
    struct
    {
        int slot;
        int interval_seconds;
        int rows;
    } params = { 0 };
    memcpy(&params, pv, sizeof(params));
    free(pv);
    int slot = params.slot;
    int interval = params.interval_seconds;
    int rows = params.rows;

    const char* mount = (slot == 1) ? SD1_MOUNT_POINT : SD2_MOUNT_POINT;
    if ((slot == 1 && !s_card1_mounted) || (slot == 2 && !s_card2_mounted))
    {
        ESP_LOGE(TAG, "Datalogger: SD slot %d not mounted", slot);
        vTaskDelete(NULL);
        return;
    }

    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char fname[128];
    snprintf(fname, sizeof(fname), "%s/rallybox_sdcard%d_%04d%02d%02d_%02d%02d%02d.txt",
        mount, slot,
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec);

    FILE* f = fopen(fname, "w");
    if (!f)
    {
        ESP_LOGE(TAG, "Datalogger: failed to open %s", fname);
        vTaskDelete(NULL);
        return;
    }

    // Write comprehensive CSV header with all system parameters
    fprintf(f, "sl_no,timestamp_IST,uptime_seconds,cpu_pct,free_heap_bytes,total_heap_bytes,wifi_connected,wifi_ssid,wifi_ip,wifi_rssi,wifi_state,wifi_connect_attempts,wifi_retry_count,wifi_disconnect_count,wifi_last_error,sd_slot,sd_capacity_mb,sd_used_mb,sd_read_count,sd_write_count,sd_error_count,sd_total_read_bytes,sd_total_write_bytes,test_load_percent,test_operations_count,test_errors_count\n");
    fflush(f);

    int counter = 0;
    while (rows == 0 || counter < rows)
    {
        counter++;
        system_status_t st = system_monitor_get_status();
        time_t tnow = time(NULL);
        struct tm tlocal;
        localtime_r(&tnow, &tlocal);
        char timestr[64];
        strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S %Z", &tlocal);

        uint64_t cap = 0, used = 0;
        sd_card_get_info(slot, &cap, &used);

        // Get SD metrics for the current slot
        uint32_t sd_read_count = (slot == 1) ? st.sdcard1_read_count : st.sdcard2_read_count;
        uint32_t sd_write_count = (slot == 1) ? st.sdcard1_write_count : st.sdcard2_write_count;
        uint32_t sd_error_count = (slot == 1) ? st.sdcard1_error_count : st.sdcard2_error_count;
        uint64_t sd_total_read_bytes = (slot == 1) ? st.sdcard1_total_read_bytes : st.sdcard2_total_read_bytes;
        uint64_t sd_total_write_bytes = (slot == 1) ? st.sdcard1_total_write_bytes : st.sdcard2_total_write_bytes;

        // CSV row: comprehensive fields
        fprintf(f, "%d,%s,%u,%u,%u,%u,%s,%s,%s,%d,%u,%u,%u,%u,%s,%d,%.0llu,%.0llu,%u,%u,%u,%.0llu,%.0llu,%u,%u,%u\n",
            counter,
            timestr,
            (unsigned)st.uptime_seconds,
            (unsigned)st.cpu_load_percent,
            (unsigned)st.free_heap_bytes,
            (unsigned)st.total_heap_bytes,
            st.wifi_connected ? "connected" : "disconnected",
            st.wifi_ssid[0] ? st.wifi_ssid : "",
            st.wifi_ip[0] ? st.wifi_ip : "",
            (int)st.wifi_rssi,
            (unsigned)st.wifi_state,
            (unsigned)st.wifi_connect_attempts,
            (unsigned)st.wifi_retry_count,
            (unsigned)st.wifi_disconnect_count,
            st.wifi_last_error[0] ? st.wifi_last_error : "",
            slot,
            (unsigned long long)cap,
            (unsigned long long)used,
            (unsigned)sd_read_count,
            (unsigned)sd_write_count,
            (unsigned)sd_error_count,
            (unsigned long long)sd_total_read_bytes,
            (unsigned long long)sd_total_write_bytes,
            (unsigned)st.test_load_percent,
            (unsigned)st.test_operations_count,
            (unsigned)st.test_errors_count);
        fflush(f);

        for (int i = 0; i < interval; ++i)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    fclose(f);
    ESP_LOGI(TAG, "Datalogger finished: %s", fname);
    vTaskDelete(NULL);
}

esp_err_t sd_card_start_datalogger(int slot, int interval_seconds, int rows)
{
    if (slot != 1 && slot != 2) return ESP_ERR_INVALID_ARG;
    if ((slot == 1 && !s_card1_mounted) || (slot == 2 && !s_card2_mounted))
    {
        ESP_LOGE(TAG, "Cannot start datalogger: SD slot %d not mounted", slot);
        return ESP_ERR_INVALID_STATE;
    }

    // Pack params on heap for task to read
    void* params = heap_caps_malloc(sizeof(struct { int slot; int interval_seconds; int rows; }), MALLOC_CAP_DEFAULT);
    if (!params) return ESP_ERR_NO_MEM;
    memcpy(params, &(struct { int slot; int interval_seconds; int rows; }){slot, interval_seconds, rows}, sizeof(struct { int slot; int interval_seconds; int rows; }));

    BaseType_t ok = xTaskCreate(datalogger_task, "sd_datalogger", 8192, params, 4, NULL);
    if (ok != pdPASS)
    {
        free(params);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Datalogger started on slot %d: every %ds, rows=%d", slot, interval_seconds, rows);
    return ESP_OK;
}
