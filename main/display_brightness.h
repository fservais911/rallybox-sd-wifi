/**
 * @file display_brightness.h
 * @brief Display brightness (backlight PWM) control module
 * @author Rallybox Development Team
 * @developer Akhil
 * 
 * Provides a high-level API for managing the display backlight intensity.
 * Wraps the BSP (Board Support Package) brightness functions to add:
 * - Step-based increase/decrease (10% increments by default)
 * - Configurable min/max brightness limits
 * - Persistent level tracking (no DSP read-backs needed)
 * - State queries (is_min, is_max)
 * 
 * Architecture:
 * - Hardware: PWM on GPIO controlled by BSP driver
 * - State: Tracked in static variable to avoid DSP polling
 * - Constraints: Min/max clamping prevents OOB values
 * 
 * Usage Example:
 * @code
 * display_brightness_init();           // Set to 80% (default)
 * display_brightness_set(100);         // Set max (100%)
 * display_brightness_decrease();       // Drop to 90%
 * int level = display_brightness_get(); // Read: 90
 * @endcode
 * 
 * @note Brightness 0% is intentionally allowed to power-off backlight
 *       during boot animation to avoid white flash
 * @note All functions are ISR-safe (no FreeRTOS calls)
 */

#ifndef DISPLAY_BRIGHTNESS_H
#define DISPLAY_BRIGHTNESS_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Configuration ── */

/** Default brightness on startup (percent, 0–100) */
#define BRIGHTNESS_DEFAULT      80

/** Minimum allowed brightness (percent) */
/* Allow 0% so the display can be initialised with the backlight off
 * to avoid a white flash while LVGL renders the first frame. */
#define BRIGHTNESS_MIN          0

/** Maximum allowed brightness (percent) */
#define BRIGHTNESS_MAX          100

/** Step applied by increase/decrease calls (percent) */
#define BRIGHTNESS_STEP         10

/* ── API ── */

/**
 * @brief Initialise the brightness subsystem and apply the default level.
 *
 * Must be called after bsp_display_start_with_config().
 *
 * @return ESP_OK on success, ESP_FAIL if BSP initialisation failed.
 */
esp_err_t display_brightness_init(void);

/**
 * @brief Set brightness to an explicit level.
 *
 * Values are clamped to [BRIGHTNESS_MIN, BRIGHTNESS_MAX].
 *
 * @param percent  Target brightness in percent (0–100).
 * @return ESP_OK on success.
 */
esp_err_t display_brightness_set(int percent);

/**
 * @brief Increase brightness by BRIGHTNESS_STEP.
 *
 * No-ops if already at BRIGHTNESS_MAX.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already at maximum.
 */
esp_err_t display_brightness_increase(void);

/**
 * @brief Decrease brightness by BRIGHTNESS_STEP.
 *
 * No-ops if already at BRIGHTNESS_MIN.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already at minimum.
 */
esp_err_t display_brightness_decrease(void);

/**
 * @brief Get the current brightness level.
 *
 * @return Current brightness percent (0–100), or -1 if not initialised.
 */
int display_brightness_get(void);

/**
 * @brief Return true if brightness is already at the maximum.
 */
bool display_brightness_is_max(void);

/**
 * @brief Return true if brightness is already at the minimum.
 */
bool display_brightness_is_min(void);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_BRIGHTNESS_H */
