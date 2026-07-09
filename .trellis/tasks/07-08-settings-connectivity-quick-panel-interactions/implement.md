# Settings Connectivity And Quick Panel Interactions Implementation Plan

## Preconditions

- Task remains in planning until the user reviews `prd.md`, `design.md`, and
  this implementation plan and approves implementation.
- Inline Codex mode is active, so no sub-agent JSONL curation is required.
- The working tree already contains unrelated dirty changes; do not revert
  files outside this task's scope.
- Before editing any existing function, struct, enum, or method, run GitNexus
  impact and report HIGH/CRITICAL risks before proceeding.

## Implementation Checklist

1. Load development context.
   - Read `trellis-before-dev`.
   - Read `.trellis/spec/guides/index.md`.
   - Read `.trellis/spec/backend/quality-guidelines.md`.
   - Re-check `git status --short`.

2. Small settings text/layout fixes.
   - Change Wi-Fi root subtitle from `SSID / OTA` to `SSID`.
   - Remove OTA row from Wi-Fi detail rows.
   - Add OTA status/action row under System settings.
   - Update preview text tables to match.

3. Add runtime station Wi-Fi scan model.
   - Add bounded scan candidate fields to `esp_bms_idf_runtime_t`.
   - Add scan request/active/status fields.
   - Hook ESP-IDF scan start from runtime tick or action processing.
   - Handle scan completion in the existing Wi-Fi event handler.
   - Copy only bounded SSID/RSSI/auth data; never store passwords in scan
     candidate records.

4. Extend UI/runtime action contract for TFT Wi-Fi.
   - Add `ESP_BMS_LVGL_ACTION_SCAN_WIFI`.
   - Add `ESP_BMS_LVGL_ACTION_CONNECT_WIFI`.
   - Add bounded SSID/password payload fields to
     `esp_bms_lvgl_action_event_t`, or equivalent runtime entry points if a
     smaller contract is selected during implementation.
   - Clear payload fields after event handoff.
   - Route connect action through the same runtime external Wi-Fi apply path as
     `/api/wifi`.

5. Project scan results into the TFT snapshot.
   - Add bounded Wi-Fi scan result fields to `esp_bms_dashboard_snapshot_t`.
   - Keep snapshot updates safe under runtime lock/main-loop ownership.
   - Show scan active/empty/error status without fabricating networks.

6. Implement Wi-Fi detail state machine.
   - Add UI state for Wi-Fi detail mode: idle/scanning/results/password.
   - Render scan button and scan results.
   - Selecting a result stores the selected SSID and opens password mode.
   - Keep right-swipe settings return behavior working from every Wi-Fi subview.

7. Implement lightweight TFT password keyboard.
   - Use custom LVGL objects/labels; do not depend on `LV_USE_KEYBOARD` or
     `LV_USE_TEXTAREA`.
   - Fixed key dimensions for 240x320 and 320x240.
   - Modes: letters, shifted letters, digits/symbols.
   - Keys: character insert, backspace, clear, mode/shift, submit, cancel.
   - Enforce runtime password bounds before queueing connect.

8. Implement hotspot live detail and QR.
   - Replace placeholder hotspot rows with live snapshot SSID/password display.
   - Create `s_ui.setup_ap_info` and `s_ui.setup_ap_qr` in hotspot detail.
   - Update QR only when payload changes.
   - Ensure hidden/empty QR state is handled cleanly.

9. Harden quick pull guard.
   - Add an explicit settings-visible check in `quick_pull_event_cb()` before
     tracking pull-to-open.
   - Preserve quick pull behavior on dashboard/home and horizontal dashboard
     pages.

10. Implement quick-panel network tile hold progress.
    - Add hold state fields: active index, progress timer, progress percent,
      completed/suppress-click flags.
    - On long press for Bluetooth/hotspot/Wi-Fi, shrink tile and draw progress
      ring/border.
    - On timer completion, show settings page and the matching detail page.
    - On release/press-lost before completion, cancel and restore.
    - Suppress normal click/toggle after hold interactions.

11. Sync preview.
    - Settings root with Wi-Fi subtitle `SSID`.
    - System detail with OTA row.
    - Wi-Fi scan result state.
    - Wi-Fi password keyboard state.
    - Hotspot detail with QR area.
    - Quick-panel network tile hold progress.

12. Host validation.
    - Render and inspect preview PNGs:
      - settings root portrait and landscape
      - Wi-Fi scan/results/password states
      - Hotspot QR state
      - quick-panel hold-progress state
    - `python3 -m py_compile preview/bms_lvgl_ui_preview.py preview/lvgl_render_compat.py`
    - `git diff --check`
    - `./scripts/esp-idf-env.sh build`
    - `node .gitnexus/run.cjs detect-changes -r esp32BMSGPS`

13. Hardware validation.
    - Flash through:
      `./scripts/esp-idf-env.sh -p "rfc2217://192.168.2.10:4000?ign_set_control" -b 115200 flash`
    - Monitor boot logs through the same RFC2217 endpoint.
    - Confirm no boot panic/WDT with Wi-Fi, HTTP, NimBLE, touch, and LVGL
      active.
    - If possible, manually test:
      - settings page does not open quick panel by pull
      - dashboard pull still opens quick panel
      - hotspot QR can be scanned by phone
      - quick-panel Wi-Fi/hotspot/Bluetooth hold navigates to detail

## GitNexus Impact Targets

Expected existing symbols to analyze before editing:

- `esp_bms_lvgl_action_event_t`
- `esp_bms_dashboard_snapshot_t`
- `esp_bms_idf_runtime_t`
- `esp_bms_idf_runtime_apply_action_event`
- `esp_bms_idf_runtime_tick`
- existing Wi-Fi event handler function in `esp_bms_idf_runtime.c`
- `quick_panel_item_event_cb`
- `quick_pull_event_cb`
- `settings_show_detail`
- `settings_detail_rows_for_id`
- `settings_option_card`
- `create_screen`
- `esp_bms_lvgl_ui_update`
- `main` orchestration loop in `main/idf_main.c`

## Risky Files / Rollback Points

- `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c`
  - Wi-Fi scan, NVS, APSTA, HTTP route reuse.
- `components/esp_bms_idf_runtime/include/esp_bms_idf_runtime.h`
  - runtime struct growth and snapshot support fields.
- `components/esp_bms_lvgl_ui/include/esp_bms_lvgl_ui.h`
  - action/snapshot contract changes.
- `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c`
  - settings subviews, keyboard, QR, quick-panel long press.
- `main/idf_main.c`
  - action-event routing.
- `preview/bms_lvgl_ui_preview.py`
  - preview parity.

Rollback by reverting those task-scoped changes if build, flash, or boot
validation fails and cannot be fixed in-place.

## Review Gate

Before `task.py start`, review and approve:

- TFT Wi-Fi does not support hidden/manual SSID entry.
- The keyboard is custom/lightweight rather than LVGL `keyboard` + `textarea`.
- Runtime scan result list is bounded for TFT use.
- Long-press progress navigates only after completion; tap behavior stays as-is.
