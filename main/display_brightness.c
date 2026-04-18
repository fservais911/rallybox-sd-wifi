/**
 * @file display_brightness.c
 * @brief Display brightness (backlight PWM) control implementation
 * @author Rallybox Development Team
 * @developer Akhil
 * 
 * This module implements brightness control for the ESP32-P4 display backlight.
 * It provides a clean abstraction over the BSP (Board Support Package) layer,
 * adding state caching and range constraints.
 * 
 * Implementation Details:
 * - **State Caching**: Stores current brightness in static variable to avoid
 *   DSP read-backs. Uses -1 as sentinel for uninitialized state.
 * - **Range Clamping**: All values clamped to [BRIGHTNESS_MIN, BRIGHTNESS_MAX]
 * - **Logging**: Uses ESP_LOGI/LOGD/LOGW for debugging
 * 
 * Hardware Interface:
 * - Delegates to bsp_display_brightness_set/get() from board support layer
 * - PWM frequency: Typically 5 kHz (configured by BSP)
 * - Output: GPIO-controlled based on board design
 * 
 * Error Handling:
 * - ESP_FAIL on BSP init failure
 * - ESP_ERR_INVALID_STATE if not initialized
 * - No assertion-style failures (returns error codes)
 * 
 * @note ISR-safe: Can be called from any context (main, ISR, FreeRTOS task)
 * @note Not thread-safe: Protect with mutex if multi-thread access needed
 */

#include "display_brightness.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_x.h"
#include "esp_log.h"

static const char *TAG = "brightness";   ///< Log tag for this module

/// Static cache of current brightness level (-1 = uninitialized)
static int  s_current = -1;

/* ─────────────────────────────────────────────────────────────────────────
 * HELPER FUNCTIONS
 * ───────────────────────────────────────────────────────────────────────── */

/**
 * @brief Clamp integer to [lo, hi] range
 * 
 * @param v Value to clamp
 * @param lo Lower bound (inclusive)
 * @param hi Upper bound (inclusive)
 * @return Clamped value
 */
static int clamp(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ─────────────────────────────────────────────────────────────────────────
 * PUBLIC API
 * ───────────────────────────────────────────────────────────────────────── */

/**
 * @brief Initialize the brightness subsystem
 * 
 * Sets the display backlight to BRIGHTNESS_DEFAULT (80%) and caches the value.
 * Must be called after bsp_display_start_with_config() but before any UI rendering
 * to ensure brightness state is consistent.
 * 
 * Failure scenarios:
 * - BSP not initialized
 * - Invalid GPIO configuration
 * - PWM controller unavailable
 * 
 * @return ESP_OK on success, ESP_FAIL if BSP initialization failed
 */
esp_err_t display_brightness_init(void)
{
    esp_err_t ret = bsp_display_brightness_set(BRIGHTNESS_DEFAULT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "brightness init failed: %s", esp_err_to_name(ret));
        return ESP_FAIL;
    }
    s_current = BRIGHTNESS_DEFAULT;
    ESP_LOGI(TAG, "initialised at %d%%", s_current);
    return ESP_OK;
}

/**
 * @brief Set brightness to an explicit level
 * 
 * Values outside [BRIGHTNESS_MIN, BRIGHTNESS_MAX] are silently clamped.
 * Successfully clamped values are considered a successful call (not an error).
 * 
 * Example: set(120) on range [0-100] → clamps to 100, returns ESP_OK
 * 
 * @param percent Target brightness in percent (0–100, auto-clamped)
 * @return ESP_OK on success, ESP_FAIL if hardware write failed
 */
esp_err_t display_brightness_set(int percent)
{
    int target = clamp(percent, BRIGHTNESS_MIN, BRIGHTNESS_MAX);
    esp_err_t ret = bsp_display_brightness_set(target);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set failed: %s", esp_err_to_name(ret));
        return ret;
    }
    s_current = target;
    ESP_LOGD(TAG, "set to %d%%", s_current);
    return ESP_OK;
}

/**
 * @brief Increase brightness by BRIGHTNESS_STEP (default: 10%)
 * 
 * Idempotent at maximum: If already at BRIGHTNESS_MAX, status returned without change.
 * Typical use case: Hardware button handler increments brightness on each press.
 * 
 * Return Codes:
 * - ESP_OK: Brightness increased successfully
 * - ESP_ERR_INVALID_STATE: Already at maximum, or not initialized
 * 
 * @return ESP_OK on success, or error code
 */
esp_err_t display_brightness_increase(void)
{
    if (s_current < 0) {
        ESP_LOGW(TAG, "not initialised");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_current >= BRIGHTNESS_MAX) {
        ESP_LOGD(TAG, "already at maximum (%d%%)", BRIGHTNESS_MAX);
        return ESP_ERR_INVALID_STATE;
    }
    return display_brightness_set(s_current + BRIGHTNESS_STEP);
}

/**
 * @brief Decrease brightness by BRIGHTNESS_STEP (default: 10%)
 * 
 * Idempotent at minimum: If already at BRIGHTNESS_MIN, status returned without change.
 * Typical use case: Hardware button handler decrements brightness on each press.
 * 
 * Return Codes:
 * - ESP_OK: Brightness decreased successfully
 * - ESP_ERR_INVALID_STATE: Already at minimum, or not initialized
 * 
 * @return ESP_OK on success, or error code
 */
esp_err_t display_brightness_decrease(void)
{
    if (s_current < 0) {
        ESP_LOGW(TAG, "not initialised");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_current <= BRIGHTNESS_MIN) {
        ESP_LOGD(TAG, "already at minimum (%d%%)", BRIGHTNESS_MIN);
        return ESP_ERR_INVALID_STATE;
    }
    return display_brightness_set(s_current - BRIGHTNESS_STEP);
}

/**
 * @brief Get the current brightness level
 * 
 * Uses cached value from last successful set/init call.
 * No hardware polling (for efficiency).
 * 
 * @return Current brightness percent (0–100), or -1 if not initialized
 */
int display_brightness_get(void)
{
    return s_current;
}

/**
 * @brief Query if brightness is at maximum limit
 * 
 * Useful for UI feedback (e.g., disable "+" button when at max)
 * 
 * @return true if current >= BRIGHTNESS_MAX, false otherwise
 */
bool display_brightness_is_max(void)
{
    return s_current >= BRIGHTNESS_MAX;
}

/**
 * @brief Query if brightness is at minimum limit
 * 
 * Useful for UI feedback (e.g., disable "-" button when at min)
 * 
 * @return true if current <= BRIGHTNESS_MIN, false otherwise
 */
bool display_brightness_is_min(void)
{
    return s_current <= BRIGHTNESS_MIN;
}
