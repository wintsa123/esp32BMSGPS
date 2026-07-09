# Settings Connectivity And Quick Panel Interactions Design

## Architecture

This work stays in the existing ESP-IDF component boundaries:

- `components/esp_bms_idf_runtime/` owns Wi-Fi scan, Setup AP credentials,
  NVS persistence, HTTP-compatible external Wi-Fi credential application, and
  snapshot fields.
- `components/esp_bms_lvgl_ui/` owns TFT settings screens, keyboard state,
  hotspot QR rendering, quick-panel long-press progress, and queued UI actions.
- `main/idf_main.c` remains orchestration only: it consumes UI events, applies
  runtime actions, updates the LVGL snapshot, and saves settings through
  runtime APIs.
- `preview/` mirrors TFT visuals and interaction states for host-side PNG
  inspection.

The feature is a single task because the pieces are tightly coupled through the
runtime snapshot and settings detail pages. Implementation should still be
performed in small, independently verifiable slices: text/layout placement,
runtime Wi-Fi scan model, TFT Wi-Fi keyboard flow, hotspot QR, and quick-panel
long-press navigation.

## Runtime Data Model

Add a bounded station Wi-Fi scan model next to the existing runtime state:

- fixed maximum candidate count, e.g. 6 to match the tiny TFT list.
- per candidate: SSID text, RSSI, auth/open flag if available.
- pending scan request flag, scan-active flag, and scan-complete/status fields.
- snapshot projection for TFT display.

The runtime should start scans from the main loop, not from LVGL or HTTP task
callbacks. Use ESP-IDF Wi-Fi scan APIs on the station/APSTA stack and handle
scan completion through the existing Wi-Fi event path.

Credential submission should reuse the same internal pending external Wi-Fi path
used by `/api/wifi`: after TFT submits the selected SSID and password, runtime
saves credentials through `runtime_save_external_wifi_credentials()` and applies
station reconnect through the existing APSTA path.

No hidden/manual SSID input is part of the TFT flow. Users select an SSID from
scan results; the keyboard enters only the password for that selected SSID.

## UI Action Contract

The current action enum is not enough to carry SSID/password text. Add a text
payload to `esp_bms_lvgl_action_event_t` for the new Wi-Fi actions, or add
specific runtime entry points if that keeps the action struct smaller. Preferred
contract:

- new action: `ESP_BMS_LVGL_ACTION_SCAN_WIFI`
- new action: `ESP_BMS_LVGL_ACTION_CONNECT_WIFI`
- optional payload fields:
  - `wifi_ssid_valid`, `wifi_ssid[33]`
  - `wifi_password_valid`, `wifi_password[65]`

Clear all payload fields in `esp_bms_lvgl_ui_take_action_event()` after
handoff, just like the existing percent fields.

## TFT Wi-Fi Flow

Wi-Fi detail page states:

1. Default: status row plus "扫描网络" action row.
2. Scanning: show an in-progress row and keep the previous list until replaced.
3. Results: show up to the bounded scan result count; selecting a result opens
   the password keyboard for that SSID.
4. Password: show selected SSID, masked/visible password buffer as appropriate,
   a lightweight ASCII keyboard, submit, delete, and cancel.
5. Submitted: queue connect action and show connecting/status text while runtime
   applies credentials.

Use a custom lightweight LVGL object grid for the keyboard instead of enabling
`LV_USE_KEYBOARD` and `LV_USE_TEXTAREA`. Current sdkconfig disables both; a
custom grid keeps memory and dependency surface smaller. The keyboard should
support:

- lowercase/uppercase letters
- digits
- common Wi-Fi password symbols
- backspace, clear, shift/symbol mode, submit, cancel

All keyboard buttons should be ordinary LVGL objects/labels with fixed sizes.
Avoid transform-scale, opacity animation, and off-screen layer effects.

## Hotspot Detail

Hotspot detail should be built from live snapshot data, not static rows:

- status row: Setup AP enabled/on/off
- SSID row: `snapshot.setup_ap_ssid`
- password row: `snapshot.setup_ap_password`
- QR panel: `lv_qrcode_create()` once, `lv_qrcode_update()` only when
  `snapshot.setup_ap_qr_payload` changes

The QR payload remains the existing runtime-generated
`WIFI:S:<ssid>;T:WPA;P:<password>;;`, so TFT and Web UI stay consistent.

The existing random-first-use and NVS persistence rules remain unchanged.
This task may improve display/update plumbing, but must not regenerate
credentials on every boot or on every settings page open.

## Settings Pull Guard

The quick panel should only be opened from dashboard pages. The existing
`quick_pull_zone` hiding while settings is visible should be kept, and
`quick_pull_event_cb()` should also guard against settings visibility before
tracking a pull. This makes the no-quick-panel-in-settings rule robust even if
z-order or foreground object order changes.

## Quick-Panel Long-Press Navigation

Bluetooth, hotspot, and Wi-Fi quick tiles get a hold-to-open-settings behavior:

- `LV_EVENT_PRESSED`: start pressed state.
- `LV_EVENT_LONG_PRESSED`: mark hold navigation active, shrink tile, start a
  timer that increments progress until complete.
- progress rendering: draw a colored border/ring using line/arc primitives or
  thin fixed-position child objects around the tile. Do not use
  `transform_scale` or opacity animations.
- completion: navigate to the matching settings detail:
  - Wi-Fi -> Wi-Fi settings detail
  - Hotspot -> Hotspot settings detail
  - Bluetooth -> Bluetooth settings detail
- release/press-lost before completion cancels progress and suppresses the
  normal click/toggle action.

The normal tap behavior remains unchanged. A quick tap toggles the local active
visual state and does not navigate.

## Preview

Update `preview/bms_lvgl_ui_preview.py` to mirror:

- root Wi-Fi subtitle `SSID`
- Wi-Fi detail list and password keyboard state
- Hotspot detail with SSID/password/QR placeholder rendering
- System detail with OTA row
- quick-panel network tile hold progress state

Preview should include both portrait and landscape enough to catch layout
overflow and text clipping.

## Compatibility

- Existing Web UI routes remain unchanged.
- Existing Setup AP NVS keys and QR payload format remain unchanged.
- Existing quick-panel tap behavior remains unchanged.
- Existing settings right-swipe return remains unchanged.
- Existing BMS BLE scan/bind flow remains unchanged.

## Risks

- Wi-Fi scan and APSTA mode can interfere with active SoftAP behavior if scan
  configuration is too aggressive. Keep scans bounded and initiated from the
  runtime loop.
- Adding text payloads to the UI action event affects main/runtime consumers;
  run GitNexus impact before editing the struct and consumers.
- TFT keyboard can crowd the 240x320 portrait layout. Preview must verify
  fixed dimensions and no text overlap.
- LVGL QR widgets can be memory-sensitive; create once and update only when
  payload changes.
- Long-press can still emit click on release in LVGL 9. Use explicit suppress
  flags and `lv_indev_wait_release(lv_indev_active())` when the hold behavior
  completes.

## Rollback

Rollback should revert task-scoped changes in:

- `components/esp_bms_idf_runtime/include/esp_bms_idf_runtime.h`
- `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c`
- `components/esp_bms_lvgl_ui/include/esp_bms_lvgl_ui.h`
- `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c`
- `main/idf_main.c`
- `preview/bms_lvgl_ui_preview.py`
- `sdkconfig.defaults` only if implementation later chooses to enable new LVGL
  widgets; the preferred custom keyboard design should avoid that.
