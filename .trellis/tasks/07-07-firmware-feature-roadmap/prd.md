# Firmware Feature Roadmap

## Goal

Design the next firmware feature roadmap after the ESP-IDF display path has
booted successfully on hardware. The roadmap covers four independently
verifiable deliverables:

1. BMS connection and UI.
2. Overall settings UI and detail polish.
3. Password and OTA functionality, intentionally last.
4. GPS speed, 0-100 acceleration timing, and map functionality.

The parent task owns the shared requirements, ordering, product decisions, and
integration acceptance criteria. Implementation should happen in child tasks
after the design is reviewed.

## Confirmed Facts

- The target firmware is a pure ESP-IDF CMake application.
- The display path now boots on the target hardware after fixing LEDC backlight
  initialization.
- `main/idf_main.c` is intended to remain boot orchestration only.
- Subsystem logic belongs under ESP-IDF components:
  - `esp_bms_idf_runtime`: runtime state, NVS, Wi-Fi, HTTP API, GPS, battery,
    BMS, and OTA orchestration.
  - `esp_bms_lvgl_bridge`: display, touch, LVGL adapter, brightness, rotation.
  - `esp_bms_lvgl_ui`: TFT dashboard/settings UI.
- The TFT display currently uses ASCII-capable fonts only. Until a separate font
  task approves broader fonts, TFT text should use ASCII labels such as `ZH`
  and `EN`.
- The embedded Web UI at `main/web/index.html` is the single local Web UI asset
  and already supports Chinese-first text with English selectable in device
  settings.
- Existing snapshot/UI contract fields include speed, GPS fix, BMS online,
  pack voltage, current, SOC, cell voltages, local battery, Wi-Fi state, OTA
  state, BMS error text, setup AP credentials, and setup AP QR payload.
- The user provided a BMS dashboard reference image on 2026-07-07. The desired
  homepage starts from that layout: a large SOC tile, large pack voltage/current
  on the right, max/min/difference/average cell voltage tile, a lower
  information/error area, and temperature tiles.
- The SOC tile should include both percentage and Ah capacity, with Ah shown
  under the percentage, for example `75%` plus `59.44/80.00Ah`.
- A generated visual concept was saved at
  `.trellis/tasks/07-07-firmware-feature-roadmap/assets/image_1783358764_0.png`.
  It is a style/reference artifact only; the actual LVGL implementation must be
  compressed for the real 240 x 320 TFT and current font constraints.
- The generated concept's bottom `ROTATE` / `BMS CONNECT` settings strip should
  not appear on the BMS homepage. Rotation and protection-board connection
  controls belong on the settings page.
- Current TFT pages are battery/dashboard and GPS, with a compact settings page
  using action buttons for setup AP, brightness, rotation, speed unit,
  language, BMS bind, and restore defaults.
- Current Web UI already calls:
  - `GET /api/status`
  - `GET /api/config`
  - `GET /api/bms/candidates`
  - `POST /api/config`
  - `POST /api/wifi`
  - `POST /api/ap-password`
  - `POST /api/bms/scan`
  - `POST /api/bms/bind`
  - `POST /api/ota/check`
  - `POST /api/ota/start`
- Existing BMS runtime work includes NimBLE scanning, candidate listing,
  binding persistence, connection flow for service `0xFFE0` and characteristic
  `0xFFE1`, notification subscription, poll writes, and status-frame parsing
  into the dashboard snapshot. Hardware validation with a real ANT BMS is still
  pending.
- The current BMS snapshot and UI already cover SOC, pack voltage, current,
  total/remaining capacity, min/average/max cell voltage, and cell delta. The
  reference image also needs temperature fields such as `T1`-`T4`, balance or
  board temperature, and MOS temperature; those are not yet part of the snapshot
  contract.
- Current runtime status text is a short `bms_error_text` string and includes
  connection/runtime states such as `BMS OK`, `BMS OFF`, `BMS CONN`,
  `BMS NO SVC`, `BMS RX`, and `BMS POLL`. True protection-board faults should
  be modeled as fixed fault/status codes once the ANT status frame fault bits
  are identified.
- ANT BMS exposes both protection information and warning information. These
  should be modeled separately instead of flattening everything into one generic
  `BMS ERR` string.
- Existing GPS runtime work parses NMEA RMC speed/fix from GPS at 9600 baud.
- GPS is currently documented as wired to UART0 GPIO1/GPIO3, which conflicts
  with flashing and logs. The latest field fix preserves the console by
  disabling GPS when GPS UART equals the ESP-IDF console UART.
- OTA endpoints currently return explicit unavailable responses; real manifest
  parsing, download, verification, partition switch, and post-boot validity
  marking remain pending.
- Setup AP constraints remain mandatory:
  - SSID is `fuckingBms_` plus six lowercase hexadecimal characters.
  - Password is eight random digits.
  - QR and visible credentials must match the active SoftAP configuration.
