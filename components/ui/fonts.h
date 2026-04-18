/**
 * @file fonts.h
 * @brief Rallybox Font Definitions & Typography System
 * @author EEZ Studio Generator + Rallybox Team
 * @developer Akhil
 * 
 * Font Management:
 * All fonts are embedded as PNG-based bitmap fonts using Montserrat typeface.
 * Fonts are baked into the firmware binary at compile time (no runtime loading).
 * 
 * Supported Font Sizes:
 * - 12pt: Labels, captions, secondary text (fine print)
 * - 14pt: Body text, list items, small content
 * - 16pt: Body large, labels, descriptions
 * - 20pt: Subheadings, section titles
 * - 24pt: Screen titles, important headers
 * - 30pt: Main titles, splash screen (Booting screen)
 * 
 * Font Access:
 * Fonts are declared as extern const lv_font_t objects and managed by LVGL.
 * Direct access: lv_font_montserrat_12, lv_font_montserrat_14, etc.
 * Font table lookup via ext_font_desc_t fonts[] array for runtime selection.
 * 
 * Font Hierarchy for Rallybox UI:
 * - 30pt: Booting Screen Title (Rally Box splash)
 * - 24pt: Dashboard Screen Title and section headers
 * - 20pt: Section header subtitles
 * - 16pt: Body large, status labels
 * - 14pt: Body text, list items, standard content
 * - 12pt: Captions, fine print, secondary info
 * 
 * Memory Footprint:
 * Each font size approximately 100-150 KB (bitmap data in flash).
 * Total for all 6 sizes: approximately 800 KB in flash.
 * Runtime memory: Font pointers only (about 100 bytes).
 * 
 * Typography Best Practices:
 * - Use no more than 3 different sizes per screen
 * - Maintain WCAG AAA contrast ratio of 7:1 for body text
 * - Left-align main content, center-align titles, right-align numbers
 * - Pre-load all fonts at ui_init() time
 * 
 * @note All fonts are const and stored in flash (read-only)
 * @note Font rendering is non-blocking (pre-computed glyphs)
 * @see styles.h for font application in styling context
 */

#ifndef EEZ_LVGL_UI_FONTS_H
#define EEZ_LVGL_UI_FONTS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Extended font descriptor structure
 * 
 * Allows runtime font lookup by name (useful for themes/settings).
 * Each entry maps a font name string to its lv_font_t pointer.
 * 
 * @member name Font name string (e.g., "montserrat_16", NULL for list terminator)
 * @member font_ptr Pointer to lv_font_t structure in flash
 * 
 * @note Table is terminated by entry with name == NULL
 * @see fonts[] array for actual font definitions
 */
#ifndef EXT_FONT_DESC_T
#define EXT_FONT_DESC_T
typedef struct _ext_font_desc_t {
    const char *name;           ///< Font identifier string
    const void *font_ptr;       ///< Pointer to lv_font_t in flash
} ext_font_desc_t;
#endif

/// Font array for runtime lookup (name to lv_font_t mapping)
extern ext_font_desc_t fonts[];

/// Get font by size (convenience function)
/// @param size_pt Requested size in points (12, 14, 16, 20, 24, 30)
/// @return Pointer to lv_font_t, or NULL if size not available
lv_font_t *get_font_by_size(uint8_t size_pt);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_FONTS_H*/