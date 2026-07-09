# Settings Connectivity And Quick Panel Interactions

## Goal

Make the TFT settings and quick panel support practical connectivity workflows:
Wi-Fi should be configured from the device with scan + password entry, hotspot
information should be visible and scannable from the TFT, quick-panel network
tiles should long-press into their matching settings pages, and the quick panel
must not open while the settings page is active.

## Confirmed Facts

- The project contract requires Setup AP SSIDs to use the `fuckingBms_` prefix
  plus a six-character lowercase hexadecimal suffix; the current runtime
  defines that prefix and length in
  `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:44`.
- The current runtime generates a random Setup AP suffix and an eight-digit
  numeric password in
  `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:174`.
- The current runtime persists Setup AP SSID/password to NVS keys
  `setup_ssid` and `setup_pw` in
  `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:2489`.
- Missing or stale Setup AP credentials are regenerated and saved by
  `runtime_ensure_setup_ap_credentials()` in
  `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:2941`.
- The runtime already exposes a Wi-Fi QR payload shaped as
  `WIFI:S:<ssid>;T:WPA;P:<password>;;` in the dashboard snapshot at
  `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:164`.
- The TFT snapshot contract already carries `setup_ap_ssid`,
  `setup_ap_password`, and `setup_ap_qr_payload` in
  `components/esp_bms_lvgl_ui/include/esp_bms_lvgl_ui.h:119`.
- The current TFT root settings option for Wi-Fi still uses subtitle
  `SSID / OTA` in `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:1568`.
- OTA currently appears inside the Wi-Fi detail rows at
  `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:1579`, while the System detail
  rows do not include OTA at
  `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:1605`.
- The current Hotspot detail rows show placeholder SSID/password text and a
  "web page" QR row, not a real TFT QR view, at
  `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:1582`.
- `set_setup_ap()` can update an LVGL QR object from the snapshot payload, but
  `create_screen()` currently leaves `s_ui.setup_ap_info` and
  `s_ui.setup_ap_qr` as `NULL` in
  `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:3454`.
- Quick pull gesture handlers are bound to the dashboard page container and
  the invisible top pull zone in
  `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:3287` and
  `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:3530`.
- Settings pages already hide `quick_pull_zone` when settings is visible via
  `set_quick_panel_open()`; this must be preserved and hardened for all
  settings transitions.
- The quick-panel Bluetooth, hotspot, and Wi-Fi tile event path currently shows
  a toast on long press but does not navigate to a specific settings detail
  page; see `quick_panel_item_event_cb()` at
  `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:1851`.
- LVGL QR is enabled (`CONFIG_LV_USE_QRCODE=y`) in `sdkconfig:2460`.
- LVGL keyboard and textarea are disabled in `sdkconfig:2398` and
  `sdkconfig:2415`.
- The runtime currently has no external Wi-Fi AP scan result model or HTTP/TFT
  API for station scan results; only `/api/wifi` can submit SSID/password in
  `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:2242`.
- Product decision: TFT Wi-Fi setup does not need hidden/manual SSID entry.
  The user expects the main case to be phone hotspots, where SSID hiding is not
  needed. The TFT flow should therefore select from scan results and only use
  the virtual keyboard for the selected network password.

## Requirements

- R1. The Wi-Fi root settings subtitle must be `SSID` only.
- R2. OTA must be shown under the System settings page, not the Wi-Fi settings
  page.
- R3. The TFT Wi-Fi settings flow must support scanning for nearby Wi-Fi SSIDs.
- R4. Selecting a scanned Wi-Fi network must provide a virtual keyboard for
  password entry.
- R5. Submitted Wi-Fi credentials must reuse the runtime's existing external
  Wi-Fi persistence and connect path, so the Web UI and TFT do not maintain
  separate credential stores.
- R6. The virtual keyboard implementation must be safe for the constrained TFT:
  no off-screen layer effects, no opacity animations, and no unnecessary LVGL
  widgets unless explicitly chosen during design.
- R7. Setup AP SSID/password initialization must remain random-on-first-use and
  then persistent until the user changes it.
