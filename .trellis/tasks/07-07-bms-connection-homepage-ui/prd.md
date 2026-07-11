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
- R16. Preserve the current homepage geometry and information hierarchy. The
  reference draft is a visual-detail reference, not a request to copy its
  oversized card proportions.
- R17. Keep Ah capacity in the upper SOC area and keep the former capacity
  area dedicated to BMS protection, warning, and connection/runtime info.
- R18. Do not add the draft's bottom rotation/BMS-connect control row to the
  homepage; those controls remain in device settings.
- R19. Refine the homepage details to include:
  - a battery icon in the SOC area,
  - a horizontal divider between pack voltage and current,
  - horizontal dividers between the cell-stat rows,
  - a thermometer icon for each temperature channel.
- R20. Match the reference's visual hierarchy with a black background, blue
  SOC emphasis, white primary values, muted-gray captions and borders, and
  mint-green secondary telemetry. Preserve red/amber fault severity colors.
- R21. The visual refinement must not change the dashboard snapshot contract,
  BMS parsing, fault priority, or telemetry validity behavior.
- R22. CCCD discovery must start from the characteristic value handle so
  NimBLE searches the following descriptor handle instead of skipping it.
- R23. Successful CCCD subscription must transition BMS state to online and
  immediately send the ANT status query. While online, status polling must run
  every 500 ms; notifications must update real dashboard telemetry and emit
  bounded diagnostic logs for each stage.
- R24. Binding a scan candidate must persist its complete advertised name with
  the MAC. Later named advertisements may refresh that cache, while nameless
  advertisements and list refreshes must retain the last name seen for the same
  MAC instead of temporarily degrading it to an anonymous device label.
- R25. The dashboard Bluetooth subtitle must show the bound device name when
  known, fall back to the existing anonymous device label when unknown, and
  never expose the MAC as the display name.
- R26. A successfully consumed Web `/api/config` save must apply all display
  settings under the LVGL lock and return to the BMS homepage once, even when
  submitted values equal the current values. Local TFT setting actions must
  keep their current navigation behavior.
- R27. Tapping a BLE scan candidate must show a confirmation prompt before any
  bind action is queued. Anonymous candidates must remain identified as a
  generic device and must not expose their MAC in the prompt.
- R28. After confirmation, show a persistent connecting toast with a rotating
  indicator. Replace it with the existing `绑定成功` toast only after the BMS
  snapshot transitions online; stop it on connection failure or scan end.
- R29. Repeated scan requests while NimBLE discovery is already active must
  reuse the current scan instead of canceling and immediately restarting it,
  which can race and surface the misleading `BLE FAIL` state.
- R30. BMS BLE discovery must remain stopped during boot and normal dashboard
  use. It starts only when the user opens the BMS candidate list or explicitly
  refreshes that list; a previously bound MAC must not trigger a boot scan.
- R31. Candidate row identity must remain stable for the duration of a scan.
  Once a row is assigned to a MAC, no other MAC may reuse that array slot until
  the user explicitly refreshes and starts a new candidate list.

## Out Of Scope

- Full settings UI redesign.
- Moving rotation or BMS connection controls into a polished settings page.
- GPS speed freshness, 0-100 timing, or map behavior.
- OTA implementation.
- Introducing Chinese TFT fonts or free-form Chinese TFT BMS fault text.

## Acceptance Criteria

- [x] `./scripts/esp-idf-env.sh build` succeeds.
- [ ] The device still boots to a visible TFT UI with backlight enabled.
- [ ] Setup AP still starts with the required `fuckingBms_` SSID policy and
      eight-digit password policy, and remains reachable.
- [x] Binding a real ANT BMS can progress through scan, connect, GATT
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
- [x] Portrait and landscape previews preserve the current panel geometry and
      show the refined battery icon, separators, thermometer icons, and color
      hierarchy without clipped or overlapping text.
- [x] Ah capacity remains in the SOC area and the former capacity area remains
      the BMS protection/warning/info area.
- [x] The visual-only refinement does not modify the snapshot ABI or runtime
      BMS parsing/update paths.
- [x] The CCCD immediately following the characteristic value handle is
      discovered, subscription succeeds, and the first ANT status query is
      sent. Hardware validation used value handle 16 and CCCD handle 17.
- [ ] The bound BMS name survives rescans without names and is shown in the
      dashboard Bluetooth subtitle when available.
- [ ] Any scan candidate that has shown a name keeps that name across list
      refreshes and later nameless advertisements for the same MAC.
- [ ] A named candidate row never changes into `设备 N` because a different
      anonymous MAC replaced its array slot during the same scan.
- [ ] A candidate row never changes to another Bluetooth device name during the
      same scan; a full list ignores additional MACs until explicit refresh.
- [ ] Saving Web config applies settings and returns to the BMS homepage once,
      including an unchanged-value save; local TFT settings do not auto-return.
- [ ] Selecting a BLE candidate requires confirmation, shows a connecting
      spinner after confirmation, and shows `绑定成功` only after online.
- [ ] Repeated scan/refresh taps do not intermittently show `BLE FAIL` while a
      NimBLE discovery procedure is already active.
- [ ] Boot and dashboard use do not start BMS discovery or populate a stale
      `found N` count; opening or refreshing the BMS list starts discovery.
