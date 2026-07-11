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

### Visual Refinement

Retain the existing portrait and landscape coordinates and data placement.
Apply the reference draft only as a detail language:

- Give the SOC panel the blue emphasis, omit the redundant `SOC` caption, and
  place a large outlined battery between percentage and Ah capacity. Its inner
  fill width uses the validated SOC percentage and clears when SOC is invalid.
- Keep Ah capacity in the SOC panel. The lower-left panel remains the compact
  BMS protection/warning/info surface.
- Add one subtle horizontal rule between pack voltage and current.
- Add subtle horizontal rules between `MAX`, `MIN`, `DIFF`, and `AVG` rows.
- Add a small thermometer glyph above each temperature value while keeping
  `T1`-`T4`, `BAL`, and `MOS` ASCII labels legible.
- Use black for the page, dark charcoal for normal panels, blue for SOC,
  white for primary values, gray for captions/borders, and mint green for
  secondary telemetry. Keep protection/warning severity overrides intact.
- Keep SOC fill static; charging may change its color but must not create a
  wave object or infinite animation.

The draft's bottom rotation/BMS-connect row is intentionally omitted because
those controls already belong to device settings. No snapshot fields or
runtime parsing changes are needed for this visual-only increment.

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

## BLE Subscription And Bound Name

- Pass the characteristic value handle to NimBLE descriptor discovery. NimBLE
  advances past that handle internally, so passing the next handle would skip
  a CCCD located immediately after the characteristic value.
- Treat a successful CCCD write as the online boundary and reuse the existing
  ANT poll sender immediately rather than adding another connection state.
- Persist the selected scan candidate name beside the bound MAC. A later
  non-empty advertisement for the bound MAC may replace it; an empty name must
  not erase it.
- Keep a bounded in-memory name cache keyed by MAC for all scan candidates.
  Clearing the visible candidate list must not clear this cache, and a later
  nameless advertisement reuses the cached name.
- Candidate slots are immutable during one scan. When the bounded list is full,
  ignore additional MACs rather than reusing a visible row; explicit refresh
  clears the list and starts a new allocation window.
- Add the bound name to the existing snapshot contract and use it only for the
  Bluetooth subtitle. Anonymous candidates keep the existing numbered label.

## Web Config Completion

- Add a one-shot runtime snapshot flag when the main loop consumes a pending
  HTTP config request, regardless of whether values changed.
- Consume that flag only in the main LVGL-locked update path, apply brightness,
  rotation, language, units, and BMS type, refresh the snapshot, then call a
  narrow public UI wrapper that reuses the existing dashboard navigation.
- Do not change `show_dashboard_view` internals and do not set the flag for
  local TFT actions.

## BLE Candidate Confirmation And Scan Reuse

- Candidate rows copy the selected display name and MAC into stable UI state,
  then open a blocking confirmation overlay. The bind action is queued only by
  the confirm button; cancel rebuilds the unchanged candidate list.
- The connecting toast reuses the existing toast objects and loop symbol with
  an LVGL rotation animation, avoiding an additional widget dependency.
- Snapshot transition from offline to online replaces the connecting toast
  with `绑定成功`. Terminal BMS info states stop the animation.
- If `BMS_SCAN_ACTIVE` or `ble_gap_disc_active()` is already true, a refresh is
  successful reuse of the active scan. Do not cancel and synchronously restart
  the same GAP discovery procedure.
- Do not call the bound-MAC BLE startup path from `app_main()`. Opening the BMS
  candidate list and its refresh action remain the only scan triggers, so the
  dashboard cannot show candidates collected before the list is opened.