- R8. The Hotspot settings page must display the active Setup AP SSID and
  password from the runtime snapshot, not hardcoded placeholders.
- R9. The Hotspot settings page must show a QR code that phones can scan to join
  the Setup AP directly.
- R10. The displayed QR payload must match the active SoftAP SSID/password.
- R11. Settings pages must not allow the quick panel to open by downward pull.
- R12. The quick panel may open only from the dashboard/home page and normal
  dashboard pages, including horizontal paged dashboard views.
- R13. Long-pressing the quick-panel Bluetooth, hotspot, or Wi-Fi tile must
  shrink the button and show a colored progress ring around the tile.
- R14. When the long-press progress completes, Bluetooth, hotspot, or Wi-Fi must
  navigate automatically to its matching settings detail page.
- R15. Releasing before the long-press progress completes must cancel the
  navigation and restore the tile state.
- R16. The quick-panel long-press progress interaction must not also trigger the
  normal click/toggle action on release.
- R17. Existing quick-panel tap behavior must remain intact: tapping Bluetooth,
  hotspot, or Wi-Fi toggles the local active visual state and does not navigate.
- R18. Preview rendering must be kept in sync for the changed settings pages,
  hotspot QR layout, keyboard layout, and quick-panel long-press progress state.
- R19. Hidden-network/manual SSID entry is not required for the TFT flow.

## Acceptance Criteria

- [ ] AC1. Root settings preview and firmware show Wi-Fi subtitle `SSID`, not
      `SSID / OTA`.
- [ ] AC2. Wi-Fi detail no longer shows OTA rows; System detail shows OTA
      status/action content.
- [ ] AC3. Wi-Fi detail can start a station scan and render a bounded list of
      discovered SSIDs.
- [ ] AC4. Selecting a Wi-Fi SSID opens a password-entry screen with a virtual
      keyboard and clear submit/cancel behavior.
- [ ] AC5. Submitting Wi-Fi credentials updates the runtime external Wi-Fi
      pending path and starts or reconnects station mode without duplicating
      credential storage.
- [ ] AC6. Hotspot detail displays the active SSID and password from the
      runtime snapshot.
- [ ] AC7. Hotspot detail displays a scannable QR code whose payload matches
      `WIFI:S:<ssid>;T:WPA;P:<password>;;`.
- [ ] AC8. Downward pull from settings root or any settings detail does not open
      the quick panel.
- [ ] AC9. Downward pull from dashboard/home and horizontal dashboard pages
      still opens the quick panel.
- [ ] AC10. Long-pressing Bluetooth/hotspot/Wi-Fi in the quick panel visibly
      shrinks the tile, draws a colored progress ring around it, and navigates
      to the matching settings detail page only after the ring completes.
- [ ] AC11. Releasing before progress completes cancels navigation and does not
      fire the normal click/toggle action.
- [ ] AC12. Existing tap active-state behavior for Bluetooth/hotspot/Wi-Fi
      remains unchanged.
- [ ] AC13. Preview PNGs are generated and visually inspected for settings root,
      Wi-Fi flow, hotspot QR, and quick-panel long-press progress.
- [ ] AC14. `git diff --check`, `./scripts/esp-idf-env.sh build`, and
      `node .gitnexus/run.cjs detect-changes -r esp32BMSGPS` complete before
      implementation is reported done.
- [ ] AC15. Hardware validation flashes through the RFC2217 bridge and confirms
      no boot panic/WDT after Wi-Fi, HTTP, NimBLE, touch, and LVGL are active.

## Out Of Scope

- Changing the required Setup AP prefix away from `fuckingBms_`; that is a
  project-level contract.
- Adding a second local Web UI asset or separate Web UI flow.
- Replacing the existing BMS BLE scan/bind workflow.
- Implementing OTA transport itself if it is not already present; this task
  only moves OTA settings placement.
- Hidden/manual SSID entry from the TFT. Users can still configure unusual
  networks through the existing Web UI if needed.

## Open Questions

- None.