- Secrets must not be logged: setup AP passwords, external Wi-Fi passwords, OTA
  credentials, token-bearing URLs, or raw HTTP request bodies.
- Primary validation after firmware changes is:

```bash
./scripts/esp-idf-env.sh build
```

- GitNexus impact analysis is required before editing any function/class/method,
  and `detect-changes` is required before commit.

## Requirements

- R1. Preserve the working boot/display/setup AP baseline at every milestone.
- R2. Keep the ESP-IDF CMake path as the only target firmware workflow; do not
  reintroduce Rust/Cargo firmware dependencies.
- R3. Use the runtime snapshot as the contract between hardware/runtime state,
  TFT UI, and Web status.
- R4. Keep complex configuration and long-form text primarily in Web UI unless a
  TFT interaction is necessary for in-vehicle use.
- R5. Keep TFT driving screens glanceable, ASCII-safe, and usable on 240 x 320.
- R6. BMS work must never synthesize telemetry. BMS fields stay offline/invalid
  until validated BLE notifications from the bound device update the snapshot.
- R7. BMS UI must support scan, candidate selection, bind/unbind or rebind,
  connection status, telemetry status, and useful failure text.
- R8. The BMS homepage must follow the user's reference layout first: large SOC,
  prominent voltage/current, cell voltage stats, lower information/error area,
  and temperatures. Ah capacity should be merged into the SOC tile below the
  percentage rather than consuming a separate card. Because the current TFT font
  is ASCII-only, labels should initially use ASCII text such as `AH`, `MAX`,
  `MIN`, `AVG`, `DIFF`, `ERR`, `T1`, and `MOS` until a font task approves
  Chinese glyphs.
- R9. Settings UI work must unify display, language, speed unit, setup AP,
  external Wi-Fi, BMS binding, and restore/defaults behavior without breaking
  current Web API contracts.
- R10. Settings and screen rotation interactions should use dropdown-style
  controls with icons where practical, instead of only plain text action rows.
- R11. Protection-board connection must be configurable from settings. Web UI
  should own candidate lists and detailed forms; TFT settings should provide a
  compact BMS connect/rebind entry suitable for the device screen.
- R12. TFT BMS error display should avoid requiring a general Chinese font in
  the first implementation. The TFT should show compact ASCII fault/status
  codes from a fixed code table, while Web UI can show longer Chinese
  explanations. Free-form Chinese BMS fault text should not be required for the
  first TFT implementation.
- R13. BMS protection and warning state must be separate runtime/snapshot
  concepts. Protection has higher display priority than warning. The TFT
  homepage should show the highest-priority active protection/warning code in
  the lower information area, while Web UI should expose the full active
  protection and warning lists with Chinese explanations.
- R14. Password work must keep the existing setup AP credential policy and must
  improve authentication UX without exposing secrets in logs or URLs.
- R15. OTA work must use ESP-IDF OTA APIs and verify image size, manifest
  integrity, download integrity, partition switch, and post-boot validity.
- R16. GPS work must resolve the UART0 console/GPS conflict before relying on
  on-device GPS for speed, 0-100 timing, or map behavior.
- R17. 0-100 timing must define clear start/stop/reset rules and avoid reporting
  a valid result from stale or invalid GPS data.
- R18. Map functionality must be designed around ESP32 constraints: no PSRAM,
  4 MB flash, limited TFT text/font support, and intermittent setup AP/local
  network availability.
- R19. Password and OTA are intentionally later than BMS, settings polish, and
  GPS/mapping design.

## Proposed Task Map

- Child 1: BMS connection validation and BMS UI.
- Child 2: Settings UI and detail polish.
- Child 3: GPS speed, 0-100 timing, and map design/implementation.
- Child 4: Password, security polish, and OTA.
- Parent: cross-feature design review, ordering, and final integration
  acceptance.

## Acceptance Criteria

- [ ] Final planning includes `design.md` and `implement.md` before any child
      implementation task is started.
- [ ] Child tasks are created for independently verifiable deliverables rather
      than implementing all four areas in one task.
- [ ] Each child task has testable hardware and build acceptance criteria.
- [ ] The first implementation child preserves the current boot/display/setup AP
      baseline after flashing.
- [ ] The roadmap explicitly documents how GPS logging/flashing conflicts are
      resolved before enabling GPS-dependent features.
- [ ] The roadmap explicitly documents which UI surface owns each workflow:
      TFT, local Web UI, or both.
- [ ] The roadmap keeps password and OTA after the earlier field-facing
      functionality unless the user explicitly changes priority.

## Open Questions

- OQ1. Which map ownership model should the product use: Web/phone-hosted map
  as the primary detailed map, with TFT showing compact navigation/speed/timing
  summaries; or an on-device TFT map despite ESP32 flash/RAM constraints?
