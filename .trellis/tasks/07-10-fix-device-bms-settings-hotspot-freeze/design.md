# Design

## Boundaries

- TFT UI code lives in `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c`.
- Runtime BMS scan, bind, Web API, Setup AP, and heap/task behavior live in
  `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c`.
- The main integration loop lives in `main/idf_main.c`.
- Touch/display bridge diagnostics live in
  `components/esp_bms_lvgl_bridge/esp_bms_lvgl_bridge.c`.

## Current Data Flow

### Web BMS Scan

`POST /api/bms/scan` -> `runtime_set_pending_http_bms_scan()` ->
`esp_bms_idf_runtime_tick()` -> `runtime_apply_pending_http_bms_scan()` ->
`esp_bms_idf_runtime_start_bms_ble_for_bind()` -> scan candidates ->
snapshot -> Web `/api/bms/candidates`.

### TFT BMS Scan

`保护板设置/连接蓝牙` -> `settings_show_bms_ble_popup(true)` ->
`settings_bms_ble_start_scan()` -> UI pending action
`ESP_BMS_LVGL_ACTION_START_BMS_BIND` -> main loop ->
`esp_bms_idf_runtime_apply_action_event()` ->
`runtime_set_pending_http_bms_scan()` -> same runtime tick path as Web.

The intended contract is that Web and TFT share the runtime scan queue. The
implementation must verify that the TFT action is not lost, overwritten, or
hidden by popup refresh state.

### TFT BMS Type

`settings_show_bms_type_picker()` -> row click ->
`settings_bms_type_option_event_cb()` -> committed UI action ->
main loop -> `runtime_select_bms_type()` -> snapshot update -> save display
settings.

## Revised TFT Navigation

The BMS settings flow becomes a three-level state inside the existing settings
detail container:

`设置根页` -> `保护板设置` -> `蓝牙连接` or `保护板类型`.

- `保护板设置` contains exactly two standard settings rows. Their subtitles
  project current snapshot state instead of adding separate status cards.
- `蓝牙连接` uses the full detail viewport. A compact refresh icon is fixed at
  the content start while the candidate list scrolls below it. Opening the page
  starts one scan; later snapshot changes rebuild only this subpage in place.
- `保护板类型` uses one full-width row per supported type. The selected row has
  a check mark and active border/color. A tap queues the existing action and
  returns to `保护板设置`; the next snapshot refreshes its subtitle.
- The existing header back button and edge-back gesture first pop the BMS
  subpage, then pop the BMS detail page. No `lv_layer_top()` popup is involved.

Represent this with an explicit BMS subpage enum (`ROOT`, `BLE_LIST`,
`TYPE_LIST`) stored in `s_ui`, rather than inferring navigation from transient
LVGL object pointers. This keeps snapshot-driven refresh and back navigation
deterministic.

## Setup AP QR Lifecycle

- Keep a pointer to the QR panel as well as the QR object so the complete visual
  unit can be hidden while Setup AP is off.
- Create the QR widget once when the cached hotspot page is built and center it
  with LVGL alignment APIs; do not rely on a hard-coded `9,9` offset.
- Encode the first valid snapshot payload once and record a `qr_ready` state.
  The device's Setup AP credentials are stable for the running page lifetime,
  so later snapshot applications and off/on toggles only hide or show the
  already-encoded panel.
- If the first page render arrives before credentials, allow one deferred encode
  when the first non-empty payload appears. Do not repeatedly update after a
  successful encode. If encoding fails, keep the panel hidden and log the
  failure without disturbing hotspot control.
- The QR panel is visible only when `SETUP_AP_ENABLED && qr_ready`; this rule is
  independent of whether the payload field remains populated while the AP is
  stopped.

## Failure Model

- FM1: TFT scan action is queued but overwritten before the main loop consumes
  it.
- FM2: Runtime scan starts and candidates exist, but the open TFT BLE list does
  not refresh with the new snapshot or is reset to the BMS root.
- FM3: Candidate labels/list rows render but are not clickable because previous
  gesture state swallows events.
- FM4: Type row click is swallowed before `queue_action()` or the action is not
  visible because the selected type matches stale snapshot data.
- FM5: Setup AP/Web traffic consumes enough heap or CPU to starve LVGL/touch.
- FM6: HTTP handler or Wi-Fi event path takes a lock or runs long enough to make
  the single main loop miss UI updates.
- FM7: The settings swipe handler recognizes a right swipe but provides no
  edge-only, finger-following feedback, so it conflicts with row taps and does
  not communicate whether releasing will navigate.

## Technical Approach

1. Instrument the exact TFT scan/type paths with concise logs or counters only
   where existing logs are insufficient.
2. Verify the runtime pending scan queue receives both Web and TFT scan
   requests.
3. Replace BMS popup trees with explicit full-page BMS subviews that reuse the
   settings detail container, header, and edge-back navigation.
4. Make BLE-list snapshot refresh preserve the current BMS subpage and clickable
   candidate rows without reallocating a top-layer overlay.
5. Add heap/task logging around Setup AP client connect and Web requests if the
   current logs do not prove the freeze cause.
6. If HTTP/AP memory pressure is confirmed, reduce HTTP server resource usage or
   defer expensive work outside touch-critical paths while preserving Web API
   behavior.
7. Reuse one persistent LVGL affordance for settings edge-back feedback instead
   of allocating/deleting objects during every gesture. Keep it hidden outside
   an active edge drag and animate it to the committed or cancelled position on
   release.
8. Reuse one persistent settings navigation bar for the settings root and all
   detail pages. Animate a single offset that moves the bar off-screen and
   reduces the scroll container top padding, so content follows the transition
   without allocating page-specific headers or leaving an empty top strip.
9. Center the QR object inside a stable panel, encode its fixed payload once,
   and drive only panel visibility from the live Setup AP enabled flag.

## Rollback

- TFT-only UI fixes can be reverted in `esp_bms_lvgl_ui.c`.
- The BMS subpage state and QR lifecycle are UI-local and require no action ABI,
  runtime snapshot, Web API, or persisted-config migration.
- Runtime scan/HTTP changes can be reverted in `esp_bms_idf_runtime.c`.
- Main-loop scheduling changes can be reverted in `main/idf_main.c`.
