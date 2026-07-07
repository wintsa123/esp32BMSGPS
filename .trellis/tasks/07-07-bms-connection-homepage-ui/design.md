# BMS Connection Validation And BMS Homepage UI Design

## Boundaries

- `esp_bms_idf_runtime` owns BMS BLE, ANT frame parsing, connection/info state,
  snapshot production, HTTP status/config JSON, and persistence.
- `esp_bms_lvgl_ui` owns the TFT homepage layout and rendering from
  `esp_bms_dashboard_snapshot_t`.
- `esp_bms_lvgl_bridge` should not change unless a display/touch regression is
  discovered during rotation validation.
- `main/idf_main.c` should not gain BMS business logic.
- `main/web/index.html` can be updated only for status display and full
  protection/warning lists; detailed BMS binding forms already exist.

## Snapshot Contract

Extend `esp_bms_dashboard_snapshot_t` conservatively. Expected additions:

- Fixed-size arrays or compact fields for active protection codes.
- Fixed-size arrays or compact fields for active warning codes.
- A compact BMS info/status code separate from fault codes.
- Temperature validity/value pairs for `T1`-`T4`, `BAL`, and `MOS` if protocol
  offsets can be verified.

The struct currently has a static size assertion in
`components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c`. Updating the snapshot requires
updating that assertion and every producer/consumer intentionally. Keep arrays
small and bounded to protect RAM and JSON size.

## BMS Parsing

Current status parsing reads dynamic ANT frame offsets from cell count and
temperature sensor count, then populates pack voltage, current, SOC, capacity,
and cell stats. The next implementation should:

1. Parse available temperature sensor values from the dynamic temperature
   section after verifying units and signedness.
2. Identify protection and warning bit offsets from known-good ANT frames,
   protocol references, or hardware captures.
3. Map known bits to fixed ASCII codes.
4. Preserve unknown set bits as `Pxx` / `Wxx` or hex-like codes.
5. Clear all BMS telemetry, protection, warning, info, and temperature fields
   together when reconnecting, rebinding, disconnecting, or invalidating BMS
   state.

If protection/warning bit offsets cannot be verified during implementation,
ship the separate snapshot/UI surfaces with unknown-bit preservation disabled
behind verified parsing checks, and keep hardware validation notes explicit.

## Display Priority

The TFT lower state area chooses one compact state:

1. First active protection code: `PROT <code>`.
2. First active warning code: `WARN <code>`.
3. Current info code: `INFO <code>`.
4. `INFO OK`.

Web UI/status JSON may expose all active protection and warning codes. TFT
shows only the highest-priority compact code to preserve glanceability.

## Homepage Layout

The first page becomes BMS-first:

- SOC tile: largest visual tile. Percent is primary. Ah line uses
  `remaining/totalAh` when both values are valid; otherwise `--/--Ah`.
- Pack tile: pack voltage and current together to reduce tile count.
- Cell tile: `MAX`, `MIN`, `DIFF`, `AVG`.
- State tile: highest-priority protection/warning/info.
- Temperature tile: `T1`-`T4`, `BAL`, `MOS`, each value or `--`.

Portrait 240 x 320 is the primary layout. Landscape should remain usable, but
the milestone can use a denser variant rather than matching portrait exactly.
Avoid controls on the homepage; settings remains separate.

## HTTP And Web UI

Keep the existing endpoints:

- `GET /api/status`
- `GET /api/config`
- `GET /api/bms/candidates`
- existing POST routes

Extend status JSON additively so old fields keep their meaning. Candidate
selection and bind flow remain unchanged. Web UI can render compact lists with
Chinese labels and English translations, using the existing language selector
state.

## Compatibility And Rollback

- Keep BMS BLE changes limited to current runtime code paths.
- If BLE instability threatens boot/display/setup AP, leave the homepage
  rendering and snapshot additions in place but gate new BLE parsing/connection
  behavior behind the existing bind/scan flow until debugged.
- If field BMS validation is unavailable, keep build validation complete and
  mark the hardware acceptance item as pending rather than pretending success.
