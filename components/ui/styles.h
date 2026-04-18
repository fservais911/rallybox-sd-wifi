/**
 * @file styles.h
 * @brief Rallybox LVGL Styling & Theming Framework
 * @author EEZ Studio Generator + Rallybox Team
 * @developer Akhil
 * 
 * Defines styling system for Rallybox UI components using Material Design dark theme.
 * Styles are created programmatically in screens.c and applied to LVGL objects.
 * 
 * Theming Architecture:
 * ┌─────────────────────────────────────────────────┐
 * │  Material Design Dark Color Palette             │
 * │  (8 colors + backgrounds)                       │
 * └──────────────┬──────────────────────────────────┘
 *                │
 *                ├─ Base Colors (predefined in styles.c)
 *                │  - Primary: 0xFF1F77C3 (blue)
 *                │  - Surface: 0xFF121212 (dark gray)
 *                │  - Error: 0xFFCF6679 (red)
 *                │  - Success: 0xFF4CAF50 (green)
 *                │
 *                ├─ Text Colors
 *                │  - Primary: 0xFFFFFFFF (white, 100%)
 *                │  - Secondary: 0xFF99FFFFFF (white, 60%)
 *                │  - Disabled: 0xFF38FFFFFF (white, 22%)
 *                │
 *                ├─ Component Styles (applied per widget)
 *                │  - Buttons: Corner radius, padding, states
 *                │  - Labels: Font, text color, alignment
 *                │  - Progress bars: Track/indicator colors
 *                │  - Images: Opacity, recoloring (icons)
 *                │
 *                └─ State Variants
 *                   - Normal/Default state
 *                   - Pressed/Selected state (brightness +10%)
 *                   - Disabled state (opacity 40%)
 *                   - Focus state (outline highlight)
 * 
 * Color Palette Reference:
 * @code
 * #define COLOR_PRIMARY      0xFF1F77C3  // Primary UI accent (blue)
 * #define COLOR_SECONDARY    0xFF6BAC3E  // Secondary accent (green)
 * #define COLOR_SURFACE      0xFF121212  // Background (dark gray)
 * #define COLOR_ERROR        0xFFCF6679  // Error/warning (red)
 * #define COLOR_SUCCESS      0xFF4CAF50  // Success/OK status (green)
 * #define COLOR_TEXT_PRIMARY 0xFFFFFFFF  // Main text (white)
 * #define COLOR_TEXT_MUTED   0xFF99FFFFFF  // Secondary text (60% white)
 * #define COLOR_BORDER       0xFF323232  // Divider/border (dark white)
 * @endcode
 * 
 * Font Hierarchy:
 * - Display Large: 30pt Montserrat (titles, headers)
 * - Display: 24pt Montserrat (screen headings)
 * - Body Large: 16pt Montserrat (main content)
 * - Body: 14pt Montserrat (labels, descriptions)
 * - Label: 12pt Montserrat (fine print, secondary info)
 * 
 * Component Styling Patterns:
 * 
 * 1. Buttons:
 * @code
 * lv_obj_t *btn = lv_btn_create(parent);
 * lv_obj_set_size(btn, 200, 50);
 * lv_obj_set_style_bg_color(btn, COLOR_PRIMARY, LV_PART_MAIN);
 * lv_obj_set_style_bg_opa(btn, LV_OPA_100, LV_PART_MAIN);
 * lv_obj_set_style_border_opa(btn, LV_OPA_0, LV_PART_MAIN);  // No border
 * lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);  // Rounded corners
 * 
 * // Press effect: Brightness change
 * lv_obj_set_style_bg_color(btn, lv_color_brighten(COLOR_PRIMARY, 10), LV_PART_MAIN | LV_STATE_PRESSED);
 * @endcode
 * 
 * 2. Labels:
 * @code
 * lv_obj_t *label = lv_label_create(parent);
 * lv_label_set_text(label, "System Status");
 * lv_obj_set_style_text_color(label, COLOR_TEXT_PRIMARY, LV_PART_MAIN);
 * lv_obj_set_style_text_font(label, &lv_font_montserrat_24, LV_PART_MAIN);
 * @endcode
 * 
 * 3. Progress Bars (SD Card Usage):
 * @code
 * lv_obj_t *bar = lv_bar_create(parent);
 * lv_obj_set_size(bar, 200, 8);
 * lv_obj_set_style_bg_color(bar, COLOR_SURFACE, LV_PART_MAIN);  // Track
 * lv_obj_set_style_bg_color(bar, COLOR_PRIMARY, LV_PART_INDICATOR);  // Indicator
 * lv_bar_set_value(bar, 75, LV_ANIM_OFF);  // 75% full
 * @endcode
 * 
 * 4. Icons (WiFi, SD card):
 * @code
 * lv_obj_t *icon = lv_img_create(parent);
 * lv_img_set_src(icon, &ui_image_wifi_icon);
 * lv_obj_set_style_img_recolor(icon, COLOR_PRIMARY, LV_PART_MAIN);  // Recolor to primary
 * lv_obj_set_style_img_recolor_opa(icon, LV_OPA_100, LV_PART_MAIN);
 * @endcode
 * 
 * Theme Extension:
 * To add new color or modify existing styles:
 * 1. Update color constants at top of styles.c
 * 2. Modify component creation in screens.c (apply styles immediately)
 * 3. Test in both BootingScreen and Dashboard contexts
 * 4. Ensure sufficient color contrast for dark theme (WCAG AAA minimum 7:1)
 * 
 * @note Styles are primarily applied in screens.c during object creation
 * @note No separate style objects defined (using direct property application)
 * @note All colors use 0xFFRRGGBB format (ARGB8888 with full opacity)
 * @see screens.c for actual style application
 * @see ui_logic.c for dynamic style updates (e.g., error state)
 */

