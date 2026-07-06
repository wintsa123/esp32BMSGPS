# Migrate firmware to pure ESP-IDF

## Goal

Make the firmware a pure ESP-IDF application for target build, flash, and
runtime behavior. The final target firmware must be built through the ESP-IDF
CMake project, must not require the Rust/Cargo firmware path, and must preserve
the existing dashboard, setup AP, Web UI, GPS, battery, BMS, and OTA product
contracts.

The user explicitly requested moving fully to ESP-IDF after comparing Rust+IDF
against pure C/ESP-IDF.

## Confirmed Facts

- The repository already contains an ESP-IDF CMake application:
  - `CMakeLists.txt`
  - `main/idf_main.c`
  - `components/esp_bms_idf_runtime`
  - `components/esp_bms_lvgl_bridge`
  - `components/esp_bms_lvgl_ui`
- The IDF path is already the documented default display/runtime path in
  `README.md`.
- `main/idf_main.c` initializes the C runtime, LVGL display bridge, LVGL UI,
  display settings, setup AP, and then drives a 50 ms runtime loop.
- The C runtime already implements:
  - native ESP-IDF NVS initialization
  - display brightness, rotation, speed unit, and language persistence
  - GPIO34 ADC sampling for local battery voltage
  - UART0 NMEA RMC parsing for GPS speed/fix
  - native ESP-IDF setup AP with `192.168.4.1/24`
  - setup AP SSID policy `fuckingBms_` + six lowercase hex characters
  - setup AP password policy of eight random digits
  - native `esp_http_server` serving `/` and `/api/*`
  - partial Web API for status, config, AP password, BMS scan trigger, and BMS
    bind persistence
- The current C runtime uses NimBLE scanning for `/api/bms/scan` and returns
  discovered ANT BMS candidates through `/api/bms/candidates`.
- The current C runtime implements `/api/wifi` for external Wi-Fi credential
  persistence and AP+STA connection attempts.
- `/api/ota/check` and `/api/ota/start` return explicit unavailable JSON
  responses while the real ESP-IDF OTA transport is still pending.
- `README.md` states that OTA and BLE-backed BMS connection/telemetry are
  pending on the IDF path.
- On 2026-07-06, the user explicitly decided to delete the Rust/Cargo path now
  and continue filling the remaining ESP-IDF functionality afterward.
- GitNexus index status is up to date for repo `esp32BMSGPS` at commit
  `8084374`.

## Requirements

- R1. Keep the ESP-IDF CMake project as the only target firmware build path.
- R2. Preserve current hardware contracts:
  - ESP32-WROOM-32E, 4 MB flash, no PSRAM assumed.
  - TPM408/ST7789 TFT at 240 x 320 with XPT2046 touch.
  - GPIO34 local battery ADC.
  - UART0 GPS at 9600 baud.
  - Ant BMS BLE service/characteristic behavior from the existing product
    contract.
- R3. Preserve setup AP policy:
  - SSID must be `fuckingBms_` plus a random six-character lowercase hex suffix.
  - Password must be eight random digits.
  - First boot or stale NVS values must regenerate and save current credentials.
  - TFT/Web QR and visible credentials must match the actual ESP-IDF SoftAP
    configuration.
- R4. Preserve Chinese-first local Web UI behavior, with English selectable from
  device settings.
- R5. Preserve TFT language constraints:
  - Use ASCII markers such as `ZH`/`EN` where the target TFT font cannot render
    Chinese.
  - Do not add a full CJK font unless a separate task explicitly approves it.
- R6. Implement the Web API routes currently used by `main/web/index.html` on the
  ESP-IDF runtime:
  - `GET /api/status`
  - `GET /api/config`
  - `GET /api/bms/candidates`
  - `POST /api/config`
  - `POST /api/wifi`
  - `POST /api/ap-password`
  - `POST /api/bms/scan`
  - `POST /api/bms/bind`
  - `POST /api/ota/check`
  - `POST /api/ota/start`
- R7. Implement external Wi-Fi station credential persistence and connection on
  the ESP-IDF path without breaking setup AP provisioning.
- R8. Implement BMS BLE scan/candidate/bind/telemetry on the ESP-IDF path.
- R9. Implement OTA check/start/download/verify/apply on the ESP-IDF path using
  ESP-IDF OTA APIs and the existing partition layout.
- R10. Keep the IDF runtime modular. `main/idf_main.c` should remain orchestration
  only; subsystem logic belongs in components.
- R11. Do not log setup AP passwords, external Wi-Fi passwords, OTA credentials,
  raw request bodies, or private token-bearing URLs.
- R12. Before editing any function/class/method, run GitNexus impact analysis
  for the target symbol and record the blast radius in the work log or final
  summary.
- R13. Delete the Rust/Cargo firmware path now. Continue implementing remaining
  external Wi-Fi, BLE BMS, OTA, and Web API functionality on the ESP-IDF path
  after cleanup.

## Acceptance Criteria

- [ ] `./scripts/esp-idf-env.sh build` succeeds for the pure ESP-IDF app.
- [ ] Flash helper defaults to the ESP-IDF app and no target firmware workflow
      depends on Cargo/Rust.
- [ ] Setup AP starts natively with the required SSID/password policy, DHCP, and
      HTTP server.
- [ ] The QR page and Web UI show the current SSID and current setup AP password.
- [ ] A phone can join the setup AP and load the Web UI at `192.168.4.1`.
- [ ] All Web UI routes listed in R6 return implemented responses rather than
      `501 Not Implemented`.
- [ ] External Wi-Fi credentials can be saved through the Web UI and the device
      attempts station connection while preserving setup AP behavior.
- [ ] BMS scan returns candidates, binding persists a selected MAC, and bound
      telemetry updates the TFT/Web status.
- [ ] OTA check reports manifest state and OTA start can download/verify/apply
      an update through ESP-IDF OTA APIs.
- [ ] Display brightness, rotation, speed unit, and language persist across
      reboot in NVS.
- [ ] The final target image fits the configured OTA app slot in `partitions.csv`.
- [ ] Host-side or component-level tests cover policy/format logic that was
      previously covered by Rust tests where practical; Rust tests are no longer
      part of the normal validation path after cleanup.
- [ ] GitNexus `detect_changes()` / CLI `detect-changes` is run before commit to
      confirm the affected symbols and flows match the migration scope.
- [ ] README and scripts no longer present the Cargo firmware path as the normal
      target workflow.

## Notes

- This is a complex architecture migration. It needs `design.md` and
  `implement.md` before implementation starts.
- Rust/Cargo deletion is intentionally front-loaded by user decision, even
  though external Wi-Fi, BLE BMS, OTA, and some Web API routes still need to be
  completed on the IDF path.
