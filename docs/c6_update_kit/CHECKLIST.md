# ESP32-C6 Update Checklist

This folder contains the known-good packet-mode ESP32-C6 firmware and the updater configuration snapshot used to install it.

## Files In This Kit

- `network_adapter_packet_mode.bin`
  - Packet-mode ESP32-C6 firmware verified on April 20, 2026
  - Size: `1206528` bytes
  - SHA256: `E44F329CEB8E34E17467DC1AC9A5AD5F154EB8935DF5C89A0E6F0389CCF7BF7B`
- `host_performs_slave_ota.sdkconfig.snapshot`
  - Snapshot of the updater app config that successfully forced the C6 update
  - OTA method: partition
  - OTA version checks: disabled for forced update

## When To Use This

Use this process when another RallyBox board still has the old ESP32-C6 firmware and needs the same packet-mode C6 update.

## One-Time Goal

The goal is:

1. Flash the temporary updater app to the ESP32-P4 host.
2. Let that updater app push `network_adapter_packet_mode.bin` into the ESP32-C6.
3. Reflash the normal RallyBox app back onto the ESP32-P4.

## Prerequisites

1. Connect the board over USB.
2. Close every open serial monitor before flashing.
3. Confirm the board COM port in Windows Device Manager.

## Step 1: Stage The C6 Firmware Into The Updater Project

From the repo root, copy the known-good C6 image into the updater project's partition OTA folder:

```powershell
Copy-Item "C:\Rallybox\RallyBoxSDWiFi\docs\c6_update_kit\network_adapter_packet_mode.bin" "C:\Rallybox\RallyBoxSDWiFi\managed_components\espressif__esp_hosted\examples\host_performs_slave_ota\components\ota_partition\slave_fw_bin\network_adapter.bin" -Force
```

## Step 2: Flash The Temporary Updater App To The P4

Open PowerShell and run:

```powershell
$env:IDF_PATH = "C:\esp\v5.5.4\esp-idf"
. "$env:IDF_PATH\export.ps1"
$env:ESPPORT = "COM10"
$env:ESPBAUD = "2000000"

Set-Location "C:\Rallybox\RallyBoxSDWiFi\managed_components\espressif__esp_hosted\examples\host_performs_slave_ota"
idf.py -p COM10 flash
idf.py -p COM10 monitor
```

If the board is not on `COM10`, replace `COM10` in both commands.

## Step 3: Wait For Successful C6 OTA

The important success lines are:

- `Starting slave OTA update...`
- `Partition OTA completed successfully`
- `OTA completed successfully!`
- `New firmware activated - slave will reboot`

After that, stop the monitor with `Ctrl+]` or `Ctrl+C`.

## Step 4: Reflash The Normal RallyBox App

```powershell
Set-Location "C:\Rallybox\RallyBoxSDWiFi"
idf.py -p COM10 flash monitor
```

## Expected Result

After the normal RallyBox app is back on the P4, the hosted connection should no longer show the old streaming-only mismatch. The C6 should already be on packet mode.

## If Flash Fails With Port Busy Errors

1. Close all VS Code monitors.
2. Unplug and reconnect the board.
3. Retry the same command.
4. Make sure no other serial tool is using the COM port.

## Notes

- This kit stores a tested firmware image and updater config snapshot for repeatable C6 recovery/update.
- The snapshot file is for reference and rollback history. The active updater config still lives in the example project.