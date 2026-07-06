# Pure ESP-IDF Migration Plan

## Preconditions

- Before editing each function/class/method, run GitNexus impact analysis:

```bash
node .gitnexus/run.cjs impact -r esp32BMSGPS --direction upstream <symbol>
```

- For ambiguous symbols, use `-u` or `-f` to disambiguate.
- Warn before editing if impact returns HIGH or CRITICAL risk.

## Milestones

1. Baseline and scope check
   - Run `./scripts/esp-idf-env.sh build`.
   - Capture current unresolved IDF route/function gaps.
   - Verify current IDF app boots at least to display/setup AP when hardware is
     available.

2. Web API parity foundation
   - Move current ad hoc route handling into a clearer runtime/API boundary if
     needed.
   - Implement `/api/wifi` request validation and persistence.
   - Add implemented placeholder-safe OTA/BMS states only where needed to keep
     Web UI responses consistent while deeper subsystems are added.
   - Verify Web UI no longer receives `501` for expected routes.

3. External Wi-Fi station support
   - Persist external SSID/password in NVS without logging passwords.
   - Start AP+STA when credentials exist.
   - Report station state through runtime snapshot and `/api/status`.
   - Preserve setup AP availability for recovery.

4. BMS BLE support
   - Implement ANT BMS frame parsing and candidate model in C or a focused IDF
     component.
   - Implement BLE scan and `/api/bms/candidates`.
   - Persist and load bound MAC.
   - Connect/read bound BMS telemetry and update the dashboard snapshot.

5. OTA support
   - Port manifest/version/hash validation semantics.
   - Implement `/api/ota/check`.
   - Implement `/api/ota/start` using ESP-IDF OTA APIs.
   - Verify download, hash/size validation, partition selection, and reboot/apply
     behavior.

6. Test replacement
   - Identify product contracts formerly protected by the deleted test suite.
   - Port high-value policy/parser coverage to C/component tests where
     practical.
   - Do not keep Rust tests in the normal validation path after cleanup.

7. Rust/Cargo cleanup
   - Remove Cargo from normal target build/flash flow.
   - Update README and helper scripts.
   - Remove legacy Rust source and Cargo build metadata from the firmware path.
   - Run GitNexus `detect-changes` before commit.

## Validation Commands

```bash
./scripts/esp-idf-env.sh build
node .gitnexus/run.cjs detect-changes -r esp32BMSGPS
```

Cargo/Rust checks are intentionally removed from the normal validation path.

## Hardware Validation

- TFT first screen appears before network startup failures can leave a blank
  display.
- Setup AP appears in phone Wi-Fi scan.
- Phone joins setup AP with displayed password.
- Browser loads `http://192.168.4.1/`.
- Web UI can save device settings, AP password, external Wi-Fi, and BMS binding.
- GPS speed updates when valid RMC data is present.
- Local battery voltage updates periodically.
- BMS candidates appear during scan and telemetry updates after bind.
- OTA check/start flow reaches expected state on a test manifest.

## Decisions

- Rust/Cargo source and firmware build metadata are deleted first by explicit
  user request. Remaining behavior is rebuilt on ESP-IDF/C.

## Progress

- External Wi-Fi persistence and `/api/wifi` have been moved into the ESP-IDF
  runtime: credentials are stored in NVS, applied from the main runtime loop,
  and start AP+STA while preserving the setup AP.
- `/api/ota/check` and `/api/ota/start` now return explicit unavailable JSON
  responses instead of falling through to `501`; real OTA transport remains a
  pending milestone.
- NimBLE-backed BMS scanning is wired into `/api/bms/scan`, LVGL BMS bind
  actions, and `/api/bms/candidates`. Candidates are deduplicated by MAC,
  filtered for ANT-style advertisements or the bound MAC, and bounded to six
  entries.
- Bound BMS connection and telemetry are now wired on the ESP-IDF path: a saved
  or newly bound MAC triggers a scan, matching advertisements start a NimBLE
  GATT connection, service `0xFFE0` / characteristic `0xFFE1` is discovered,
  CCCD notifications are enabled, request frames are written, and valid ANT
  status frames update the dashboard snapshot. Hardware validation is still
  pending.
- `./scripts/esp-idf-env.sh build` passes after enabling BMS connection and
  telemetry parsing; generated app size is `0x125670` against the `0x1e0000`
  OTA slot.