#ifndef EEZ_LVGL_UI_STYLES_H
#define EEZ_LVGL_UI_STYLES_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* COLOR PALETTE - Material Design Dark Theme */

/// Primary brand color (blue accent for interactive elements)
#define COLOR_PRIMARY      0xFF1F77C3

/// Secondary accent color (green for success/positive states)
#define COLOR_SECONDARY    0xFF6BAC3E

/// Surface/background color (dark gray)
#define COLOR_SURFACE      0xFF121212

/// Error/warning state color (red)
#define COLOR_ERROR        0xFFCF6679

/// Success/OK indicator color (green)
#define COLOR_SUCCESS      0xFF4CAF50

/// Primary text color (white for readability)
#define COLOR_TEXT_PRIMARY 0xFFFFFFFF

/// Secondary/muted text color (white at 60% opacity)
#define COLOR_TEXT_MUTED   0xFF99FFFFFF

/// Border/divider color (light gray)
#define COLOR_BORDER       0xFF323232

/// WiFi signal strength color (blue)
#define COLOR_WIFI_SIGNAL  0xFF2196F3

/// SD card status OK color (green)
#define COLOR_SD_OK        0xFF4CAF50

/// SD card status error color (red)
#define COLOR_SD_ERROR     0xFFE53935

/// CPU load indicator color (orange)
#define COLOR_CPU_LOAD     0xFFFFA726

/* STYLE CREATION & APPLICATION FUNCTIONS */

/**
 * @brief Initialize all styles for the UI
 * 
 * Called during ui_init() to set up base styles and color definitions.
 * Creates style objects and registers them for reuse across screens.
 * 
 * Currently implemented through direct property application in screens.c,
 * but this function declaration allows for future migration to
 * centralized style object management.
 */
void init_styles(void);

/**
 * @brief Apply button style to a widget
 * 
 * Convenience function to apply standard button styling:
 * - Corner radius: 8px
 * - Background: colored (default primary)
 * - Text: white, Montserrat 14pt
 * - Press effect: brightness +10%
 * 
 * @param btn Button widget to style
 * @param color Button background color (use COLOR_PRIMARY, COLOR_ERROR, etc)
 * 
 * @example
 * lv_obj_t *submit_btn = lv_btn_create(parent);
 * apply_button_style(submit_btn, COLOR_PRIMARY);
 */
void apply_button_style(lv_obj_t *btn, lv_color_t color);

/**
 * @brief Apply label/text style to a widget
 * 
 * Convenience function to apply standard text styling:
 * - Color: primary text (white)
 * - Font: Montserrat 14pt
 * - Alignment: left aligned
 * - Text padding: standard margins
 * 
 * @param label Label or text object to style
 * @param font_size Font size enum (SMALL, MEDIUM, LARGE)
 * 
 * @note Font size parameter allows quick scaling without separate calls
 */
void apply_text_style(lv_obj_t *label, uint8_t font_size);

/**
 * @brief Apply progress bar style to a widget
 * 
 * Convenience function for status indicators (SD card usage, CPU load):
 * - Track color: surface (dark gray)
 * - Indicator color: blue (primary)
 * - Height: 8px
 * - Corner radius: 4px
 * 
 * @param bar Progress bar widget to style
 * @param indicator_color Color for filled portion (COLOR_PRIMARY, COLOR_ERROR, etc)
 */
void apply_progress_style(lv_obj_t *bar, lv_color_t indicator_color);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_STYLES_H*/