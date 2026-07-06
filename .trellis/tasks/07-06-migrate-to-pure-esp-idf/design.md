# Pure ESP-IDF Migration Design

## Architecture

The final firmware should be an ESP-IDF CMake project with `app_main` in
`main/idf_main.c` and project logic split into components. `main/idf_main.c`
should remain a small orchestration layer:

1. initialize runtime state
2. initialize display/touch/LVGL
3. load persisted settings
4. start provisioning/network services
5. run the periodic runtime loop

Subsystem ownership should stay in components:

- `esp_bms_idf_runtime`: application state, NVS persistence, Wi-Fi, HTTP API,
  GPS, battery, BMS, OTA orchestration
- `esp_bms_lvgl_bridge`: ESP LCD, touch, LVGL adapter, brightness, rotation
- `esp_bms_lvgl_ui`: dashboard/settings UI and UI action queue
- new focused components may be added when a subsystem becomes large enough,
  for example `esp_bms_ble`, `esp_bms_ota`, or `esp_bms_web_api`

## Current Boundary

Already on the IDF path:

- top-level ESP-IDF CMake build
- LVGL adapter display bridge
- XPT2046 touch integration
- dashboard/settings LVGL UI
- display settings NVS persistence
- setup AP credential generation and NVS migration
- ESP-IDF SoftAP with DHCP
- partial HTTP API and embedded Web UI
- ADC local battery sampling
- UART0 GPS RMC parsing

Still pending or incomplete on the IDF path after Rust/Cargo cleanup:

- real OTA manifest parsing, download, verify, and apply behind
  `/api/ota/check` and `/api/ota/start`
- BMS BLE connection/notification/telemetry after binding
- adding C/component tests for high-value policy and parser contracts where
  practical
- removing Cargo/Rust from normal build/flash documentation and scripts

Already added during execution:

- external Wi-Fi station credential persistence
- AP+STA startup when external Wi-Fi credentials exist
- `/api/wifi`
- explicit unavailable JSON responses for `/api/ota/check` and
  `/api/ota/start` until the real OTA transport is implemented
- NimBLE-backed BMS scanning and `/api/bms/candidates`, with MAC deduplication
  and ANT advertisement filtering

## Data Flow

### UI Runtime Loop

```
LVGL touch/action -> esp_bms_lvgl_ui_take_action()
                  -> app_main
                  -> esp_bms_idf_runtime_apply_action()
                  -> esp_bms_lvgl_ui_update(snapshot)
```

The snapshot remains the UI contract. Any new BMS, Wi-Fi, or OTA feature must
update the runtime snapshot rather than writing dashboard widgets directly.

### Web API

```
HTTP request -> esp_http_server handler
             -> auth check against current/pending setup AP password
             -> runtime parser/validator
             -> pending state or subsystem command
             -> main runtime tick applies changes
```

Request handlers may queue state changes but should avoid directly mutating
display state while the LVGL task may read it. Existing pending fields and the
`http_pending_lock` pattern should be extended or replaced with a clearer command
queue if the number of pending actions grows.

### Wi-Fi

Setup AP remains available for provisioning. Station mode should be layered on
top of the existing AP behavior by moving from AP-only to AP+STA when external
credentials are present. The Web UI and TFT state should distinguish:

- setup AP available
- station connecting
- station connected
- station failed/offline

### BMS BLE

Rebuild the BMS control-plane contracts on the ESP-IDF path:

- scan request creates candidates
- candidates deduplicate by MAC and retain useful display fields
- binding persists the normalized MAC
- bound telemetry updates the dashboard snapshot

ESP-IDF BLE/NimBLE implementation details should live outside `main/idf_main.c`.

### OTA

The IDF implementation should use ESP-IDF OTA APIs and the existing OTA
partition table. OTA state should map to the existing UI enum:

- idle
- checking
- available
- downloading
- verifying
- ready
- failed

Manifest parsing should preserve the existing version/hash/size validation
semantics.

## Compatibility And Cleanup

The user chose to delete Rust/Cargo first, before full feature parity. Cleanup
must ensure:

- target firmware builds with ESP-IDF only
- flash scripts default to ESP-IDF
- no helper can accidentally flash an old non-IDF path
- docs no longer describe Rust as the normal firmware implementation
- stale Rust files and Cargo build metadata are removed from the firmware path

## Risks

- Removing Rust first drops the old reference implementation from the normal
  tree. The remaining missing features must be rebuilt directly on the IDF path.
- Expanding `esp_bms_idf_runtime.c` indefinitely will make the C migration harder
  to audit; split large subsystems when they become independently testable.
- Web UI route drift is user-visible because the current page already calls
  routes not implemented by the C runtime.
- BLE and Wi-Fi coexistence can affect memory and link stability on ESP32 without
  PSRAM. Validate heap after enabling BLE and OTA.
- OTA image size must be checked against the configured app slot, not only
  compile success.

## Rollback

Keep the current ESP-IDF display/setup AP path building after every milestone.
If a new subsystem breaks boot, revert only that component or disable it behind a
Kconfig/default-off setting while preserving the working display and setup AP.
