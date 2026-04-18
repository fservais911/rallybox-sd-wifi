/**
 * @file actions.h
 * @brief Rallybox UI Event Handlers & Actions
 * @author EEZ Studio Generator + Rallybox Team
 * @developer Akhil
 * 
 * Declares callback functions for all UI events (button clicks, keyboard input, etc).
 * These handlers bridge LVGL events to application logic.
 * 
 * Event System Flow:
 * @code
 * User Action (click button)
 *     ↓
 * LVGL detects event (LV_EVENT_CLICKED)
 *     ↓
 * Callback invoked (e.g., action_connect_wifi)
 *     ↓
 * Handler processes event (validate input, call system functions)
 *     ↓
 * UI updates or system state changes
 * @endcode
 * 
 * Handler Registration:
 * All actions are registered in EEZ Studio design and linked in screens.c:
 * @code
 * lv_obj_add_event_cb(objects.wifi_conenct_button_1, action_connect_wifi, LV_EVENT_CLICKED, NULL);
 * @endcode
 * 
 * Event Parameter Details:
 * - lv_event_t *e: LVGL event structure containing:
 *   - e->current_target: Widget that triggered event
 *   - e->code: Event type (LV_EVENT_CLICKED, LV_EVENT_VALUE_CHANGED, etc)
 *   - e->param: User data (if registered with lv_obj_add_event_cb)
 *   - e->user_data: Additional context
 * 
 * @note Event handlers run in Core 1 context (FreeRTOS task)
 * @note Must NOT call display_lock/unlock directly (handled transparently)
 * @see ui_logic.c for application state updates
 * @see screens.h for object definitions
 */

#ifndef EEZ_LVGL_UI_EVENTS_H
#define EEZ_LVGL_UI_EVENTS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* WiFi CONNECTION & AUTHENTICATION HANDLERS */

/**
 * @brief WiFi connection initiation handler
 * 
 * Triggered when user clicks "Connect" button in WiFi settings screen.
 * Extracts SSID and password from input fields, validates, and initiates WiFi connection.
 * 
 * Implementation:
 * - Reads SSID from objects.wifi_ssid_1 text area
 * - Reads Password from objects.wifi_password_1 text area
 * - Validates input lengths (SSID max 32 chars, PWD max 63 chars)
 * - Calls wifi_init_sta() with credentials
 * - Shows status feedback (overlay, toast, or status change)
 * - Transitions to Dashboard screen on success
 * 
 * @param e LVGL event structure (e->current_target = WiFi Connect button)
 * 
 * @note Credential validation occurs here before passing to esp_wifi API
 * @note Connection is asynchronous; actual connection handled by WiFi event loop
 * @see wifi_init_sta() in main.c for WiFi stack initialization
 * @see action_jumptowifiscreen() for reverse flow (back to dashboard)
 * 
 * Event Trigger: LV_EVENT_CLICKED (WiFi Connect button)
 */
extern void action_connect_wifi(lv_event_t * e);

/**
 * @brief Show on-screen keyboard handler
 * 
 * Displays numeric/alphabetic keyboard when user focuses a text input field.
 * Typically triggered on LV_EVENT_FOCUSED for SSID/password input boxes.
 * 
 * Keyboard Configuration:
 * - Mode: LVGL_KEYBOARD_MODE_TEXT_LOWER (lowercase) or NUMERIC
 * - Target: Text area that triggered the event
 * - Dismissal: Auto-hide when input loses focus
 * 
 * Implementation:
 * - Makes keyboard visible (lv_obj_clear_flag(objects.ui_keyboard_1, LV_OBJ_FLAG_HIDDEN))
 * - Sets keyboard target to input field (lv_keyboard_set_textarea)
 * - Optionally switches keyboard type (numeric vs alphabetic)
 * 
 * @param e LVGL event structure (e->current_target = input text area)
 * 
 * @note Keyboard automatically syncs to input field text
 * @note Multiple calls safe (idempotent - resets timeout)
 * @see action_password_ssid_keyboad() for mode selection handler
 * 
 * Event Trigger: LV_EVENT_FOCUSED (SSID or Password input field)
 */
extern void action_show_keyboard(lv_event_t * e);

/**
 * @brief Keyboard mode selector (SSID vs Password handlers)
 * 
 * Routes keyboard input to correct field and handles mode switching.
 * Called when keyboard transitions between SSID and Password input.
 * 
 * Logic:
 * - Determine which field has focus (SSID or Password)
 * - Switch keyboard mode if needed (text vs numeric/special)
 * - Set keyboard target to active field
 * - Auto-switch on Return/Enter key if complete
 * 
 * Implementation Details:
 * - SSID input: Lowercase letters, numbers, common special chars
 * - Password input: Full character set (special chars, caps, numbers)
 * - Return key: Advance to next field or trigger connect
 * 
 * @param e LVGL event structure (e->current_target = keyboard control)
 * 
 * @note Typo in function name: "keyboad" should be "keyboard" (EEZ generated, preserved for compatibility)
 * @see action_show_keyboard() for keyboard visibility
 * @see action_connect_wifi() for connection initiation
 * 
 * Event Trigger: LV_EVENT_VALUE_CHANGED or LV_EVENT_KEY press
 */
extern void action_password_ssid_keyboad(lv_event_t * e);

/**
 * @brief Navigate to WiFi Settings screen
 * 
 * Shows WiFi connection setup form (SSID/Password input fields + keyboard).
 * Triggered by clicking "WiFi" menu item or settings button from Dashboard.
 * 
 * Screen Transition:
 * Dashboard (SCREEN_ID_DASHBOARD)
 *     ↓ [user clicks WiFi button]
 *     ↓
 * WiFi Settings (SCREEN_ID_WIFISCREEN or wifi_page)
 * 
 * Implementation:
 * - Calls loadScreen(SCREEN_ID_WIFISCREEN) (if using EEZ enum)
 * - Clears any previous input (objects.wifi_ssid_1, objects.wifi_password_1)
 * - Shows keyboard (lv_obj_clear_flag(ui_keyboard_1, LV_OBJ_FLAG_HIDDEN))
 * - Focuses first input field (SSID)
 * 
 * Reverse Navigation:
 * - Back button on WiFi screen calls action_jumptodashboard() or similar
 * - Unsaved changes are discarded on back
 * 
 * @param e LVGL event structure (e->current_target = WiFi button)
 * 
 * @note Screen load is synchronous (locks mutex during transition)
 * @note Previous dashboard state preserved (persists when returning)
 * @see loadScreen() in ui.h for screen switching
 * @see action_connect_wifi() for WiFi connection from this screen
 * 
 * Event Trigger: LV_EVENT_CLICKED (WiFi menu/settings button)
 */
extern void action_jumptowifiscreen(lv_event_t * e);

///////////////////////////////////////////////////////////////////////////////
// ADDITIONAL EVENT HANDLERS (if defined in EEZ Studio design)
///////////////////////////////////////////////////////////////////////////////
// 
// Common patterns for other potential handlers:
// - action_format_sd_card1() - Format internal SD card (user confirmation)
// - action_format_sd_card2() - Format external SD card
// - action_test_sd_mount() - Manual SD card mount test
// - action_show_file_manager() - Browse SD card contents
// - action_disconnect_wifi() - Stop WiFi connection
// - action_reset_settings() - Factory reset
//
// These would follow the same documentation pattern as above.

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_EVENTS_H*/