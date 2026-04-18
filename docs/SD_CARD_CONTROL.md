# SD Card Control & GPIO Mount/Unmount Guide

**Last Updated**: April 6, 2026
**Version**: 1.0
**Status**: Complete & Tested

---

## Table of Contents

1. [Overview](#overview)
2. [Hardware Setup](#hardware-setup)
3. [GPIO Configuration](#gpio-configuration)
4. [Operation Modes](#operation-modes)
5. [Real-Time Detection](#real-time-detection)
6. [Troubleshooting](#troubleshooting)
7. [Examples](#examples)

---

## Overview

### Purpose

The Rallybox system features **independent GPIO-controlled mount/unmount** for both SD card slots, enabling selective activation/deactivation of each card without affecting the other. This is useful for:

- **Power management**: Disable unused SD card to reduce current draw
- **Hot-swap**: Safely eject SD Card 2 (external slot) while keeping SD Card 1 mounted
- **Diagnostic**: Test SD card detection/communication independently
- **Selective logging**: Record data to only the desired SD card

### Key Characteristics

| Feature | Details |
|---------|---------|
| **Control Method** | GPIO-based, independent pins per card |
| **Detection Rate** | Real-time polling every 250ms (4 Hz) |
| **Debounce** | 100ms hysteresis to prevent false toggles |
| **Default State** | Both LOW (mount) via pull-down resistors |
| **UI Feedback** | Real-time LVGL popups on state change |
| **Mount State** | Persistent across GPIO toggles once mounted |

---

## Hardware Setup

### Control Circuit Requirements

Both GPIO45 and GPIO29 are **3.3V logic input pins** with internal pull-down resistors:

```
┌─── +3.3V
│
[Button/Switch]    Optional: Pull-up for clean edges
│                  (Not strictly required due to internal pull-down)
│
GPIO45 or GPIO29 ───────────→ ESP32-P4 GPIO input pin
│
└─── GND (pull-down active internally)
```

### Simple Push-Button Wiring

For a basic toggle button:

```
+3.3V ──[Button]──→ GPIO45 (or GPIO29)
                    ↓
                  [Internal pull-down]
                    ↓
                   GND
```

**Pressing button**: GPIO reads HIGH (3.3V) → Unmount
**Releasing button**: GPIO reads LOW (0V from pull-down) → Mount

### Recommended Safe Configuration

For reliability in noisy environments:

```
+3.3V ──[10K pull-up, optional]──┐
                                 [Button]
                                 ├──→ GPIO45 (or GPIO29)
                                 ├──→ [Internal 50K pull-down]
                                 └── GND
```

**Optional 10K pull-up**: Provides faster rise time, improves noise immunity

### GPIO Specifications

| Parameter | Value | Notes |
|-----------|-------|-------|
| **Input Voltage Range** | 0 - 3.3V | 5V tolerant input (not recommended) |
| **Pull-Down Strength** | ~50 kΩ | ESP32 internal resistance |
| **Maximum Input Current** | ±36 mA | Per pin limit |
| **Input Capacitance** | ~10 pF | Minimal filtering needed |

---

## GPIO Configuration

### Pin Definitions (From `sd_card.h`)

```c
#define SD1_CONTROL_PIN         45      /* GPIO45 controls SD Card 1 */
#define SD2_CONTROL_PIN         29      /* GPIO29 controls SD Card 2 */
```

### Initialization Code (From `main.c`)

```c
// Configure GPIO45 for SD Card 1 control
gpio_config_t sd1_control_conf = {
    .pin_bit_mask = (1ULL << SD1_CONTROL_PIN),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,    // No pull-up
    .pull_down_en = GPIO_PULLDOWN_ENABLE, // Pull-down enabled
    .intr_type = GPIO_INTR_ANYEDGE,       // Trigger on any edge
};
gpio_config(&sd1_control_conf);

// Configure GPIO29 for SD Card 2 control
gpio_config_t sd2_control_conf = {
    .pin_bit_mask = (1ULL << SD2_CONTROL_PIN),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_ENABLE,
    .intr_type = GPIO_INTR_ANYEDGE,
};
gpio_config(&sd2_control_conf);
```

### Boot Diagnostics

On startup, the firmware logs initial GPIO states:

```
I (706) app_main: SD Card control pins configured:
I (706) app_main:   GPIO45 (SD Card 1): LOW=mount, HIGH=unmount
I (708) app_main:   GPIO29 (SD Card 2): LOW=mount, HIGH=unmount
I (710) app_main: GPIO initial states after config: GPIO45=0, GPIO29=0
I (716) app_main: ✓ Both GPIO45 and GPIO29 are LOW (default mount state)
```

If diagnostics show HIGH (1), check your wiring to ensure pull-down resistors are working correctly.

---

## Operation Modes

### Mount State Logic

```
GPIO Level    Action                  UI Feedback
─────────────────────────────────────────────────────────
LOW (0V)      Mount SD card           Green popup "SD Card X Mounted"
              (if not already)        Display capacity & files
              
HIGH (3.3V)   Unmount SD card         Orange popup "SD Card X Disconnected"
              (if mounted)            Display "DISCONNECTED" status
```

### Real-Time Polling

The firmware continuously monitors both GPIO pins every 250ms (4 Hz):

```c
// From system_monitor_task() in main.c
while (1) {
    xTaskDelayUntil(&xLastWakeTime, xFrequency);  // 250ms interval
    
    // Read GPIO45 (SD Card 1)
    int gpio45_level = gpio_get_level(SD1_CONTROL_PIN);
    if (gpio45_level != prev_gpio45_level) {
        // Handle state change (see below)
        prev_gpio45_level = gpio45_level;
        vTaskDelay(pdMS_TO_TICKS(100)); // Debounce
    }
    
    // Read GPIO29 (SD Card 2) - same logic
    int gpio29_level = gpio_get_level(SD2_CONTROL_PIN);
    if (gpio29_level != prev_gpio29_level) {
        // Handle state change
        prev_gpio29_level = gpio29_level;
        vTaskDelay(pdMS_TO_TICKS(100)); // Debounce
    }
}
```

### Independent Control

Each GPIO pin is read and handled **completely independently**:

| Scenario | GPIO45 State | GPIO29 State | Result |
|----------|-------------|--------------|--------|
| Both mounted | LOW | LOW | SD1 mounted, SD2 mounted |
| Eject SD1 | HIGH | LOW | SD1 unmounted, SD2 mounted |
| Eject SD2 | LOW | HIGH | SD1 mounted, SD2 unmounted |
| Eject both | HIGH | HIGH | SD1 unmounted, SD2 unmounted |
| Remount SD1 | LOW | HIGH | SD1 mounted, SD2 unmounted |

---

## Real-Time Detection

### Detection Interval

The system polls GPIO states at **4 Hz (250ms intervals)**:

```
Time (ms)  GPIO45 Read  GPIO29 Read  Action if Changed
┌────────────────────────────────────────────────────┐
│ 0         [Read]      [Read]      (Initial state)   │
│ 250       [Read]      [Read]      Check for changes │
│ 500       [Read]      [Read]      Check for changes │
│ 750       [Read]      [Read]      Check for changes │
│ 1000      [Read]      [Read]      Check for changes │
└────────────────────────────────────────────────────┘
```

**Maximum latency**: ~250ms between GPIO state change and UI update

### Debounce Strategy

Each pin change is debounced with **100ms delay**:

```c
if (gpio_level != prev_gpio_level) {
    // Change detected
    ESP_LOGI(TAG, "*** GPIO%d CHANGED: %d -> %d ***", 
             pin, prev_gpio_level, gpio_level);
    
    // Perform mount/unmount
    // ...
    
    // Debounce delay (ignore further changes for 100ms)
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Update previous state
    prev_gpio_level = gpio_level;
}
```

**Benefits**:
- Prevents button bounce false-triggers
- Single state change can trigger mount/unmount cycle
- Debounce happens **within the 250ms polling cycle** (non-blocking)

### Serial Log Output

Every GPIO state change is logged to serial console:

```
I (15495) app_main: GPIO states - SD1(GPIO45)=0, SD2(GPIO29)=0
I (15500) app_main: *** GPIO45 CHANGED: -1 -> 0 (LOW=mount SD1, HIGH=unmount SD1) ***
I (15516) app_main: Action: GPIO45 LOW - Mounting SD Card 1
I (15535) app_main: SD Card 1 mounted successfully
I (15535) app_main: SD Card 1: 60050 MB total, 1253 MB used
W (15545) UI_Logic: UI: Showing SD Card 1 mounted notification
W (15545) UI_Logic: UI: SD Card 1 - 58.8 GB / 58.6 GB
```

---

## Troubleshooting

### GPIO State Not Detected (Stuck at Same Level)

**Symptom**: Toggle GPIO45 or GPIO29 but no change detected in logs

**Cause**: GPIO pin may be internally connected on your board design

**Solutions**:

1. **Verify GPIO availability**:
   ```bash
   # Check which GPIOs are actually available on your ESP32-P4 board
   # Visit: https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/hw-reference/esp32p4_datasheet.pdf
   ```

2. **Try alternative GPIO pins**:
   - GPIO12 (if GPIO45 doesn't work)
   - GPIO30, GPIO33 (if GPIO45 doesn't work)
   - GPIO15, GPIO16, GPIO17, GPIO18 (general purpose alternatives)

3. **Check wiring**:
   - Verify physical connection to GPIO pin
   - Ensure GND is common between all devices
   - No accidental 5V connection (damage risk)

### SD Card Mounts Then Unmounts Immediately

**Symptom**: GPIO LOW triggers mount, then immediately unmounts (HIGH)

**Cause 1**: Capacitive coupling or noise on GPIO pin
- **Fix**: Add 100nF ceramic bypass capacitor on GPIO pin to GND (place near ESP32)

**Cause 2**: GPIO pin shorted to power rail
- **Fix**: Check button/switch wiring; verify pull-down resistor strength

### "GPIO initial state is HIGH" on Startup

**Symptom**: Log shows `GPIO45=1, GPIO29=1` at boot

**Cause**: Pull-down resistor not active or insufficient strength

**Fix**:
1. Verify pull-down is enabled in `gpio_config_t` (`pull_down_en = GPIO_PULLDOWN_ENABLE`)
2. Add external 10K pull-down resistor to GND
3. Check for parasitic power leakage to GPIO pin

### No UI Popup on GPIO Change

**Symptom**: GPIO toggles (verified in logs) but no LVGL popup

**Cause**: Display lock timeout prevents UI update

**Fix**:
1. Ensure `bsp_display_lock()` is not stuck in high-priority task
2. Check LVGL task priority (must be lower than system monitor)
3. Review logs for `"failed to acquire display lock"` messages

### One SD Card Affects the Other

**Symptom**: Unmounting SD1 (GPIO45) also unmounts SD2

**Cause**: GPIO45 is reading GPIO29's state (cross-wiring)

**Fix**:
1. Verify each GPIO pin is wired to separate button/circuit
2. Check for solder bridges between GPIO45 and GPIO29 traces
3. Use multimeter to verify GPIO45 and GPIO29 read independently

---

## Examples

### Example 1: Manual Toggle via Serial Button

**Hardware**: Push button wired to GPIO45 and GND

**Behavior**:
- User presses button → GPIO45 goes HIGH → SD Card 1 unmounts
- User releases button → GPIO45 goes LOW → SD Card 1 mounts
- Logs show:
  ```
  I (15516) app_main: *** GPIO45 CHANGED: 0 -> 1 ***
  I (15516) app_main: Action: GPIO45 HIGH - Unmounting SD Card 1
  I (15535) sd_card: SD Card 1 unmounted
  W (15545) UI_Logic: UI: Showing SD Card 1 disconnected notification
  ```

### Example 2: Independent SD Card Control

**Hardware**: 
- Button A wired to GPIO45 (SD Card 1 control)
- Button B wired to GPIO29 (SD Card 2 control)

**Scenario**:
1. Both cards mount on boot (both GPIO pins LOW)
2. User presses Button A → GPIO45 HIGH → SD1 unmounts only
3. User presses Button B → GPIO29 HIGH → SD2 unmounts only
4. User releases Button A → GPIO45 LOW → SD1 remounts
5. SD2 remains unmounted (GPIO29 still HIGH)

**Logs**:
```
I (15516) app_main: *** GPIO45 CHANGED: 0 -> 1 ***
I (15535) app_main: SD Card 1 unmounted successfully
W (15545) UI_Logic: UI: SD Card 1 - DISCONNECTED
I (15600) app_main: *** GPIO29 CHANGED: 0 -> 1 ***
I (15615) app_main: SD Card 2 unmounted successfully
W (15625) UI_Logic: UI: SD Card 2 - DISCONNECTED
I (16000) app_main: *** GPIO45 CHANGED: 1 -> 0 ***
I (16015) app_main: SD Card 1 mounted successfully
I (16035) app_main: SD Card 1: 60050 MB total, 1253 MB used
W (16045) UI_Logic: UI: SD Card 1 - 58.8 GB / 58.6 GB
```

### Example 3: Power-Saving Mode (Disable Unused SD)

**Hardware**: Relay or MOSFET switch controlled by external MCU

**Script** (pseudo-code):
```c
// External system disables SD2 to save power
gpio_set_level_external(GPIO45, 1);  // Pull GPIO45 HIGH externally

// Firmware detects state change
if (gpio45_level == 1) {
    sd_card2_deinit();  // Unmount & power down
    ui_show_sd_card_event(2, false);
}

// Later, when SD2 needed again:
gpio_set_level_external(GPIO45, 0);  // Pull GPIO45 LOW externally

// Firmware detects state change
if (gpio45_level == 0) {
    sd_card2_init();    // Mount & power up
    ui_show_sd_card_event(2, true);
}
```

---

## API Reference

### GPIO State Reading

```c
// Read GPIO45 (SD Card 1 control)
int gpio45_level = gpio_get_level(SD1_CONTROL_PIN);
// Returns: 0 (LOW) = mount, 1 (HIGH) = unmount

// Read GPIO45 (SD Card 2 control)
int gpio45_level = gpio_get_level(SD2_CONTROL_PIN);
// Returns: 0 (LOW) = mount, 1 (HIGH) = unmount
```

### Mount/Unmount Functions

```c
// SD Card 1
esp_err_t sd_card1_init(void);      // Mount
esp_err_t sd_card1_deinit(void);    // Unmount
bool sd_card1_is_mounted(void);     // Query state

// SD Card 2
esp_err_t sd_card2_init(void);      // Mount
esp_err_t sd_card2_deinit(void);    // Unmount
bool sd_card2_is_mounted(void);     // Query state

// Get capacity info
esp_err_t sd_card_get_info(int slot, uint64_t *capacity_mb, 
                           uint64_t *used_mb);
```

### UI Notification

```c
// Show mount/unmount popup
extern void ui_show_sd_card_event(int slot, bool mounted);
// slot: 1 or 2
// mounted: true for "Mounted" popup, false for "Disconnected"

// Update storage display
extern void ui_logic_update_storage_info(int slot, bool detected, 
                                         uint64_t capacity_mb, 
                                         uint64_t used_mb);
```

---

## Changelog

### Version 1.1 (April 6, 2026)
- ✅ Swapped GPIO pins: SD1 now uses GPIO45, SD2 now uses GPIO29
- ✅ Updated all documentation and code references

### Version 1.0 (April 6, 2026)
- ✅ Initial implementation of independent GPIO control
- ✅ GPIO45 controls SD Card 1 (SDMMC)
- ✅ GPIO29 controls SD Card 2 (SDSPI)
- ✅ Real-time polling (250ms interval)
- ✅ 100ms debounce per pin
- ✅ LVGL popups on state change
- ✅ Serial logging for all events
- ✅ Pull-down default (safe floating state)

---

**For support or questions, refer to main README.md or contact the Rallybox development team.**
