# UI Component - Quick Reference

**Component**: Rallybox LVGL UI (EEZ Studio Generated)  
**Target Display**: 720×1280 MIPI DSI ILS LCD  
**Screens**: 2 (Booting, Dashboard)  
**Framework**: LVGL v9.4

---

## File Organization

```
components/ui/
├── 📋 UI_DOCUMENTATION.md ←── Read this for detailed architecture
├── 📋 README.md (this file)
├── ui.c/h              [Generated] Core initialization
├── screens.c/h         [Generated] Screen definitions
├── styles.c/h          [Generated] Style management
├── images.c/h          [Generated] Image resources
├── fonts.h             [Generated] Font declarations
├── actions.h           [Generated] Event handler stubs
├── CMakeLists.txt      [Config] Build configuration
└── .eez-project        [Design] EEZ Studio project file
```

## Key API

### Initialization & Screens

```c
// Initialize all UI components (call once in app_main)
ui_init();

// Load a screen by ID
loadScreen(SCREEN_ID_BOOTINGSCREEN);  // Show boot splash
loadScreen(SCREEN_ID_DASHBOARD);      // Show main dashboard

// Update UI every frame (called by LVGL automatically)
ui_tick();
```

### Object Access

```c
// All objects accessible via global 'objects' pointer
extern objects_t objects;

objects.bootingscreen              // Boot screen
objects.dashboard                  // Dashboard
objects.dashboard_uptime_1         // Uptime display label
objects.wifi_connection            // WiFi status label
objects.sdcard_storage_1_1         // SD1 usage bar
```

### Screen Update Functions

These are called from `ui_logic.c` to update dashboard metrics:

```c
// Update uptime display (HH:MM:SS)
ui_logic_set_uptime(uint32_t seconds);

// Update WiFi connection status
ui_logic_update_wifi_live(const system_status_t *status);

// Update SD card info (capacity & usage)
ui_logic_update_storage_info(int slot, bool detected, 
                             uint64_t capacity_mb, uint64_t used_mb);

// Update system metrics (CPU, heap, errors)
ui_logic_update_system_stats(uint32_t cpu_load, uint32_t errors);
```

## Screens Overview

### Screen 1: Booting Screen
- **Duration**: 10 seconds
- **Components**: Title, status label, spinner
- **Auto-transition**: Yes → Dashboard
- **Purpose**: Professional splash screen during startup

### Screen 2: Dashboard
- **Refresh**: 1 Hz (metrics update)
- **Sections**: WiFi, SD cards, System, Stats
- **Buttons**: Test/Format SD, WiFi settings
- **Purpose**: Real-time system monitoring

## Color Scheme

| Element | Color (Hex) |
|---------|-----------|
| Text | 0xFFF2F4F5 (light) |
| Background | 0xFF0F1419 (very dark) |
| Accent | 0xFF6BA6A8 (teal) |
| Success | 0xFF77B255 (green) |
| Warning | 0xFFD4A14A (amber) |
| Error | 0xFFD77474 (red) |

## Fonts Available

- **Montserrat 12 pt** - Small text
- **Montserrat 14 pt** - Regular small
- **Montserrat 16 pt** - Default (labels)
- **Montserrat 18 pt** - Section headers
- **Montserrat 20 pt** - Large headers
- **Montserrat 30 pt** - Titles

## Events & Actions

```c
// WiFi connection
void action_connect_wifi(lv_event_t *e);

// Keyboard management
void action_show_keyboard(lv_event_t *e);
void action_password_ssid_keyboad(lv_event_t *e);

// Screen navigation
void action_jumptowifiscreen(lv_event_t *e);
```

## Editing the UI

### Option 1: Visual Editor (Recommended)
```bash
# Open EEZ Studio and load the project
open components/ui/.eez-project

# After making changes:
# 1. Export/Generate from EEZ Studio
# 2. Rebuild project
idf.py build
```

### Option 2: Manual Code (Small Changes)
- Edit **ui_logic.c** for behavior ✅
- Edit **generated files** for structure ❌ (changes will be overwritten)

## Common Tasks

### Change Boot Screen Duration
Edit `main.c` line ~90:
```c
const TickType_t boot_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(10000);
```

### Update Dashboard Every 500ms Instead of 1 Hz
Edit `main.c` line ~130:
```c
const TickType_t xFrequency = pdMS_TO_TICKS(500);  // Was 250ms (4Hz with divider)
```

### Add New Screen
1. In EEZ Studio: Create screen visually
2. Generate/Export code
3. Add to `enum ScreensEnum` in **screens.h**
4. Call `loadScreen(SCREEN_ID_NEW_SCREEN)` when ready

### Customize Text Color
In **ui_logic.c**:
```c
lv_obj_set_style_text_color(objects.dashboard_title_1, 
                            lv_color_hex(0xFF00FF00), 0);  // Bright green
```

## Memory Impact

| Component | Size |
|-----------|------|
| UI Objects | ~250 KB |
| Images | ~60 KB |
| Fonts | ~40 KB |
| **Total** | **~350 KB** |

Typical available heap: 300-400 KB (sufficient for dual SD card buffers + UI)

## Integration Points

```c
// In main.c app_main():
ui_init();                              // Initialize UI
loadScreen(SCREEN_ID_BOOTINGSCREEN);    // Show boot screen

// In system_monitor_task():
ui_logic_set_uptime(status->uptime_seconds);      // Update metrics
ui_logic_update_wifi_live(status);
```

## Troubleshooting

| Problem | Check |
|---------|-------|
| Objects are NULL | Call `ui_init()` first |
| Display shows garbage | LVGL framebuffer not allocated |
| Touch not responding | GT911 I2C (GPIO 8, 9) |
| Text overlapped | Use `lv_label_set_long_mode()` |
| Memory crash | Increase heap in `menuconfig` |

## For More Details

See **UI_DOCUMENTATION.md** in this directory for:
- Complete architecture overview
- Detailed screen layout specifications
- Event handling system
- Styling & theming guide
- Maintenance & extension procedures

---

**Last Updated**: April 6, 2026  
**Status**: Production Ready ✅  
**Framework**: LVGL v9.4 + EEZ Studio
