# Rallybox UI Component Documentation

**Component Name**: UI (EEZ Studio Generated)  
**Author**: Rallybox Design Team  
**Developer**: Akhil  
**Framework**: LVGL v9.4 (Light and Versatile Graphics Library)  
**Generator**: EEZ Studio

---

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [File Structure](#file-structure)
4. [Screen Definitions](#screen-definitions)
5. [Object Hierarchy](#object-hierarchy)
6. [Event Handling](#event-handling)
7. [Styles & Theming](#styles--theming)
8. [Image & Font Resources](#image--font-resources)
9. [Integration with Main App](#integration-with-main-app)
10. [Maintenance & Extension](#maintenance--extension)

---

## Overview

### What is EEZ Studio?

**EEZ Studio** is a professional LVGL UI design tool that allows developers to:
- Visually design embedded UI (drag-and-drop components)
- Define screen layouts, colors, fonts, and animations
- Generate production-ready C code from the design
- Manage assets (images, fonts, themes)
- Define event handlers and actions

### This Component

This UI component is **automatically generated** from an EEZ Studio project file (`.eez-project`). It provides:

- **Booting Screen**: Professional 10-second splash screen with spinner animation
- **Dashboard Screen**: Real-time system monitoring with WiFi, SD card, and system stats
- **LVGL Integration**: Seamless integration with LVGL v9.4 graphics library
- **Event System**: Pre-defined event handlers for user interactions
- **Resource Management**: Embedded fonts, images, and color themes

### Design Philosophy

- **Professional UX**: Modern Material Design color scheme
- **Performance Optimization**: Minimal redraws, efficient memory usage
- **Responsive Layout**: Adapts to 720×1280 display
- **Accessibility**: High contrast text, readable fonts
- **Maintainability**: Clear separation between UI code and application logic

---

## Architecture

### High-Level System

```
┌─────────────────────────────────────────┐
│      Application Layer (main.c)         │
│  - System monitoring                    │
│  - Event orchestration                  │
│  - WiFi management                      │
└──────────────┬──────────────────────────┘
               │ calls ui_logic_update_*()
        ┌──────▼──────────────────┐
        │   UI Logic (ui_logic.c) │
        │  - Dashboard updates    │
        │  - File manager         │
        │  - Event callbacks      │
        └──────┬──────────────────┘
               │ calls screen API
        ┌──────▼─────────────────────────┐
        │  UI Component (this directory) │
        │  - Screen definitions          │
        │  - Object hierarchy            │
        │  - Styles & themes             │
        │  - Event handlers              │
        └──────┬─────────────────────────┘
               │ renders to
        ┌──────▼──────────────────┐
        │  LVGL Library v9.4      │
        │  - Graphics rendering   │
        │  - Input handling       │
        └─────────────────────────┘
```

### Initialization & Update Flow

```c
// Application startup (main.c)
app_main()
├─ bsp_display_start_with_config()              // Initialize LCD
├─ ui_init()                ◄─────────────────── // Load UI from this component
│  ├─ create_screen_bootingscreen()
│  ├─ create_screen_dashboard()
│  └─ loadScreen(SCREEN_ID_BOOTINGSCREEN)       // Show boot screen
├─ wifi_init_sta()                               // WiFi setup
├─ xTaskCreate(system_monitor_task, ...)         // Background monitoring
│  └─ system_monitor_task()
│     ├─ [wait 10 seconds with boot screen]
│     ├─ loadScreen(SCREEN_ID_DASHBOARD)         // Switch to dashboard
│     ├─ [continuous 1 Hz monitoring loop]
│     │  └─ bsp_display_lock()
│     │     └─ ui_logic_update_wifi_live()       // Update display
│     │     └─ ui_logic_set_uptime()
│     │     └─ ui_logic_update_storage_info()
│     │     └─ bsp_display_unlock()
│     └─ [forever]

// Main rendering loop (runs on Core 0, managed by LVGL)
while(1)
{
    lv_timer_handler()                          // Run LVGL timers & redraw
    vTaskDelay(1 / portTICK_PERIOD_MS)
}
```

---

## File Structure

### Generated Files (Do Not Edit Manually)

These files are **automatically generated** by EEZ Studio. Manual edits will be **overwritten** on regeneration.

```
components/ui/
├── ui.c                 # Core UI initialization and tick logic
├── ui.h                 # Public UI API (ui_init, ui_tick, loadScreen)
├── screens.c            # Screen creation functions implementation
├── screens.h            # Screen definitions and object pointers
├── styles.c             # Style definitions and theme management
├── styles.h             # Style API
├── actions.c            # Event action implementations (if implemented)
├── actions.h            # Event action declarations
├── images.c             # Embedded image data (binary)
├── images.h             # Image resource declarations
├── ui_image_sdcard.c    # SD card icon image data
├── ui_image_wifi_icon.c # WiFi icon image data
├── fonts.h              # Font resource declarations
├── vars.h               # Variable/constant definitions
├── structs.h            # Data structure definitions
└── .eez-project         # EEZ Studio project file (design source)
```

### Key Files Explained

#### `ui.h` (Public API)
```c
#ifndef EEZ_LVGL_UI_GUI_H
#define EEZ_LVGL_UI_GUI_H

void ui_init();           // Initialize all screens and objects
void ui_tick();           // Call periodically for animations/updates
void loadScreen(enum ScreensEnum screenId);  // Switch between screens

enum ScreensEnum {
    SCREEN_ID_BOOTINGSCREEN = 1,
    SCREEN_ID_DASHBOARD = 2,
};
```

#### `screens.h` (Object Pointers)
```c
typedef struct _objects_t {
    // Screens
    lv_obj_t *bootingscreen;
    lv_obj_t *dashboard;
    
    // Boot screen objects
    lv_obj_t *booting_title;
    lv_obj_t *booting_label;
    lv_obj_t *booting_spinner;
    
    // Dashboard objects
    lv_obj_t *dashboard_title_1;
    lv_obj_t *dashboard_uptime_1;
    lv_obj_t *wifi_connection;
    lv_obj_t *system_load_status_1;
    // ... more dashboard widgets
} objects_t;

extern objects_t objects;  // Global object reference
```

#### `CMakeLists.txt` (Build Configuration)
```cmake
idf_component_register(
    SRCS "ui.c" "screens.c" "styles.c" "images.c" 
         "ui_image_sdcard.c" "ui_image_wifi_icon.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl__lvgl  # Dependency on LVGL library
)
```

---

## Screen Definitions

### Screen 1: Booting Screen

**Purpose**: Professional splash screen shown during first 10 seconds of boot

**Resolution**: 720×1280 pixels  
**Duration**: 10 seconds (controlled by main.c system_monitor_task)  
**Auto-transition**: Yes → Dashboard

#### Components

| Component | Type | Purpose |
|-----------|------|---------|
| `booting_title` | Label | "Rally Box" (Montserrat 30pt, white) |
| `booting_label` | Label | "Booting" (Montserrat 16pt, white) |
| `booting_spinner` | Spinner | Animated rotating indicator (20×26px) |
| Background | Rectangle | Solid black (0xFF000000, prevents flash) |

#### Layout (Center-Aligned)

```
┌──────────────────────────────────────────┐
│                                          │
│              Rally Box                   │
│             (Montserrat 30)              │
│                                          │
│               Booting ⟳                  │
│            (Montserrat 16)               │
│                                          │
│                                          │
└──────────────────────────────────────────┘
720 px wide × 1280 px tall
```

#### Code Structure

```c
void create_screen_bootingscreen()
{
    lv_obj_t *obj = lv_obj_create(0);
    objects.bootingscreen = obj;
    
    // Set properties
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 1280, 720);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff000000), 0);
    
    // Create child objects (title, label, spinner)
    // ... (generated by EEZ Studio)
}
```

#### Status in Boot Sequence

```
T+0s       bootingscreen active, spinner animating
T+0.5s     display visible, text centered
T+10s      [transition triggered by system_monitor_task]
T+10.1s    loadScreen(SCREEN_ID_DASHBOARD) called
T+10.1s+   dashboard appears, boot sequence complete
```

### Screen 2: Dashboard Screen

**Purpose**: Real-time system monitoring and control interface

**Resolution**: 720×1280 pixels  
**Refresh Rate**: 1 Hz (metrics update)  
**Content Areas**: WiFi, SD cards, CPU/Heap, Uptime

#### Components

##### Top System Status Bar
| Component | Type | Purpose |
|-----------|------|---------|
| `system_status_top_bar_title_1` | Label | Section header "System Status" |
| `dashboard_uptime_1` | Label | Uptime "HH:MM:SS" (updated 1 Hz) |
| `system_load_status_1` | Label | "CPU: X% \| Heap: Y KB" |
| `error_status_1` | Label | Error count or OK status |

##### WiFi Section
| Component | Type | Purpose |
|-----------|------|---------|
| `dashbaord_wifi_icon_1` | Image | WiFi signal icon |
| `wifi_connection` | Label | "Connected \| RSSI: -XX dBm" |
| `wifi_icon_wifi_screen_1` | Image | WiFi logo icon |

##### SD Card Monitoring (2 Slots)

**SD Card 1 (SDMMC - Internal)**
| Component | Type | Purpose |
|-----------|------|---------|
| `sd_card_icon_2_2` | Image | SD card icon |
| `sdcard_label_1_1` | Label | Card status "OK/FAIL" |
| `read_write_label_1_1` | Label | "R:X W:Y" (operation counts) |
| `active_checkbox_sd_card_1_1` | Checkbox | Mount status indicator |
| `sdcard_storage_1_1` | Progress Bar | Capacity usage visualization |

**SD Card 2 (SDSPI - External)**
| Component | Type | Purpose |
|-----------|------|---------|
| `sd_card_icon_2_3` | Image | SD card icon |
| `sdcard_label_2_1` | Label | Card status "OK/FAIL" |
| `read_write_label_2_1` | Label | "R:X W:Y" (operation counts) |
| `active_checkbox_sd_card_2_1` | Checkbox | Mount status indicator |

##### Control Buttons

| Button | Function | Action |
|--------|----------|--------|
| `test_sd_card1_mount` | SD1 Test | Trigger read/write test on SD1 |
| `test_sd_card1_mount_2` | SD2 Test | Trigger read/write test on SD2 |
| `format_sd_card1` | Format SD1 | Quick-format SD1 (with confirmation) |
| `format_sd_card2` | Format SD2 | Quick-format SD2 (with confirmation) |

##### Dashboard Layout (Conceptual)

```
┌─────────────────────────────────────────────┐
│  🏠 Rallybox Dashboard                      │
├─────────────────────────────────────────────┤
│ System Status         │ Uptime: 00:10:30   │
│ CPU: 25% | Heap: 256K│ Errors: 0          │
├─────────────────────────────────────────────┤
│ 📶 WiFi: Connected | RSSI: -45 dBm        │
│    IP: 192.168.1.100                       │
├─────────────────────────────────────────────┤
│ SD Card 1 (Internal)    │ SD Card 2 (Ext)  │
│ ✓ OK                    │ ✓ OK              │
│ R: 42 W: 15             │ R: 8  W: 3        │
│ [████████░░] 80% Used   │ [██░░░░] 20% Used │
├─────────────────────────────────────────────┤
│ [Test SD1] [Format SD1] │ [Test SD2] [Fmt] │
├─────────────────────────────────────────────┤
│ 🔧 System              📁 Files            │
│ 🛜 WiFi Settings       📊 Logs             │
└─────────────────────────────────────────────┘
```

#### Update Mechanism

```c
// Called periodically from ui_logic.c (via system_monitor_task)
void ui_logic_set_uptime(uint32_t seconds)
{
    // Format "HH:MM:SS"
    lv_label_set_text_fmt(objects.dashboard_uptime_1, "%02u:%02u:%02u", 
                          h, m, s);
}

// Update WiFi status
void ui_logic_update_wifi_live(const system_status_t *status)
{
    if (status->wifi_connected)
        lv_label_set_text_fmt(objects.wifi_connection, 
                              "Connected | RSSI: %d dBm", status->wifi_rssi);
    else
        lv_label_set_text(objects.wifi_connection, "Disconnected");
}

// Update SD card info
void ui_logic_update_storage_info(int slot, bool detected, 
                                   uint64_t capacity_mb, uint64_t used_mb)
{
    if (slot == 1) {
        uint8_t percent = (used_mb * 100) / capacity_mb;
        lv_bar_set_value(objects.sdcard_storage_1_1, percent, LV_ANIM_ON);
        lv_label_set_text_fmt(objects.sdcard_label_1_1, "%s", 
                              detected ? "OK" : "FAIL");
    }
    // Similar for slot 2
}
```

---

## Object Hierarchy

### Global Object Structure

All UI objects are accessible via the global `objects` pointer (defined in `screens.h`):

```c
extern objects_t objects;

// Usage examples:
objects.bootingscreen          // Booting screen root object
objects.dashboard              // Dashboard screen root object
objects.dashboard_title_1      // Dashboard title label
objects.wifi_connection        // WiFi status label
objects.sdcard_storage_1_1     // SD1 progress bar
```

### Object Types (LVGL)

| Type | Purpose | Examples |
|------|---------|----------|
| `lv_obj_t` | Container/Base | Screen root, panels |
| Label | Text display | Title, status messages |
| Image | Icon/Picture | WiFi icon, SD card icon |
| Progress Bar | Visual indicator | Storage usage |
| Checkbox | Toggle indicator | Mount status |
| Spinner | Animation | Boot spinner |
| Textarea | Text input | (future use) |
| Button | Interactive | SD test, format, settings |

### Object Naming Convention

```
{location}_{component_type}_{description}_{instance}

Examples:
- dashboard_uptime_1        → Dashboard section, uptime display, instance 1
- wifi_connection           → WiFi section, connection status
- sdcard_storage_1_1        → SD card section, storage progress, slot 1, instance 1
- read_write_label_2_1      → Read/write counter, slot 2, instance 1
- booting_title             → Boot screen, title text
```

---

## Event Handling

### Event Architecture

All events flow through LVGL's event system:

```
User Interaction (touch, button press)
    ↓
LCD Touch Driver (GT911)
    ↓
LVGL Input Device
    ↓
Object Event Handler
    ↓
Action Function (defined in actions.h)
    ↓
Application Logic (ui_logic.c or main.c)
```

### Defined Actions

#### `action_connect_wifi(lv_event_t *e)`
**Trigger**: WiFi Connect button pressed  
**Purpose**: Validate SSID/password and initiate WiFi connection  
**Implementation**: Calls `system_wifi_connect_credentials(ssid, pass)`

```c
// In ui_logic.c (ui_logic_update_*() functions)
void action_connect_wifi(lv_event_t *e)
{
    const char *ssid = lv_textarea_get_text(objects.wifi_ssid_1);
    const char *pass = lv_textarea_get_text(objects.wifi_password_1);
    
    if (strlen(ssid) == 0) {
        ESP_LOGW(TAG, "SSID cannot be empty");
        return;
    }
    
    system_wifi_connect_credentials(ssid, pass);
    ESP_LOGI(TAG, "WiFi connect initiated for SSID: %s", ssid);
}
```

#### `action_show_keyboard(lv_event_t *e)`
**Trigger**: SSID/Password text field clicked  
**Purpose**: Display on-screen keyboard for text input  
**Implementation**: Shows virtual keyboard (EEZ Studio component)

#### `action_password_ssid_keyboad(lv_event_t *e)`
**Trigger**: Keyboard key pressed  
**Purpose**: Update text field with keyboard input  
**Implementation**: Delegates to keyboard event handler

#### `action_jumptowifiscreen(lv_event_t *e)`
**Trigger**: WiFi menu button pressed  
**Purpose**: Navigate to WiFi settings screen  
**Implementation**: `loadScreen(SCREEN_ID_WIFI)` (if implemented)

### Event Binding Example

```c
// In screens.c (during screen creation)
void create_screen_dashboard()
{
    // ... object creation ...
    
    // Attach click handler to WiFi settings button
    lv_obj_add_event_cb(
        objects.wifi_conenct_button_1,
        action_connect_wifi,
        LV_EVENT_CLICKED,
        NULL
    );
    
    // Attach to WiFi screen button (navigation)
    lv_obj_add_event_cb(
        objects.wifi_icon_wifi_screen_1,
        action_jumptowifiscreen,
        LV_EVENT_CLICKED,
        NULL
    );
}
```

---

## Styles & Theming

### LVGL Style System

Styles in LVGL are applied using `lv_obj_set_style_*()` functions:

```c
// Set text color
lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), 0);

// Set background
lv_obj_set_style_bg_color(obj, lv_color_hex(0xff0f1419), 0);
lv_obj_set_style_bg_opa(obj, 255, 0);

// Set font
lv_obj_set_style_text_font(obj, &lv_font_montserrat_16, 0);
```

### Color Scheme

The design uses a **Material Design Dark** theme:

| Element | Color | Hex Code | Usage |
|---------|-------|----------|-------|
| Background | Very Dark Blue-Grey | 0xFF0F1419 | Screen bg, surfaces |
| Surface | Dark Grey | 0xFF182028 | Panels, containers |
| Text Primary | Light Grey-White | 0xFFF2F4F5 | Headings, labels |
| Text Secondary | Muted Grey | 0xFF9FADB8 | Secondary info |
| Accent | Teal | 0xFF6BA6A8 | Highlights, active |
| Success | Green | 0xFF77B255 | Status OK, online |
| Warning | Amber | 0xFFD4A14A | Warnings, caution |
| Danger | Red | 0xFFD77474 | Errors, failures |

### Font Hierarchy

| Usage | Font | Size | Example |
|-------|------|------|---------|
| Screen Title | Montserrat Bold | 24-30 pt | "Rally Box", "Dashboard" |
| Section Header | Montserrat SemiBold | 18-20 pt | "System Status", "WiFi" |
| Regular Text | Montserrat Regular | 14-16 pt | Labels, values |
| Small Text | Montserrat Regular | 12 pt | Hints, secondary |

### Border & Radius

```c
// Modern rounded corners
lv_obj_set_style_radius(obj, 12, 0);  // 12px roundness

// Subtle borders
lv_obj_set_style_border_width(obj, 1, 0);
lv_obj_set_style_border_color(obj, lv_color_hex(0xFF31404C), 0);

// Soft shadow (Material Design elevation)
lv_obj_set_style_shadow_width(obj, 3, 0);
lv_obj_set_style_shadow_color(obj, lv_color_hex(0xFF000000), 0);
```

---

## Image & Font Resources

### Embedded Images

Images are compiled into the binary as C arrays for fast loading (no file I/O):

```c
// ui_image_sdcard.c
const uint8_t sdcard_image_data[] = {
    0x... // Image data in RGB565 or LZ4 compressed format
};

// ui_image_wifi_icon.c
const uint8_t wifi_icon_data[] = {
    0x... // WiFi icon bitmap
};
```

### Font Resources

Fonts are defined in `fonts.h` and managed by LVGL:

```c
#include "lvgl.h"

// Predefined fonts (from LVGL)
extern lv_font_t lv_font_montserrat_12;
extern lv_font_t lv_font_montserrat_14;
extern lv_font_t lv_font_montserrat_16;
extern lv_font_t lv_font_montserrat_18;
extern lv_font_t lv_font_montserrat_20;
extern lv_font_t lv_font_montserrat_30;
```

### Asset Management

All assets (images, fonts) are embedded in the executable:
- **No file system needed** for UI fonts/icons
- **Fast loading** (no SD card I/O)
- **Predictable memory** (fixed size at compile time)

---

## Integration with Main App

### Initialization Sequence

```c
// In main.c (app_main)

// Step 1: Initialize display hardware
bsp_display_start_with_config(&display_cfg);

// Step 2: Initialize LVGL and load UI from this component
ui_init();

// Step 3: Show boot screen
loadScreen(SCREEN_ID_BOOTINGSCREEN);

// Step 4: In background task (after 10 seconds)
loadScreen(SCREEN_ID_DASHBOARD);
```

### Update Flow

```c
// In system_monitor_task (every 250ms with 4x divider = 1 Hz)

if (bsp_display_lock((uint32_t)-1) == ESP_OK) {
    system_status_t *status = system_monitor_get_status();
    
    // Call UI update functions (from ui_logic.c)
    ui_logic_set_uptime(status->uptime_seconds);
    ui_logic_update_wifi_live(status);
    ui_logic_update_storage_info(1, status->sdcard1_initialized, cap1, used1);
    ui_logic_update_storage_info(2, status->sdcard2_initialized, cap2, used2);
    ui_logic_update_system_stats(status->cpu_load_percent, total_errors);
    
    bsp_display_unlock();
}
```

### Screen Switching

```c
// Navigate between screens
void loadScreen(enum ScreensEnum screenId)
{
    switch (screenId) {
        case SCREEN_ID_BOOTINGSCREEN:
            lv_scr_load_anim(objects.bootingscreen, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
            break;
        case SCREEN_ID_DASHBOARD:
            lv_scr_load_anim(objects.dashboard, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, true);
            break;
    }
}
```

---

## Maintenance & Extension

### Editing the UI Design

#### Method 1: Using EEZ Studio (Recommended)

1. **Install EEZ Studio**
   ```bash
   # Download from https://www.envox.hr/eez/index.html
   # or https://github.com/eez-open/studio
   ```

2. **Open Project File**
   ```bash
   cd components/ui
   open .eez-project  # Opens in EEZ Studio
   ```

3. **Make Changes**
   - Drag/drop components
   - Change colors, fonts, sizes
   - Define new screens
   - Add event handlers

4. **Export/Generate**
   - In EEZ Studio: Project → Export or Generate
   - Select target: LVGL C code
   - Overwrites all generated files

5. **Rebuild**
   ```bash
   cd <project-root>
   idf.py build
   ```

#### Method 2: Manual Code Changes (For Small Tweaks)

**⚠️ WARNING**: Only edit application-layer files (`ui_logic.c`). Do NOT edit generated files.

```c
// ✅ OK to edit in ui_logic.c
void ui_logic_update_wifi_live(const system_status_t *status)
{
    // Add custom logic here
    if (status->wifi_connected) {
        // Custom styling
        lv_obj_set_style_bg_color(objects.wifi_connection, 
                                   lv_color_hex(0xFF77B255), 0);
    }
}

// ❌ DO NOT edit in screens.c (will be overwritten)
// (changes here are lost when regenerating from EEZ Studio)
```

### Adding New Screens

1. **In EEZ Studio**: Create new screen in visual editor
2. **Generate**: Export the updated code
3. **Update enum** in `screens.h`:
   ```c
   enum ScreensEnum {
       SCREEN_ID_BOOTINGSCREEN = 1,
       SCREEN_ID_DASHBOARD = 2,
       SCREEN_ID_SETTINGS = 3,  // New screen
   };
   ```
4. **Update struct** in `screens.h`:
   ```c
   typedef struct _objects_t {
       lv_obj_t *settings_screen;  // New screen pointer
       // ... more objects ...
   } objects_t;
   ```
5. **Call new screen** from application:
   ```c
   loadScreen(SCREEN_ID_SETTINGS);
   ```

### Adding New Event Handlers

1. **In EEZ Studio**: Assign event to button/widget
2. **Declare in actions.h**:
   ```c
   extern void action_my_custom_action(lv_event_t *e);
   ```
3. **Implement in ui_logic.c**:
   ```c
   void action_my_custom_action(lv_event_t *e)
   {
       ESP_LOGI(TAG, "Custom action triggered");
       // Your logic here
   }
   ```

### Common Customizations

#### Change Boot Screen Duration
Edit `main.c` → `system_monitor_task()`:
```c
// Change from 10000ms to desired duration
const TickType_t boot_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(15000);
```

#### Update Dashboard Refresh Rate
Edit `main.c` → `system_monitor_task()`:
```c
// Change from 250ms (4 Hz) to different rate
const TickType_t xFrequency = pdMS_TO_TICKS(200);  // 5 Hz instead
```

#### Customize Colors
Edit **screens.c** (in generated code) or via EEZ Studio:
```c
// Example: Change accent color
lv_obj_set_style_bg_color(obj, lv_color_hex(0xFF00FF00), 0);  // Bright green
```

#### Add Animation to Widget
```c
// Fade in effect on dashboard load
lv_scr_load_anim(objects.dashboard, 
                 LV_SCR_LOAD_ANIM_FADE_IN,  // Animation type
                 300,                        // Duration (ms)
                 0,                          // Delay (ms)
                 true);                      // Automatically delete old screen
```

### Debugging Tips

#### View Object Tree
```c
// Add logging to see object hierarchy
void debug_print_objects(lv_obj_t *obj, int depth)
{
    for (int i = 0; i < depth; i++) printf("  ");
    printf("Object: %p\n", obj);
    
    // Iterate children
    uint32_t child_cnt = lv_obj_get_child_cnt(obj);
    for (int i = 0; i < child_cnt; i++) {
        debug_print_objects(lv_obj_get_child(obj, i), depth + 1);
    }
}
```

#### Monitor Object State
```c
// Check if object exists and has valid pointer
if (objects.wifi_connection != NULL) {
    uint32_t children = lv_obj_get_child_cnt(objects.wifi_connection);
    ESP_LOGI(TAG, "WiFi object has %u children", children);
}
```

#### Verify Styles Applied
```c
// Print object styling info
lv_color_t bg_color = lv_obj_get_style_bg_color(obj, 0);
uint8_t bg_opa = lv_obj_get_style_bg_opa(obj, 0);
ESP_LOGI(TAG, "BG Color: 0x%06X, Opacity: %u", bg_color.full, bg_opa);
```

---

## Performance Considerations

### Memory Usage

| Component | Size | Notes |
|-----------|------|-------|
| Objects | ~200-300 KB | All screen objects in SRAM |
| Images | ~50-100 KB | Embedded images |
| Fonts | ~30-50 KB | Montserrat font family |
| **Total UI** | **~300-450 KB** | Included in heap allocation |

### Rendering Performance

- **Frame Rate**: 60 FPS (LVGL default)
- **Update Frequency**: 1 Hz for metrics (throttled to prevent CPU waste)
- **Dirty Area Tracking**: LVGL only redraws changed regions (efficient)

### Optimization Tips

1. **Minimize redraw calls**: Use `ui_logic_update_*()` sparingly
2. **Batch updates**: Lock display once per update cycle
3. **Use simple colors**: Avoid gradients (extra rendering)
4. **Compress images**: LZ4 compression available in EEZ Studio
5. **Cache computed values**: Store formatted strings, don't regenerate

---

## File Sizes & Build Impact

```bash
# Typical binary size contribution
ui.c + screens.c + styles.c:      ~20-30 KB (compiled)
images.c + image_*.c:             ~50-80 KB (compressed images)
fonts:                            ~30-50 KB (font rasterization)
LVGL library:                     ~150-200 KB (core)
                                  ─────────────
Total UI Layer Impact:            ~250-360 KB
```

---

## Support & Troubleshooting

### Common Issues

| Issue | Cause | Solution |
|-------|-------|----------|
| "Objects NULL" | `ui_init()` not called | Call `ui_init()` in `app_main()` |
| Display shows garbage | LVGL not initialized | Check `bsp_display_start_with_config()` |
| Touch not working | GT911 not configured | Verify I2C pins (GPIO 8, 9) |
| Text overlaps | Long strings | Use `lv_label_set_long_mode()` |
| Colors wrong | Endianness issue | Use `lv_color_hex()` (handles conversion) |
| Memory crashes | Heap exhausted | Increase heap in `menuconfig` |

### Getting Help

- **LVGL Docs**: https://docs.lvgl.io/
- **EEZ Studio Docs**: https://docs.envox.hr/eez/
- **ESP-IDF LVGL Integration**: Component documentation in esp-idf

---

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | April 2026 | Initial UI design (Booting + Dashboard screens) |

---

**Last Updated**: April 6, 2026  
**Generated By**: EEZ Studio  
**Rallybox Author**: Akhil  
**Status**: Production Ready ✅
