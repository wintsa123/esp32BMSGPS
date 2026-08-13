# Validation

## Passed

- `./scripts/run-host-selftests.sh`
- `python3 -m unittest tests/test_ble_scan_source_contract.py tests/test_ble_host_bridge.py` (16 tests)
- LVGL headless smoke at 480x320 and 240x320, including BMS/Controller 0/6/7/12 candidate pagination
- ESP32-S3 profile `esp32s3-n16r8-st7796u-gt1151` full build
- `git diff --check`
- GitNexus compare change detection; task symbols match the planned BLE callbacks and runtime initialization scope
- RFC2217 flash through `rfc2217://192.168.2.10:4000?ign_set_control` with image hash verification
- Hardware cold boot reached `boot_ready` without panic/watchdog; log reported `no valid bound MAC; BLE stays off` and did not start local advertising

## Known Unrelated Failure

- `tests/configurator_selftest.sh` expects the previous S3 SPI draw-buffer height of 120. The existing dirty S3 display configuration sets 40; this predates and is outside this BLE task.

## Manual Hardware Follow-up

- The remote endpoint cannot operate the TFT. Confirm the settings switch starts/stops local discoverability and verify real BMS/Controller candidate rows with nearby peripherals.
