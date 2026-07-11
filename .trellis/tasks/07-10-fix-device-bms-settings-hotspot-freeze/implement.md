# Implementation Plan

## Pre-Edit Checks

- Run Trellis before-dev context for frontend/backend specs.
- Use GitNexus impact analysis before editing every function/symbol.
- Preserve existing user/uncommitted changes; do not revert unrelated work.

## Steps

1. Reproduce/observe the current firmware logs through RFC2217 monitor:
   - TFT `连接蓝牙` scan action.
   - TFT BMS type selection.
   - Setup AP client connect and Web page/API traffic.
2. Trace TFT scan/type event paths:
   - `settings_detail_action_event_cb`
   - `settings_bms_ble_start_scan`
   - `settings_show_bms_ble_popup`
   - `settings_bms_type_option_event_cb`
   - `esp_bms_lvgl_ui_take_action_event`
   - `settings_show_detail`
   - `settings_detail_back_event_cb`
   - `settings_swipe_event_cb`
3. Trace runtime scan/type paths:
   - `esp_bms_idf_runtime_apply_action_event`
   - `runtime_set_pending_http_bms_scan`
   - `runtime_apply_pending_http_bms_scan`
   - `runtime_bms_scan_project_snapshot`
   - `runtime_select_bms_type`
4. Refactor the BMS settings UI:
   - add explicit root/BLE-list/type-list subpage state.
   - keep exactly two rows on the BMS root page.
   - replace popup creation/close paths with full-page list renderers.
   - make header-back and edge-back pop one settings level at a time.
   - refresh only the open BLE/type subpage when relevant snapshot state changes.
   - preserve the existing scan, bind, and type-selection action contracts.
5. Investigate Setup AP freeze:
   - capture heap logs before/after AP start, AP client connect, HTTP requests,
     BLE scan.
   - inspect HTTP server stack/socket settings and long-running handlers.
   - adjust resource usage or scheduling only where evidence points.
6. Fix Setup AP QR lifecycle and alignment:
   - retain the QR panel pointer and center the QR widget with LVGL alignment.
   - encode the first valid fixed payload once per cached page lifetime.
   - hide/show the full QR panel from the Setup AP enabled flag.
   - verify repeated snapshots and off/on toggles do not re-encode the payload.
7. Add settings left-edge back feedback:
   - start only inside a bounded left-edge zone.
   - move one persistent affordance with the pointer.
   - suppress accidental row clicks after a recognized drag.
   - return one settings level after a committed release; animate away on
     cancellation.
8. Generate desktop LVGL previews under repository-root `preview/` for:
   - hotspot disabled and enabled.
   - BMS root, BLE candidate list, and BMS type list.
   - portrait and landscape layouts where supported by the preview harness.
9. Build:
   - `git diff --check`
   - `./scripts/esp-idf-env.sh build`
10. Run GitNexus detect changes:
   - `node .gitnexus/run.cjs detect-changes -r esp32BMSGPS`
11. Do not flash for this follow-up. The user explicitly requested preview-only
    UI validation after the successful ESP-IDF build.

## Risk Areas

- Main loop action ordering can affect all quick-panel/settings actions.
- Runtime BMS scan shares NimBLE with Bluetooth advertising and bound BMS
  connection startup.
- Setup AP/HTTP changes can affect the phone Web configuration UI.
- Snapshot-driven page rebuilds can accidentally reset the BMS subpage or
  trigger a second scan; subpage state and entry actions must be separated.
- Hiding only the QR child leaves a misleading white panel; visibility must be
  applied to the QR panel container.

## Validation Evidence To Capture

- Logs showing TFT scan queues and runtime consumes scan.
- Logs showing candidates projected into snapshot while the TFT BLE list is open.
- Logs showing TFT type selection reaches runtime.
- Logs or a counter showing one QR encode followed by visibility-only hotspot
  toggles for the unchanged payload.
- Preview images showing centered hotspot QR and both dedicated BMS list pages.
- Logs showing touch samples/releases after phone connects to Setup AP and loads
  Web UI.
- Heap/task logs around AP/HTTP/BLE operations.
