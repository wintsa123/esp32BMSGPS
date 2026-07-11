# BMS Connection Validation And BMS Homepage UI Implementation Plan

## Preflight

- Load project coding guidelines with `trellis-before-dev` before editing code.
- For every symbol to edit, run GitNexus upstream impact first, for example:

```bash
node .gitnexus/run.cjs impact esp_bms_dashboard_snapshot_t -r esp32BMSGPS --direction upstream
```

- Warn before proceeding if any impact result is HIGH or CRITICAL.
- Prefer narrow changes in existing components over new components.

## Execution Order

1. Inspect and record impacts for the planned symbols:
   - `esp_bms_dashboard_snapshot_t`
   - `runtime_clear_bms_telemetry`
   - `runtime_set_error`
   - `runtime_apply_bms_status_frame`
   - `runtime_apply_bms_frame`
   - `runtime_http_status_handler`
   - `set_dashboard`
   - the TFT layout builder function that owns the current first page
2. Extend the snapshot contract:
   - protection/warning code storage,
   - BMS info code,
   - temperature validity/value pairs,
   - ABI/static assertion updates.
3. Update runtime state clearing and BMS info updates so disconnect/rebind/error
   paths cannot leave stale telemetry or stale faults visible.
4. Extend ANT status-frame parsing only where offsets and units are verified.
   Preserve unknown bits when fault bitfields are known.
5. Extend HTTP status JSON additively for full protection/warning and
   temperature data.
6. Update Web UI status rendering only as needed to show the new lists without
   changing existing config/bind flows.
7. Redesign the TFT first page around the BMS homepage layout and update
   `set_dashboard` formatting.
8. Refine the existing homepage geometry without changing its data flow:
   - add the SOC battery icon and blue emphasis,
   - add pack and cell-row separators,
   - add six thermometer icons,
   - align homepage colors with the approved reference hierarchy.
9. Update the repository-root `preview/` renderer and generate portrait and
   landscape PNGs for direct visual inspection.
10. Run build validation and GitNexus detect-changes.
11. Flash through the LAN RFC2217 bridge and capture hardware validation
    status separately from build status.
12. Fix CCCD discovery to pass the characteristic value handle, log the
    discovered descriptor/subscription/poll/notification/parse stages, and
    send the first ANT query after subscription succeeds.
13. Persist and snapshot the selected BMS name, refresh it only from non-empty
    advertisements, and render it in the dashboard Bluetooth subtitle.
14. Set a one-shot HTTP-config-applied flag when pending Web config is
    consumed, then apply settings and navigate through a public dashboard
    wrapper from the LVGL-locked main loop.
15. Add candidate confirmation, a persistent connecting spinner toast, online
    success replacement, and failure cleanup using existing LVGL objects.
16. Treat an already-active NimBLE discovery as a successful scan request to
    remove the cancel/restart race that intermittently reports `BLE FAIL`.
17. Remove the boot-time bound-MAC BLE startup call. Keep candidate-list entry
    and refresh actions as the only discovery triggers, then verify boot logs
    remain free of BMS scan activity until the list is opened.
18. Cache advertised candidate names by MAC in bounded component memory so a
    refresh or later nameless advertisement cannot downgrade a known device to
    `设备 N`.
19. Remove full-list slot replacement entirely. Keep each row bound to one MAC
    for the scan lifetime and admit different devices only after explicit
    refresh clears the candidate list.

## Validation Commands

```bash
LVMP_BIN="$HOME/lv_micropython/ports/unix/build-lvgl-ft/micropython" \
  python3 preview/lvgl_render_compat.py preview/bms_lvgl_ui_preview.py \
  --out preview/bms_home_portrait.png --size 240x320
LVMP_BIN="$HOME/lv_micropython/ports/unix/build-lvgl-ft/micropython" \
  python3 preview/lvgl_render_compat.py preview/bms_lvgl_ui_preview.py \
  --out preview/bms_home_landscape.png --size 320x240
./scripts/esp-idf-env.sh build
node .gitnexus/run.cjs detect-changes -r esp32BMSGPS --scope all
```

## Hardware Checks

- Device boots to the BMS homepage.
- Backlight stays on.
- Setup AP remains reachable.
- BMS scan returns candidates when an ANT BMS is nearby.
- BMS scan does not start before the candidate list is opened.
- Binding the real BMS can connect and subscribe.
- Poll writes produce valid notifications.
- Telemetry updates only after valid notifications.
- Disconnect/missing service/missing characteristic/missing CCCD remains visible
  as BMS info/error state.

## Validation Result 2026-07-11

- RFC2217 flash succeeded on ESP32 `20:e7:c8:5f:ab:a4`.
- Real ANT BMS connected and exposed FFE0 handles 14-19, FFE1 value handle 16,
  and CCCD handle 17.
- Subscription succeeded, the immediate status poll was sent, fragmented
  notifications were reassembled, and telemetry parsed repeatedly (including
  70.450 V and 77% SOC).
- After removing the boot-time bound-MAC startup call, a fresh RFC2217 flash
  and boot log showed no NimBLE initialization, BMS discovery, or candidate
  logs before the BMS list was opened.
- Bound-name display remains pending because this BMS advertised without a
  name. Web-save navigation remains pending because Setup AP was not enabled
  during the hardware run.

## Rollback Points

- After snapshot/runtime parsing compiles but before layout rewrite.
- After layout rewrite but before Web UI expansion.
- If BLE changes destabilize boot, keep the BMS homepage and status contract but
  revert or narrow the BLE parsing/connection changes.
