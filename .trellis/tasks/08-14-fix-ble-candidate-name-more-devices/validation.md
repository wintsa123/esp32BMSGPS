# Validation

## Completed

- `python3 -m unittest tests/test_ble_scan_source_contract.py`: 4 passed.
- `./scripts/run-host-selftests.sh`: passed.
- `./scripts/run-lvgl-simulator.sh --headless`: 480x320 passed.
- `./scripts/run-lvgl-simulator.sh --headless --portrait`: 240x320 passed.
- `git diff --check`: passed.
- GitNexus impact was run before edits. `settings_bms_ble_refresh_rows` was
  CRITICAL (5 direct callers, 15 flows, 4 modules); the full simulator matrix
  and hardware boot were retained as mandatory gates.
- GitNexus `detect-changes --scope compare --base-ref refs/heads/main` ran
  successfully. The workspace-wide CRITICAL result includes unrelated existing
  backlight, touch, quick-panel, and settings changes; this task is limited to
  candidate rendering/click smoke and its source contract.
- S3 profile `esp32s3-n16r8-st7796u-gt1151` built successfully. Application
  size: `0x1925f0`; the 6 MiB OTA slot has 74% free.
- Published full image:
  `output/esp32s3-n16r8-st7796u-gt1151/esp32s3-n16r8-st7796u-gt1151-flash.bin`.
  SHA-256: `a17f8918d96b22f9c7fa342afc8045387287a027e1d0faae321056a91bfdea2c`.
- RFC2217 flash at 115200 wrote 1,713,648 bytes and verified the device hash.
- Cold boot reached `heap[boot_ready]` on ESP32-S3 rev 0.2 with 16 MB Flash and
  8 MB PSRAM. Display/touch initialized; no panic or watchdog occurred.
- Default BLE behavior remained off: `no valid bound MAC; BLE stays off`.

## Pending Hardware Interaction

- Enter the Controller BLE list, wait for scan candidates, activate
  `More devices`, then activate one page-two candidate.
- Compare raw scan MAC/name logs with visible rows. Only same-MAC advertisements
  or cache entries may supply `midea`; nameless rows must show `设备 XX:XX`.
- Confirm the More action emits no binding-confirmation log and the page-two
  candidate confirmation contains the touched MAC.
