# Firmware Feature Roadmap Implementation Plan

## Preconditions

- Do not start implementation from the parent task. Create/start a child task
  for the next independently verifiable deliverable.
- Before editing any function/class/method, run GitNexus impact analysis:

```bash
node .gitnexus/run.cjs impact <symbol> -r esp32BMSGPS --direction upstream
```

- For ambiguous symbols, disambiguate with `--file` or `--uid`.
- Warn before editing if impact returns HIGH or CRITICAL.
- After changes and before commit, run:

```bash
node .gitnexus/run.cjs detect-changes -r esp32BMSGPS --scope all
```

## Child Task Order

### 1. BMS Connection Validation And BMS Homepage UI

Purpose:

- Make the first screen a BMS-focused homepage using the agreed layout.
- Validate the existing ANT BMS connection/notification path on hardware.
- Add separate protection/warning/info concepts.

Planned work:

- Verify live ANT BMS scan, bind, connect, service/characteristic discovery,
  CCCD subscription, poll write, and notification parsing.
- Extend runtime snapshot for:
  - BMS protection codes.
  - BMS warning codes.
  - BMS info/connection state.
  - Temperature fields needed for `T1`-`T4`, `BAL`, and `MOS`.
- Map ANT BMS protection/warning bits into fixed ASCII codes.
- Preserve unknown fault bits as visible hex/unknown codes for debugging.
- Redesign the TFT BMS homepage:
  - SOC tile with percent and Ah below it.
  - Voltage/current tile.
  - Cell stats tile.
  - Lower protection/warning/info area.
  - Temperature area.
- Update Web UI status/config output only as needed for full protection/warning
  list display.

Validation:

```bash
./scripts/esp-idf-env.sh build
```

Hardware checks:

- Device boots to visible BMS homepage.
- Backlight stays on.
- Setup AP remains reachable.
- Binding a real ANT BMS produces real telemetry.
- BMS protection/warning display does not synthesize data.
- BMS disconnect or missing service/characteristic stays visible as info/error.

Rollback point:

- Keep a buildable snapshot before layout rewrite.
- If BMS BLE destabilizes boot, keep homepage rendering but disable BLE changes
  behind a narrow guard until the connection issue is fixed.

### 2. Settings UI And Detail Polish

Purpose:

- Make settings coherent after the BMS homepage redesign.
- Move rotation and BMS connection controls out of the homepage and into
  settings.

Planned work:

- Redesign TFT settings page with compact rows:
  - Rotation dropdown-style row.
  - Brightness control.
  - Speed unit.
  - Language `ZH` / `EN`.
  - Setup AP / QR access.
  - BMS connect/reconnect/rebind entry.
  - Restore defaults.
- Add icon-like labels or LVGL symbols where practical.
- Keep Web UI as the detailed place for scan candidates and Chinese help text.
- Check portrait/landscape/inverted rotations for text fit and touch mapping.

Validation:

```bash
./scripts/esp-idf-env.sh build
```

Hardware checks:

- Rotation changes keep panel, LVGL resolution, and touch mapping in sync.
- Settings text does not overlap in all supported rotations.
- BMS connection entry does not require entering long MAC text on TFT.

Rollback point:

- Keep old settings actions functional until the replacement page is verified.

### 3. GPS Speed, 0-100 Timing, And Map

Purpose:

- Restore reliable GPS use and add timing/map features without breaking logs or
  flashing workflows.

Planned work:

- Resolve GPS UART0 vs console conflict:
  - choose hardware pin move, debug-vs-product build switch, or separate debug
    transport.
- Improve GPS validity tracking:
  - fresh fix timestamp.
  - stale fix timeout.
  - sentence/error counters.
- Add 0-100 timing state:
  - idle/armed/running/done/invalid.
  - start threshold.
  - stop threshold.
  - reset behavior.
  - last/best result.
- Decide map ownership before implementation:
  - recommended: detailed map in Web/phone UI, compact summary on TFT.

Validation:

```bash
./scripts/esp-idf-env.sh build
```

Hardware checks:

- GPS can be used without losing required debug/recovery path.
- Speed is invalid when GPS fix is stale.
- 0-100 result is not produced from stale/invalid GPS data.

Rollback point:

- Keep GPS parsing isolated from BMS and setup AP work.

### 4. Password, Security Polish, And OTA

Purpose:

- Finish sensitive configuration and update flow last, after the primary field
  UI and telemetry are stable.

Planned password work:

- Preserve setup AP SSID and password policy.
- Improve Web UI auth prompt/session handling.
- Keep password changes synchronized with QR payload and active SoftAP config.
- Avoid logging secrets.

Planned OTA work:

- Define manifest schema.
- Implement check/start through ESP-IDF OTA APIs.
- Verify hash/size before boot switch.
- Mark app valid after successful boot.
- Handle failure/rollback state in snapshot and Web UI.

Validation:

```bash
./scripts/esp-idf-env.sh build
```

Hardware checks:

- Password change does not lock out setup AP recovery.
- OTA image fits the active OTA slot.
- OTA rollback path is tested.

Rollback point:

- Keep OTA disabled/unavailable until the full check/download/verify/switch
  path is implemented safely.

## Parent Integration Checks

Before considering the roadmap done:

- Child tasks have completed their own build and hardware validation.
- BMS, GPS, settings, password, and OTA changes still preserve boot/display.
- Web UI and TFT present consistent status for BMS, GPS, Wi-Fi, and OTA.
- No fake telemetry, fake OTA success, or stale GPS result is displayed as real.
- README and specs are updated for any lasting workflow or hardware changes.
