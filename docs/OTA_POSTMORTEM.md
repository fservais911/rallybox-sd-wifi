# OTA Postmortem

## Summary

OTA was failing for more than one reason at the same time. The firmware image itself was valid, but the overall update path had UI, transport, and flash layout issues. The decisive fix for successful OTA boot was correcting the partition layout so both OTA slots stayed fully below the 16 MB flash boundary.

## Initial Symptoms

- Long save, upload, and OTA operations froze the UI because work was happening on the UI path.
- OTA progress updates caused visible screen flashing.
- ESP-Hosted traffic between the ESP32-P4 host and ESP32-C6 side was unstable in streaming mode.
- OTA downloads appeared to finish, but the update still failed during final verification or reboot.
- This made it unclear whether the problem was the binary, the download path, chip revision compatibility, or partition placement.

## What We Verified

The following possibilities were ruled out during the investigation:

- The OTA source artifact was valid.
- The uploaded and downloaded image sizes matched exactly.
- The uploaded and downloaded SHA256 values matched exactly.
- The configured minimum and maximum chip revision values were compatible with the actual chip revision.

To make that conclusive, the OTA flow was updated to log the source image size and SHA256 from the manifest, then compute and log the downloaded OTA partition size and SHA256 on-device.

## Root Cause

The main OTA blocker was partition placement.

One OTA slot was crossing the 16 MB flash boundary. In that configuration, the device could download the correct image bytes and still fail to use the slot as a valid OTA boot target. In other words, the failure was not due to payload corruption; it was due to where the payload was written in flash.

## Fixes That Worked

### 1. Move long operations off the UI thread

Save, upload, export, and OTA work was moved into background tasks with deferred UI updates. This removed the visible freeze during long operations.

### 2. Reduce OTA UI redraw churn

The OTA progress UI was updated less aggressively, and the progress label handling was simplified to avoid repeated layout changes. This reduced visible flashing during download.

### 3. Stabilize ESP-Hosted transport

ESP-Hosted SDIO was changed from streaming mode to packet/max-size mode. That improved transfer stability between the ESP32-P4 host and the ESP32-C6 side.

### 4. Add definitive OTA integrity logging

The OTA flow now logs:

- Source image size in bytes
- Source SHA256
- Downloaded OTA partition size in bytes
- Downloaded OTA partition SHA256
- Explicit `MATCH` or `MISMATCH` result

This removed guesswork and proved whether the device had actually stored the exact expected image.

### 5. Correct the partition table

The partition layout was changed so both OTA slots remain fully below `0x1000000`, and storage was moved above that boundary.

Working layout:

- `ota_0` at `0x20000`, size `0x7F0000`
- `ota_1` at `0x810000`, size `0x7F0000`
- `storage` at `0x1000000`, size `0x1000000`

This was the fix that made OTA boot succeed.

## Validation

After the partition update, the device rebooted successfully into `ota_1 @ 0x810000`. That confirmed the corrected partition layout resolved the OTA boot failure.

The earlier integrity logs had already proven that the downloaded image matched the source image exactly, so the successful boot closed the loop:

- The OTA payload was correct.
- The chip revision settings were correct.
- The partition layout had been the root cause.

## Lessons Learned

- Matching file size is not enough; on-device SHA256 comparison is the right integrity check.
- A valid firmware image can still fail OTA if the partition geometry is wrong.
- UI flashing during OTA was partly a redraw/layout problem, not only a transfer problem.
- When OTA still fails after image integrity is proven, partition mapping should move to the top of the suspect list.

## Related Areas

- OTA workflow and UI handling: `main/ui_logic.c`
- Active partition layout: `partitions.csv`
- OTA upload and manifest generation: `.vscode/scripts/upload_ota_firmware_s3.ps1`