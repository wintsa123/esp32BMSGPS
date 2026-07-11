# Fix Device BMS Settings And Hotspot Freeze

## Goal

Fix the device-side TFT/LVGL settings regressions around BMS setup and Setup AP use:
the protection-board BLE scan shown on the TFT must expose the same candidates
that the phone Web UI can see, the BMS type selection must be usable and
persisted, the BMS settings flow must use dedicated list pages instead of popup
pickers, the fixed Setup AP QR code must be centered and only visible while the
hotspot is enabled, and the TFT touch UI must remain responsive after a phone
connects to the Setup AP and loads the Web settings page.

## Background

- User report on 2026-07-10:
  - Phone connected to the Setup AP can see scanned Bluetooth/BMS candidates in
    the Web settings UI.
  - Device TFT settings page cannot scan/show candidates from `连接蓝牙`.
  - Device TFT cannot select the protection-board type.
  - After a phone connects to the Setup AP, the device TFT touch UI freezes or
    becomes unresponsive, suspected memory pressure.
  - The Setup AP QR code is visually misaligned and remains visible while the
    hotspot is disabled.
  - Setup AP SSID/password are fixed for the running device, so snapshot refresh
    must not repeatedly regenerate the same QR bitmap; the existing QR object
    should be shown or hidden instead.
  - The `保护板设置` page must contain only two entries: `蓝牙连接` and
    `保护板类型`. Each entry must open a dedicated full-page list instead of a
    modal popup; the detailed page design is delegated to this task.
- Current supported BMS type enum in firmware and Web UI is `ANT / JK / JBD /
  DALY`. There is no current `yanyang` runtime enum, parser branch, or UI
  contract in this task.

## Confirmed Code Facts

- Web BMS scan path:
  - `main/web/index.html` calls `POST /api/bms/scan`.
  - `runtime_http_post_bms_scan_handler()` queues
    `HTTP_BMS_SCAN_PENDING`.
  - `esp_bms_idf_runtime_tick()` calls
    `runtime_apply_pending_http_bms_scan()`, clears candidates, and starts
    `esp_bms_idf_runtime_start_bms_ble_for_bind()`.
- TFT BMS scan path:
  - `settings_detail_action_event_cb()` opens `settings_show_bms_ble_popup(true)`
    for `ESP_BMS_LVGL_ACTION_START_BMS_BIND`.
  - `settings_bms_ble_start_scan()` queues the same
    `ESP_BMS_LVGL_ACTION_START_BMS_BIND` action.
  - `esp_bms_idf_runtime_apply_action_event()` maps that action without a MAC to
    `runtime_set_pending_http_bms_scan()`, which should then be consumed by the
    same runtime tick path as Web scan.
- TFT candidate projection exists:
  - Runtime stores candidates through `runtime_bms_scan_store_candidate()`.
  - `runtime_bms_scan_project_snapshot()` copies them into the dashboard
    snapshot.
  - `apply_dashboard_snapshot()` reopens `settings_show_bms_ble_popup(false)`
    when scan candidates or BMS info text change while the TFT popup is open.
- Setup AP QR rendering currently creates a QR object inside the cached hotspot
  detail page, but `set_setup_ap()` still calls `lv_qrcode_update()` whenever
  the payload cache differs and shows the QR whenever the payload is non-empty,
  regardless of the hotspot-enabled flag. The QR object is positioned with a
  fixed offset instead of being centered in its panel.
- The current BMS settings page already has two logical actions, but both open
  `lv_layer_top()` modal popup trees. The settings detail header and left-edge
  back gesture can be reused for dedicated BMS subpages.
- Main loop service startup:
  - `main/idf_main.c` unlocks LVGL before starting Setup AP and HTTP server, then
    relocks only for a snapshot update.
  - Runtime logs heap at `setup_ap_http`, but the current issue needs live
    evidence after a phone connects and exercises Web endpoints.

## Requirements

- R1: TFT `保护板设置 -> 连接蓝牙` must start the same BMS BLE scan path as Web
  `/api/bms/scan`.
- R2: TFT BLE list page must update from runtime scan candidates and show
  candidate rows, or a truthful visible state (`Loading...`, `NO BMS`, or
  failure text).
- R3: Tapping a candidate on the TFT must queue a BMS bind action with the
  selected MAC.
- R4: TFT BMS type list page must allow selecting `ANT / JK / JBD / DALY`;
  selection must reach runtime, update the displayed type, and be saved when
  applicable.
- R5: Setup AP + HTTP traffic from a connected phone must not block LVGL/touch
  responsiveness.
- R6: If the freeze is caused by heap exhaustion, stack pressure, or task
  starvation, the fix must reduce/avoid the pressure and add enough logging to
  verify the result on hardware.
- R7: Preserve the existing fixed Setup AP SSID/password policy and the Web API
  behavior that already scans BMS candidates.
