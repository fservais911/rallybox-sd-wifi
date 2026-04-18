# Rallybox ESP32-P4 Firmware

**Project Name**: Rallybox
**Developer**: Akhil
**Company/Author**: Rallybox

A production-grade rally computer firmware for the ESP32-P4 microcontroller, featuring real-time LVGL UI rendering, concurrent dual SD card management, WiFi connectivity, and system monitoring capabilities.

---

## Table of Contents

1. [Overview](#overview)
2. [System Architecture](#system-architecture)
3. [Hardware Configuration](#hardware-configuration)
4. [Features](#features)
5. [File Structure](#file-structure)
6. [Module Documentation](#module-documentation)
7. [SD Card GPIO Control](#sd-card-gpio-control)
8. [Building and Flashing](#building-and-flashing)
9. [API Reference](#api-reference)
10. [Development Notes](#development-notes)

---

## Overview

### Project Purpose

Rallybox is a sophisticated embedded system designed for rally sports telemetry and real-time vehicle diagnostics. It runs on the **ESP32-P4** SoC (System-on-Chip from Espressif) paired with a 720×1280 IPS LCD display featuring capacitive touch input and MIPI DSI interface.

### Key Achievements

- ✅ Concurrent operation of multiple high-speed peripherals (MIPI DSI display, SDMMC, SDSPI, WiFi)
- ✅ Real-time LVGL v9 UI with FreeType font support
- ✅ Dual SD card support (internal SDMMC + external SDSPI)
- ✅ WiFi connectivity via SDIO with automatic credential persistence
- ✅ Hosted BLE discovery/connection path for RaceBox Mini and RaceBox Micro
- ✅ UART GNSS ingestion for u-blox and LG290P NMEA receivers
- ✅ System monitoring (uptime, heap, CPU load, WiFi signal)
- ✅ 10-second boot sequence with professional splash screen
- ✅ Touch input support via GT911 capacitive controller

### Core Specifications

| Specification | Details |
|---|---|
| **Microcontroller** | ESP32-P4 (Xtensa dual-core 240 MHz) |
| **Display** | 720×1280 ILS LCD, MIPI DSI 2-lane @ 1500 Mbps |
| **RAM** | 768 KB on-chip SRAM (default) + external PSRAM available |
| **Storage** | 4 MB SPI Flash (bootloader + partition table + app + SPIFFS) |
| **SD Cards** | Dual slots: SDMMC (40 MHz) + SDSPI (20 MHz) |
| **WiFi** | IEEE 802.11a/b/g/n/ax via ESP-Hosted over SDIO |
| **Touch** | GT911 capacitive controller (I2C @ 400 kHz) |
| **IO Expansion** | UART, I2C, SPI, GPIO available |
| **Power** | USB-C with integrated voltage regulation |

---

## System Architecture

### Boot Sequence Timeline

```
T+0s        → NVS Flash init, GPIO setup
T+0.5s      → Display initialization (MIPI DSI, LVGL setup)
T+0.5-1s    → Boot screen active (system info splash)
T+1s        → SD Card 1 (SDMMC) detection attempt
T+1.5s      → SD Card 2 (SDSPI) detection attempt
T+5s        → WiFi stack initialized
T+10s       → Dashboard screen transition
T+10s+      → Continuous monitoring loop (4 Hz ticks, 1 Hz updates)
```

### Multi-Core Task Distribution

| Core | Priority | Task | Purpose |
|------|----------|------|---------|
| **Core 0** | 5 (highest) | LVGL Rendering | Real-time display refresh (must not skip frames) |
| **Core 0** | 4 | WiFi Event Loop | Non-blocking WiFi events |
| **Core 1** | 4 | System Monitor | Periodic metrics update, SD testing |
| **Core 1** | 3 | Background Tasks | SNTP sync, time logging |

### DMA Channel Allocation

```
DMA Channel 0  → Display (MIPI DSI)
DMA Channel 1  → SDMMC (internal SD slot)
DMA Channel 2  → SPI3 (external SD card)
DMA Channel 3  → WiFi over SDIO
```

### Memory Layout (Flash Partition Scheme)

```
0x0000_0000   nvs                  (16 KB)
0x0000_4000   nvs_keys             (0 KB)
0x0000_4000   phy                  (4 KB)
0x0000_5000   factory              (20 KB reserved)
0x0000_A000   ota_0                (main app, ~2 MB)
0x0020_0000   spiffs               (512 KB user files/config)
0x0280_0000   end of 8 MB SPI flash
```

---

## Hardware Configuration

### GPIO Pin Assignments

#### SDMMC (SD Card 1 - Internal)
| Function | GPIO |
|----------|------|
| CLK | 43 |
| CMD | 44 |
| D0 | 39 |
| D1 | 40 |
| D2 | 41 |
| D3 | 42 |

#### SPI3 (SD Card 2 - External)
| Function | GPIO | Purpose |
|----------|------|---------|
| CLK | 48 | Serial clock |
| MOSI | 47 | Master out, slave in (data to card) |
| MISO | 34 | Master in, slave out (data from card) |
| CS | 32 | Chip select |

#### SD Card Control (Independent GPIO)
| Signal | GPIO | Purpose | Default State |
|--------|------|---------|----------------|
| **GPIO45** | 45 | **SD Card 1 control** - LOW=mount, HIGH=unmount | LOW (pull-down) |
| **GPIO45** | 45 | **SD Card 2 control** - LOW=mount, HIGH=unmount | LOW (pull-down) |

**Note**: Both control pins use pull-down resistors, enabling LOW (mount) when left floating/unconnected. Each GPIO operates independently, allowing selective mount/unmount of either SD card without affecting the other.

#### I2C (Touch Controller)
| Signal | GPIO |
|--------|------|
| SCL | 8 |
| SDA | 9 |

#### MIPI DSI (Display)
- Special DSI pins: Configured via display clock/data lane outputs
- Resolution: 720×1280 pixels, 60 Hz refresh

#### Power Management
| Subsystem | LDO Channel | Voltage |
|-----------|-------------|---------|
| SDMMC Slot | 4 | 3.3V |
| Touch (GT911) | Via I2C pull-ups | 3.3V |

### External Dependencies

```
Managed Components:
├── lvgl__lvgl (v9.x)              → Graphics library
├── espressif__esp_lcd_touch        → Touch panel drivers
├── espressif__esp_lcd_ili9881c     → ILI9881C LCD driver
├── espressif__esp_lvgl_adapter     → LVGL-ESP32 integration
├── espressif__esp_kbd_gt911        → GT911 touch controller
├── espressif__esp_mmap_assets      → MMAP for assets
├── espressif__esp_wifi_remote      → WiFi over SDIO
├── chmorgan__esp-file-iterator     → File system utilities
└── espressif__button               → Button debouncing
```

---

## Features

### Display & UI
- **LVGL v9.4** with FreeType fonts (supports TTF/OTF)
- **RGB565 color format** (16-bit) for optimal memory usage
- **72 fps rendering** at 720×1280 resolution
- **12-bit color depth** for accent colors (3-color channels)
- **Touch input** with calibration support

### Storage
- **Dual concurrent SD** operation without bus contention
- **Hot-swap capable** external (SD2) slot
- **Independent GPIO control** for selective mount/unmount (GPIO45 for SD1, GPIO29 for SD2)
- **Real-time detection** with 250ms polling interval and 100ms debounce
- **Auto-format** on first mount (FAT filesystem)
- **Quick-format** utility with optimized parameters (32 KB AU)
- **File manager** UI with directory browsing and file preview

### Connectivity
- **WiFi 6E ready** (802.11ax support via ESP-Hosted)
- **Automatic credential persistence** via NVS flash
- **Non-blocking connection** (no UI freezing)
- **RSSI monitoring** (signal strength display)
- **SNTP time sync** (IST timezone, NTP pool)

### System Monitoring
- **Uptime tracking** (HH:MM:SS format)
- **Heap statistics** (free/total bytes)
- **WiFi status** (connected/disconnected, IP, RSSI)
- **SD card R/W counters** (cumulative operations)
- **CPU load approximation** (0-100%)
- **Error tracking** (SD I/O errors per slot)

### Advanced Features
- **Display brightness control** (0-100% PWM, 10% steps)
- **Backlight fade** on boot (configurable)
- **Screen lock timeout** (prevent accidental touches)
- **Event-based WiFi** (no polling)
- **Async background tasks** (no main task blocking)

---

## File Structure

### Main Application Files

```
main/
├── main.c                      # App entry point, system orchestration
├── display_brightness.{c,h}    # Backlight PWM control module
├── racebox.{c,h}               # RaceBox BLE discovery and connection module
├── gnss.{c,h}                  # UART GNSS reader and NMEA parser
├── sd_card.{c,h}              # Dual SD card init & management
├── system_monitor.{c,h}        # System status and WiFi management
├── ui_logic.c                  # UI logic, file manager, event handlers
├── CMakeLists.txt             # Component build configuration
└── idf_component.yml          # Component metadata
```

### Component Modules

Each module follows embedded developer best practices:

#### **main.c** (2400 lines)
- **Purpose**: System initialization and event orchestration
- **Exports**: `app_main()` entry point
- **Key Functions**:
  - `system_monitor_task()` - Background monitoring + boot sequence
  - `wifi_init_sta()` - WiFi stack init
  - `event_handler()` - WiFi event processing
  - `start_sntp_and_log_time()` - Time synchronization

#### **display_brightness.{c,h}** (150 lines)
- **Purpose**: LCD backlight PWM control abstraction
- **State**: Static brightness level caching (no HW polling)
- **Thread-Safe**: ISR-safe on 32-bit systems
- **API**:
  - `display_brightness_init()` - Initialize to 80%
  - `display_brightness_set(percent)` - Set explicit level
  - `display_brightness_increase/decrease()` - Step control
  - `display_brightness_is_max/is_min()` - Query limits

#### **sd_card.{c,h}** (400 lines)
- **Purpose**: Dual SD card management (SDMMC + SDSPI) with independent GPIO control
- **Concurrent**: Both slots operate independently
- **State**: Per-slot card descriptors + power handles
- **GPIO Control**: 
  - GPIO45 controls SD Card 1 (LOW=mount, HIGH=unmount)
  - GPIO29 controls SD Card 2 (LOW=mount, HIGH=unmount)
- **API**:
  - `sd_card1/2_init()` - Mount internal/external slot
  - `sd_card1/2_deinit()` - Unmount and power-down
  - `sd_card_get_info(slot, &capacity, &used)` - Capacity query
  - `sd_card_format(slot)` - Full reformat with FAT

#### **system_monitor.{c,h}** (450 lines)
- **Purpose**: Centralized system status and WiFi credential management
- **Architecture**: Single `system_status_t` struct (source of truth)
- **Update Rate**: Throttled to 1 Hz (prevents polling overhead)
- **NVS Integration**: Persistent WiFi SSID/password storage
- **API**:
  - `system_monitor_init()` - Initialize status struct
  - `system_monitor_update()` - Refresh metrics (call ~1 Hz)
  - `system_monitor_get_status()` - Read current status
  - `system_sdcard1/2_init()` - Mount SD cards with status update
  - `system_wifi_connect_credentials(ssid, pass)` - Connect with creds
  - `system_wifi_connect_saved()` - Auto-connect using NVS creds
  - `system_wifi_disconnect(clear_creds)` - Disconnect and optionally erase

#### **ui_logic.c** (2000+ lines)
- **Purpose**: UI update functions, file manager, event handlers
- **Integration**: Bridges system_monitor metrics to LVGL display
- **Components**:
  - File manager with directory tree and file preview
  - Keyboard input for SSID/password entry
  - Modern 8-color design system
  - Text display container for logs/reports
- **Key Functions**:
  - `create_modern_text_display()` - Polished text container
  - `style_modern_label()` - Consistent text styling
  - `file_manager_load_files()` - Directory enumeration
  - `ui_logic_update_*()` - Dashboard metric updates

---

## Module Documentation

### Core Concepts

#### Thread Safety
- **File reads**: Not synchronized (main task only)
- **System status**: Atomic reads OK (single `system_status_t` struct)
- **Display access**: Protected by `bsp_display_lock()` mutex
- **NVS access**: Handled by esp_nvs with internal locking

#### Error Handling
- **All functions**: Return `esp_err_t` (ESP_OK or failure code)
- **No exceptions**: C-only, uses return codes
- **Logging**: Always log errors before returning to help debugging

#### Memory Constraints
- **Heap**: Typically 200-300 KB available for application heap
- **Stack**: 8 KB per task (watch for deep recursion or large arrays)
- **PSRAM**: Optional, controlled via kconfig

### Update Functions (Called from Main Task)

These functions are called periodically from `system_monitor_task`:

```c
/**
 * Update Display with Fresh System Metrics
 * (Called every 250ms with 4x divider = 1 Hz refresh)
 */
xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(250));
if (bsp_display_lock((uint32_t)-1) == ESP_OK) {
    system_status_t *status = system_monitor_get_status();
    
    // Update each widget
    ui_logic_set_uptime(status->uptime_seconds);
    ui_logic_update_storage_info(1, status->sdcard1_initialized, capacity, used);
    ui_logic_update_wifi_live(status);
    ui_logic_update_system_stats(status->cpu_load_percent, errors);
    
    bsp_display_unlock();
}
```

---

## SD Card GPIO Control

### Quick Start

Rallybox supports **independent GPIO control** for selective SD card mount/unmount:

| GPIO | Function | Default | Logic |
|------|----------|---------|-------|
| **GPIO45** | SD Card 1 Control | LOW (pull-down) | LOW=Mount, HIGH=Unmount |
| **GPIO45** | SD Card 2 Control | LOW (pull-down) | LOW=Mount, HIGH=Unmount |

### Block Diagram

```
┌────────────────────────────────────────────────────┐
│  External Control Circuit                           │
│  (Push-button / Relay / External MCU)               │
│                                                     │
│  +3.3V ──[Button/Switch]──→ GPIO45                  │
│  +3.3V ──[Button/Switch]──→ GPIO45                 │
│                 ↓                                    │
└────────────────────────────────────────────────────┘
                 ↓
           ESP32-P4 with pull-downs
                 ↓
┌────────────────────────────────────────────────────┐
│  Real-Time GPIO Polling (250ms interval)            │
│  - GPIO45 HIGH → Unmount SD1                        │
│  - GPIO45 LOW  → Mount SD1                          │
│  - GPIO45 HIGH → Unmount SD2                       │
│  - GPIO45 LOW  → Mount SD2                         │
│  - 100ms debounce per pin                          │
└────────────────────────────────────────────────────┘
                 ↓
┌────────────────────────────────────────────────────┐
│  UI & Logging                                       │
│  - LVGL popup on state change                      │
│  - Serial log: "SD Card X Mounted/Disconnected"   │
│  - Capacity display updates in real-time           │
└────────────────────────────────────────────────────┘
```

### Features

- ✅ **Independent control**: Each GPIO operates completely independently
- ✅ **Real-time detection**: Changes detected every 250ms with 100ms debounce
- ✅ **UI feedback**: LVGL popups on mount/unmount events
- ✅ **Serial logging**: All events logged for diagnostics
- ✅ **Safe defaults**: Pull-down resistors default to LOW (mount) when floating
- ✅ **No hot-swap conflicts**: Unmounting one card doesn't affect the other

### Hardware Setup

**Simplest configuration** (push-button to GND):

```
GPIO45 ←→ [Button] ←→ GND          (controls SD Card 1)
GPIO29 ←→ [Button] ←→ GND          (controls SD Card 2)

Pressing: GPIO reads HIGH → Unmount
Releasing: GPIO reads LOW → Mount (via pull-down)
```

**Recommended configuration** (with external resistor):

```
GPIO45 ←→ [10K pull-up, optional] ←→ [Button] ←→ GND
GPIO29 ←→ [10K pull-up, optional] ←→ [Button] ←→ GND

Optional 10K pull-up provides better noise immunity & faster edges
```

### Typical Operation

1. **Boot sequence**: Both GPIO pins read LOW (pull-down active) → Both SD cards mount
2. **User presses GPIO45 button**: GPIO45 reads HIGH → SD Card 1 unmounts (SD2 unaffected)
3. **User presses GPIO29 button**: GPIO29 reads HIGH → SD Card 2 unmounts (SD1 unaffected)
4. **UI popups**: Each unmount shows orange "Disconnected" popup; capacity shows as "DISCONNECTED"
5. **Remounting**: Release button (GPIO goes LOW via pull-down) → Card remounts automatically

### Serial Output Example

```
I (706) app_main: SD Card control pins configured:
I (706) app_main:   GPIO45 (SD Card 1): LOW=mount, HIGH=unmount
I (708) app_main:   GPIO29 (SD Card 2): LOW=mount, HIGH=unmount
I (710) app_main: GPIO initial states after config: GPIO45=0, GPIO29=0
I (716) app_main: ✓ Both GPIO45 and GPIO29 are LOW (default mount state)

[User presses GPIO45 button...]

I (25000) app_main: GPIO states - SD1(GPIO45)=1, SD2(GPIO29)=0
I (25000) app_main: *** GPIO45 CHANGED: 0 -> 1 ***
I (25016) app_main: Action: GPIO45 HIGH - Unmounting SD Card 1
I (25035) sd_card: SD Card 1 unmounted
I (25035) app_main: SD Card 1 unmounted successfully
W (25038) UI_Logic: UI: Showing SD Card 1 disconnected notification
W (25046) UI_Logic: UI: SD Card 1 - DISCONNECTED
```

### Troubleshooting

**GPIO state not changing?**
- Verify GPIO45 and GPIO29 are free on your board (not used by WiFi/SPI)
- Check physical wiring to button/switch
- Ensure common GND connection
- See [docs/SD_CARD_CONTROL.md](docs/SD_CARD_CONTROL.md) for detailed troubleshooting

**For detailed hardware setup, operation modes, and examples**, see **[docs/SD_CARD_CONTROL.md](docs/SD_CARD_CONTROL.md)**.

---

## Building and Flashing

Project-specific configuration now lives under `menuconfig -> RallyBox` with grouped sections for:
- SD Card Settings
- WiFi Settings
- RaceBox Settings
- GNSS Settings

### Prerequisites

- ESP-IDF v6.x (from Espressif)
- Python 3.8+
- USB-UART bridge driver installed (CH340G typical)

### Build Steps

```bash
# Set ESP-IDF variables
source ~/<esp-idf-path>/export.sh

# Set target
idf.py set-target esp32p4

# Configure project
idf.py menuconfig
# (Adjust heap size, malloc cap, WiFi settings, etc.)

# Clean build
idf.py build

# Flash to device
idf.py -p /dev/cu.usbmodem5AE70680181 flash monitor
# (adjust port for your system)

# Monitor serial output
idf.py -p /dev/cu.usbmodem5AE70680181 monitor
```

### Configuration (menuconfig)

Key settings to review:

```
Component config:
  ├── ESP System Settings
  │   └── Heap Configuration
  │       ├── Heap size (default 96K, increase if needed)
  │       └── Memory type (internal/external PSRAM)
  ├── WiFi
  │   ├── Enable 802.11ax (Wi-Fi 6): YES
  │   └── Enable 802.11b: NO (space savings)
  └── SD/MMC
      └── Max frequency (40 MHz for SDMMC recommended)
```

### Troubleshooting Builds

| Issue | Solution |
|-------|----------|
| "No module named 'xxx'" | Run `idf.py install-python-packages` |
| Display stays black | Check MIPI DSI GPIO config in BSP |
| "Card not detected" | Press reset, check SD card contact, verify GPIO |
| WiFi connection times out | Verify WiFi credentials saved in `idf.py menuconfig` |
| Low heap warnings | Reduce LVGL cache (menuconfig → Component config → LVGL) |

---

## API Reference

### System Monitor API

#### `system_monitor_init()`
```c
/**
 * Initialize system monitoring subsystem.
 * Must be called once from app_main before background tasks.
 */
void system_monitor_init(void);
```

#### `system_monitor_update()`
```c
/**
 * Refresh system metrics (call periodically, ~1 Hz).
 * Throttles automatically if called too frequently.
 */
void system_monitor_update(void);
```

#### `system_monitor_get_status()`
```c
/**
 * Get pointer to current system status.
 * Safe to read from any context (atomic operations).
 * 
 * @return Pointer to static system_status_t struct
 */
system_status_t* system_monitor_get_status(void);
```

### SD Card API

#### `sd_card1_init()` / `sd_card2_init()`
```c
/**
 * Mount SD card and register with VFS.
 * 
 * @return ESP_OK on success, ESP_FAIL if card not detected
 */
esp_err_t sd_card1_init(void);
esp_err_t sd_card2_init(void);
```

#### `sd_card_get_info()`
```c
/**
 * Query SD card capacity and usage.
 * 
 * @param slot 1 (internal SDMMC) or 2 (external SDSPI)
 * @param out_capacity_mb Total capacity in MB
 * @param out_used_mb Used space in MB
 * @return ESP_OK on success
 */
esp_err_t sd_card_get_info(int slot, uint64_t *out_capacity_mb, 
                           uint64_t *out_used_mb);
```

### Display Brightness API

#### `display_brightness_init()`
```c
/**
 * Initialize backlight to default brightness (80%).
 * Must be called after bsp_display_start_with_config().
 * 
 * @return ESP_OK on success
 */
esp_err_t display_brightness_init(void);
```

#### `display_brightness_set()`
```c
/**
 * Set brightness to explicit level (0-100%, auto-clamped).
 * 
 * @param percent Target brightness (clamped to BRIGHTNESS_MIN/MAX)
 * @return ESP_OK on success
 */
esp_err_t display_brightness_set(int percent);
```

---

## Development Notes

### Coding Standards

This project follows embedded C best practices:

1. **No Dynamic Allocation**: Avoid `malloc()` after boot
2. **Fixed-Size Buffers**: All strings/arrays are stack or static
3. **Error Handling**: Every operation checks return codes
4. **Logging**: Use `ESP_LOG*` macros for all diagnostic output
5. **Comments**: Function headers precede every public function
6. **Type Safety**: Use `esp_err_t` for all error returns

### Debug Logging

Enable/disable debug logs via menuconfig:

```
Component config → Log output → Default log level → set to DEBUG
```

Example logging usage:

```c
ESP_LOGI(TAG, "System initialized");          // Info
ESP_LOGW(TAG, "SD card not detected");        // Warning
ESP_LOGE(TAG, "WiFi connection failed: %s", return_error); // Error
ESP_LOGD(TAG, "Uptime: %u seconds", uptime); // Debug (needs DEBUG level)
```

### Performance Considerations

| Subsystem | Typical CPU Usage | Notes |
|-----------|-----------------|-------|
| Display Refresh | ~60-80% (Core 0) | Real-time, must not miss frames |
| WiFi | ~5-10% average | Event-driven, bursty |
| SD Card I/O | ~2-5% during R/W | Depends on filesystem activity |
| System Monitor | <1% | Throttled to 1 Hz on Core 1 |

### Known Limitations

1. **WiFi Throughput**: Limited to ~20 Mbps due to SDIO interface (not limitation of device, but board design)
2. **Concurrent SD Access**: While both slots can operate, high-speed simultaneous I/O may cause brief UI stutter
3. **PSRAM**: Not used in default build; add external PSRAM via solder pads for increased heap if needed
4. **Battery**: No integrated battery management; requires external BMS for portable operation

### Future Enhancements

- [ ] USB Serial slave for laptop telemetry upload
- [ ] Real-time data logging with CSV output
- [ ] CAN bus integration for vehicle diagnostics
- [ ] OTA firmware update over WiFi
- [ ] LVGL screen rotation support (landscape mode)
- [ ] Multi-language UI support

---

## Support & Contact

For bug reports, feature requests, or technical support:

- **Author/Developer**: Akhil
- **Project**: Rallybox
- **Documentation Version**: 1.0 (April 2026)

---

**Last Updated**: April 6, 2026  
**Firmware Version**: POC v1.0  
**ESP-IDF Target**: v6.x (Stable)  
**Board**: Waveshare ESP32-P4-WIFI6-Touch-LCD-X
