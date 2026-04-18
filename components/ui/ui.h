/**
 * @file ui.h
 * @brief Rallybox UI Component - Public API
 * @author EEZ Studio Generator + Rallybox Team
 * @developer Akhil
 * 
 * This header provides the public interface for the LVGL-based UI component
 * generated from EEZ Studio. It manages screen lifecycle, object initialization,
 * and display updates for the Rallybox rally computer dashboard.
 * 
 * Key Functions:
 * - ui_init(): Initialize all screens and objects (call once from app_main)
 * - ui_tick(): Run LVGL timers & animations (called by main rendering loop)
 * - loadScreen(): Switch between screens with optional animation
 * 
 * Architecture:
 * - Screen-based: Two main screens (BootingScreen, Dashboard)
 * - Object model: All LVGL objects stored in global 'objects' struct
 * - Event-driven: User interactions handled via LVGL event callbacks
 * - Display pipeline: App updates metrics → ui_logic_update_*() → LVGL renders
 * 
 * Typical Usage:
 * @code
 * // In app_main()
 * ui_init();                           // Initialize UI (once)
 * loadScreen(SCREEN_ID_BOOTINGSCREEN); // Show boot splash
 * 
 * // In rendering loop (Core 0, runs continuously)
 * while (1) {
 *     ui_tick();                       // Update animations
 *     vTaskDelay(1 / portTICK_PERIOD_MS);
 * }
 * 
 * // In system_monitor_task() (1 Hz, every 1 second)
 * ui_logic_set_uptime(status->uptime_seconds);
 * ui_logic_update_wifi_live(status);
 * @endcode
 * 
 * @note All screen objects accessed via global 'objects' pointer
 * @note Multi-core safe: Render thread (Core 0) separate from update thread (Core 1)
 * @note Display operations protected by bsp_display_lock() mutex
 * @see screens.h for screen definitions and object hierarchy
 */

#ifndef EEZ_LVGL_UI_GUI_H
#define EEZ_LVGL_UI_GUI_H

#include "lvgl.h"
#include "screens.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize all UI screens and LVGL objects
 * 
 * Performs one-time initialization:
 * 1. Creates all screen objects (BootingScreen, Dashboard)
 * 2. Registers fonts and images
 * 3. Sets up default styling
 * 4. Initializes event handlers
 * 5. Prepares rendering pipeline
 * 
 * Must be called exactly once, before any screen loading or object access.
 * Typically called early in app_main() after display hardware initialization.
 * 
 * @return void (no error handling; failures are logged)
 * 
 * @note Blocking call (takes ~50-100ms on first run)
 * @note Must be called after bsp_display_start_with_config()
 * @note Safe to call multiple times (idempotent after first init)
 */
void ui_init(void);

/**
 * @brief Run LVGL timers and update animations
 * 
 * This function should be called periodically (typically every 1-5ms)
 * from the main rendering loop. It:
 * 1. Processes LVGL internal timers (animations, transitions)
 * 2. Checks for input events (touch, buttons)
 * 3. Marks dirty regions for redraw
 * 4. Triggers screen refresh if needed
 * 
 * Performance: ~2-5ms per call (depending on screen complexity)
 * 
 * @return void
 * 
 * @note Non-blocking: Returns immediately if no work needed
 * @note Call rate: 200-1000 Hz (usually via xTaskDelay in LVGL task)
 * @note Safe for concurrent calls (LVGL internally synchronized)
 */
void ui_tick(void);

/**
 * @brief Load a screen with optional animation
 * 
 * Switches the active screen, optionally with fade/slide transition animation.
 * The animation duration is defined by EEZ Studio design (typically 300ms).
 * 
 * Example Usage:
 * @code
 * // Show boot screen (no animation)
 * loadScreen(SCREEN_ID_BOOTINGSCREEN);
 * 
 * // After 10 seconds, transition to dashboard with fade
 * vTaskDelay(pdMS_TO_TICKS(10000));
 * loadScreen(SCREEN_ID_DASHBOARD);  // Fade animation configured in EEZ
 * @endcode
 * 
 * @param screenId ID from enum ScreensEnum (SCREEN_ID_BOOTINGSCREEN, SCREEN_ID_DASHBOARD, etc.)
 * @return void
 * 
 * @note Non-blocking: Animation happens asynchronously
 * @note Previous screen objects remain allocated (memory not freed)
 * @note Safe to call from any task/context (but protect display with bsp_display_lock)
 * @note Animation controlled by EEZ Studio design (configure in .eez-project file)
 */
void loadScreen(enum ScreensEnum screenId);

#ifdef __cplusplus
}
#endif

#endif // EEZ_LVGL_UI_GUI_H