- R8: Settings pages must support an edge-back gesture that starts only from
  the left edge, reveals a lightweight visual affordance that follows the
  finger, and returns to the previous settings level when released beyond the
  threshold. Short or cancelled drags must animate the affordance away without
  navigating.
- R9: The settings root and every settings detail page must share a top
  navigation bar with a back control and centered page title. Browsing down
  must hide the bar and browsing back up must reveal it with a smooth
  transition while content fills the released space.
- R10: Every enabled TFT settings switch must use the shared green active
  color while disabled tracks remain gray and enabled thumbs remain white.
- R11: The hotspot QR panel and QR code must be centered with stable dimensions;
  neither may appear skewed or offset inside the other in portrait or landscape
  orientation.
- R12: The hotspot QR panel must be hidden whenever Setup AP is disabled and
  shown only when Setup AP is enabled and a valid QR payload has been encoded.
- R13: A fixed Setup AP QR payload must be encoded at most once per hotspot-page
  object lifetime. Routine dashboard snapshots and hotspot on/off toggles must
  only update visibility and must not call `lv_qrcode_update()` again.
- R14: The `保护板设置` root page must contain exactly two selectable rows:
  `蓝牙连接`, showing the current scan/connection state, and `保护板类型`,
  showing the selected `ANT / JK / JBD / DALY` value.
- R15: Selecting either BMS settings row must open a dedicated settings subpage
  with the shared title/back chrome and edge-back gesture. The BLE subpage must
  provide refresh, loading/empty/error states, candidate rows with name, MAC,
  and RSSI, and candidate binding. The type subpage must show all supported
  types with an unambiguous current-selection indicator and queue the existing
  type-selection action when tapped.
- R16: Returning from a BMS subpage must go first to `保护板设置`; returning from
  `保护板设置` must go to the settings root. Snapshot refreshes must preserve the
  currently open BMS subpage rather than reopening a popup or jumping levels.

## Acceptance Criteria

- [ ] With a nearby BMS candidate that appears in the phone Web UI, opening the
  TFT BLE list shows the candidate on the TFT without needing to leave/re-enter
  the page.
- [ ] Selecting a TFT candidate logs a `start-bms-bind` action with a valid MAC
  and runtime enters the bind/connect path.
- [ ] Opening the TFT BMS type list and selecting each supported type logs the
  expected `select-bms-*` action, updates the displayed type, and persists after
  the normal settings save path.
- [x] The protection-board root page shows only `蓝牙连接` and `保护板类型`,
  with no status cards, popup overlays, or extra controls.
- [ ] `蓝牙连接` opens a full-page candidate list, starts a scan once on entry,
  refreshes in place as snapshot candidates change, and binds the tapped MAC.
- [ ] `保护板类型` opens a full-page `ANT / JK / JBD / DALY` list, marks the
  active type, and returns to the BMS root with the new value after selection.
- [ ] Header-back and the left-edge gesture return from each BMS list to the BMS
  root before returning to the settings root.
- [x] With Setup AP off, neither the QR bitmap nor its white panel is visible.
  With Setup AP on, the QR is centered in the panel and matches the displayed
  SSID/password.
- [x] Repeated dashboard snapshots and at least three hotspot off/on toggles do
  not trigger another QR encode/update for the unchanged payload.
- [x] Portrait and landscape LVGL previews for hotspot off/on and both BMS list
  pages are stored under the repository-root `preview/` directory.
- [ ] After a phone connects to the Setup AP and loads the Web page, the TFT
  still logs touch samples/releases and responds to navigation/settings taps.
- [ ] Hardware monitor output shows no panic, watchdog reset, repeated LVGL lock
  failure, or heap exhaustion during Setup AP + Web + TFT interaction.
- [ ] Dragging right from the left edge of a settings detail page visibly pulls
  out a back affordance; releasing past the threshold returns one level, while
  releasing early restores the current page without an accidental click.
- [x] The settings root plus Hotspot, Bluetooth, BMS, System, and About pages
  render the shared back/title bar in the desktop LVGL preview.
- [x] Expanded, intermediate, and collapsed previews show the navigation bar
  and page content moving together without a blank strip.
- [x] Hotspot and Bluetooth enabled-switch previews use the same green track
  and border with a white thumb.
- [x] `./scripts/esp-idf-env.sh build` passes.
- [x] RFC2217 flashing is explicitly waived by the user for this follow-up;
  desktop previews plus a successful ESP-IDF build are sufficient validation
  for the QR and BMS settings UI changes.

## Out Of Scope

- Adding a new `yanyang` BMS protocol type.
- Implementing vendor-specific telemetry parsers beyond the current BMS scan,
  bind, and type-selection regressions.
- Allowing the user to edit Setup AP SSID/password from the TFT.
