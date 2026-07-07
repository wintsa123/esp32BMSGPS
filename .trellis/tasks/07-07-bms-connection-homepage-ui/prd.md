# BMS Connection Validation And BMS Homepage UI

## Goal

Validate the existing ANT BMS BLE connection path on hardware and turn the
first TFT screen into a BMS-focused homepage based on the approved roadmap.
The milestone must preserve the known-good ESP-IDF boot, backlight, LVGL, and
Setup AP recovery baseline while adding only real BMS telemetry or explicitly
invalid/offline states.

## Background

- Parent roadmap: `.trellis/tasks/07-07-firmware-feature-roadmap/`.
- Current runtime snapshot type is
  `components/esp_bms_lvgl_ui/include/esp_bms_lvgl_ui.h:52`
  `esp_bms_dashboard_snapshot_t`.
- BMS runtime already has ANT scan, candidate listing, binding persistence,
  connect, service/characteristic/CCCD discovery, notification subscription,
  poll writes, frame buffering, and basic status-frame parsing.
- Current telemetry clearing is in
  `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:390`
  `runtime_clear_bms_telemetry`.
- Current ANT status parsing is in
  `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:488`
  `runtime_apply_bms_status_frame`.
- Current HTTP status JSON is in
  `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:1571`
  `runtime_http_status_handler`.
- Current TFT dashboard rendering is in
  `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:361`
  `set_dashboard`; current layout creation is in the same file around
  `build_screen`.
- The current TFT font path is ASCII-only for this milestone. TFT labels and
  fault/status indicators must stay compact ASCII such as `SOC`, `AH`, `MAX`,
  `MIN`, `AVG`, `DIFF`, `PROT`, `WARN`, `INFO`, `T1`, `BAL`, and `MOS`.
- The homepage must not include rotation or BMS connection controls. Those
  remain settings work for a later child task.

## Requirements

- R1. Preserve the working pure ESP-IDF firmware workflow; do not reintroduce
  Rust/Cargo firmware dependencies.
- R2. Preserve boot to visible display, enabled backlight, and reachable Setup
  AP throughout the milestone.
- R3. Keep `main/idf_main.c` as boot orchestration only; runtime, TFT UI, and
  bridge logic must remain in their existing components.
- R4. Extend the runtime snapshot contract instead of inventing a separate UI
  data path.
- R5. BMS telemetry must never be synthesized. Fields stay invalid/offline
  until parsed from valid ANT BMS BLE notifications.
- R6. Hardware validation must cover scan, bind, connect, service discovery,
  characteristic discovery, CCCD subscription, poll write, and notification
  parsing on a real ANT BMS when hardware is available.
- R7. Split BMS state into separate concepts:
  - protection codes reported by BMS protection bits,
  - warning codes reported by BMS warning bits,
  - info/connection/runtime state such as scan, connect, no service, no
    characteristic, no CCCD, RX, poll, and OK.
- R8. Protection has higher display priority than warning; warning has higher
  priority than info; OK is the fallback.
- R9. Unknown protection or warning bits must remain visible as compact hex or
  unknown codes instead of being discarded.
- R10. Add temperature snapshot fields needed by the homepage: `T1`-`T4`,
  `BAL`, and `MOS` where protocol data is available. Missing values must render
  as invalid placeholders.
- R11. Redesign the first TFT page as a BMS homepage:
  - large SOC tile with percentage and `remaining/total Ah` below it,
  - prominent pack voltage and current tile,
  - cell stats tile with max/min/diff/avg,
  - lower protection/warning/info area,
  - temperature area.
- R12. Keep the TFT homepage legible on the actual 240 x 320 portrait layout
  and existing landscape/inverted rotations. Text must not overlap.
- R13. Update Web status/config output only as needed to expose full
  protection/warning information and preserve existing API compatibility.
- R14. Do not expose setup AP passwords, external Wi-Fi passwords, or other
  secrets in logs while touching nearby HTTP/runtime code.
- R15. GitNexus impact analysis must be run before editing each function,
  method, class, or public type, and `detect-changes` must run before commit.

## Out Of Scope

- Full settings UI redesign.
- Moving rotation or BMS connection controls into a polished settings page.
- GPS speed freshness, 0-100 timing, or map behavior.
- OTA implementation.
- Introducing Chinese TFT fonts or free-form Chinese TFT BMS fault text.

## Acceptance Criteria

- [ ] `./scripts/esp-idf-env.sh build` succeeds.
- [ ] The device still boots to a visible TFT UI with backlight enabled.
- [ ] Setup AP still starts with the required `fuckingBms_` SSID policy and
      eight-digit password policy, and remains reachable.
- [ ] Binding a real ANT BMS can progress through scan, connect, GATT
      discovery, CCCD subscription, poll write, and notification parsing, or
      each failing stage is visible as an info/error state.
- [ ] The runtime snapshot exposes separate BMS protection, warning, and info
      state.
- [ ] Unknown BMS fault bits are preserved in visible compact codes.
- [ ] The TFT homepage shows SOC with Ah capacity in the same tile, pack
      voltage/current, cell stats, highest-priority BMS protection/warning/info,
      and temperature placeholders or values.
- [ ] The Web UI/status API exposes full active BMS protection and warning
      lists when available.
- [ ] Missing or disconnected BMS data renders invalid/offline state rather
      than stale or fabricated telemetry.
- [ ] No homepage control duplicates the later settings work for rotation or
      BMS connection management.
