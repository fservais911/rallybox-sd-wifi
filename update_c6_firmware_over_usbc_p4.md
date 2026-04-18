# Update C6 Firmware Over USB-C to P4

This guide updates the ESP32-C6 co-processor firmware using only the normal USB-C connection to the Waveshare ESP32-P4 board.

## Scope

- Host board: ESP32-P4 (connected to PC over USB-C)
- Co-processor: ESP32-C6 (updated over ESP-Hosted transport, no direct C6 USB required)
- Transport used between P4 and C6: SDIO (as configured in project)

## Important Notes

- Stop all running ESP-IDF build/flash/monitor tasks before starting.
- COM port busy errors and "One_Task_At_A_Time" errors must be resolved first.
- If C6 firmware is extremely old and does not support OTA RPC, this method may fail and direct C6 flashing may still be required.

## Prerequisites

1. USB-C connection from PC to Waveshare ESP32-P4 board.
2. ESP-IDF installed at `C:/esp/v5.5.4/esp-idf`.
3. Python environment from ESP-IDF tools available.
4. Workspace path:
   - `C:/Rallybox/RallyBoxSDWiFi`

## Step 1: Stop Conflicting Tasks

1. Close all active ESP-IDF Monitor terminals.
2. Cancel any running build/flash task in VS Code.
3. Ensure only one terminal session will run commands.

## Step 2: Open a Fresh Terminal and Set Environment

From `C:/Rallybox/RallyBoxSDWiFi`, run:

```powershell
$env:IDF_PATH = "C:/esp/v5.5.4/esp-idf"
$env:PATH = "$env:IDF_PATH/tools;$env:PATH"
```

## Step 3: Build ESP32-C6 Slave Firmware

```powershell
Set-Location C:/Rallybox/RallyBoxSDWiFi/managed_components/espressif__esp_hosted/slave
idf.py set-target esp32c6
idf.py build
```

Expected output artifact:

- `build/network_adapter.bin`

## Step 4: Stage C6 Firmware into Host OTA Example

```powershell
Copy-Item .\build\network_adapter.bin C:/Rallybox/RallyBoxSDWiFi/managed_components/espressif__esp_hosted/examples/host_performs_slave_ota/components/ota_partition/slave_fw_bin/ -Force
```

## Step 5: Build and Flash Host OTA Example to P4

```powershell
Set-Location C:/Rallybox/RallyBoxSDWiFi/managed_components/espressif__esp_hosted/examples/host_performs_slave_ota
idf.py set-target esp32p4
idf.py -p COM10 build flash monitor
```

If your COM port is not `COM10`, replace it accordingly.

## Step 6: Optional Menuconfig Check (Only if Needed)

If build or runtime behavior suggests wrong defaults:

```powershell
idf.py menuconfig
```

Verify:

- ESP-Hosted transport matches your board wiring (SDIO host path).
- OTA method is Partition (recommended for local update).

Then rerun:

```powershell
idf.py -p COM10 build flash monitor
```

## Step 7: Confirm OTA Success in Monitor Log

Look for messages like:

- ESP-Hosted initialized successfully
- Starting OTA via Partition
- OTA completed successfully
- Firmware activated or activation support message
- Host restart

## Step 8: Verify Version Mismatch Is Gone

After restart, this should no longer appear:

- `Version mismatch: Host [2.12.0] > Co-proc [0.0.0]`

If mismatch is gone and coprocessor reports a proper version, C6 update succeeded.

## Step 9: Reflash RallyBox Application

```powershell
Set-Location C:/Rallybox/RallyBoxSDWiFi
idf.py -p COM10 build flash monitor
```

## Troubleshooting

### A) COM port busy / access denied

Symptoms:

- `Could not open COMx`
- `PermissionError(13, 'Access is denied')`

Actions:

1. Close all monitors and serial tools.
2. Retry with a single terminal session.
3. Confirm the correct COM port in Device Manager.

### B) One_Task_At_A_Time errors

Symptoms:

- `Wait for ESP-IDF build or flash to finish`

Actions:

1. Cancel running ESP-IDF task.
2. Wait for task state to clear.
3. Run one `idf.py` command at a time.

### C) OTA RPC timeout or unsupported feature

Symptoms:

- Timeout on OTA commands
- `Failed to init hosted BT controller` remains and coprocessor version still `0.0.0`

Actions:

1. Reconfirm transport setup and stable host-slave connection.
2. Retry OTA once after clean reboot.
3. If still failing, C6 firmware may be too old for OTA RPC; direct C6 flash may be required once for baseline recovery.

## Reference Paths

- Host OTA APIs:
  - `managed_components/espressif__esp_hosted/host/api/include/esp_hosted_ota.h`
- ESP-Hosted top-level host API:
  - `managed_components/espressif__esp_hosted/host/esp_hosted.h`
- Example README:
  - `managed_components/espressif__esp_hosted/examples/host_performs_slave_ota/README.md`
- Example app:
  - `managed_components/espressif__esp_hosted/examples/host_performs_slave_ota/main/main.c`
