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
8. Run build validation and GitNexus detect-changes.
9. Capture hardware validation status separately from build status.

## Validation Commands

```bash
./scripts/esp-idf-env.sh build
node .gitnexus/run.cjs detect-changes -r esp32BMSGPS --scope all
```

## Hardware Checks

- Device boots to the BMS homepage.
- Backlight stays on.
- Setup AP remains reachable.
- BMS scan returns candidates when an ANT BMS is nearby.
- Binding the real BMS can connect and subscribe.
- Poll writes produce valid notifications.
- Telemetry updates only after valid notifications.
- Disconnect/missing service/missing characteristic/missing CCCD remains visible
  as BMS info/error state.

## Rollback Points

- After snapshot/runtime parsing compiles but before layout rewrite.
- After layout rewrite but before Web UI expansion.
- If BLE changes destabilize boot, keep the BMS homepage and status contract but
  revert or narrow the BLE parsing/connection changes.
