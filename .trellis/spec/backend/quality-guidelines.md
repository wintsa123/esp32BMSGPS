# Quality Guidelines

> Code quality standards for backend development.

---

## Overview

<!--
Document your project's quality standards here.

Questions to answer:
- What patterns are forbidden?
- What linting rules do you enforce?
- What are your testing requirements?
- What code review standards apply?
-->

(To be filled by the team)

---

## Forbidden Patterns

<!-- Patterns that should never be used and why -->

(To be filled by the team)

---

## Required Patterns

<!-- Patterns that must always be used -->

(To be filled by the team)

---

## Testing Requirements

### Scenario: ESP32 Setup AP Target Transport Contract

#### 1. Scope / Trigger

- Trigger: target firmware displays the first-boot Wi-Fi QR screen and must also
  start the actual ESP32 setup AP that a phone can discover.
- Applies to `Cargo.toml`, `src/main.rs`, `src/wireless_wifi.rs`,
  `src/wifi_control.rs`, `src/provisioning.rs`, and `src/display.rs`.

#### 2. Signatures

- Target heap/RTOS setup is owned by `main::init_wireless_heap()` plus
  `esp_rtos::start(timg0.timer0, software_interrupt.software_interrupt0)`.
- Runtime config projection is
  `wifi_control::desired_runtime_config(settings) -> WifiRuntimeConfig`.
- Target driver projection is
  `wireless_wifi::esp_radio_config(config) -> Option<esp_radio::wifi::Config>`.
- Target AP owner is
  `wireless_wifi::WifiRuntime::start(wifi, config) -> Result<Option<WifiRuntime>, WifiError>`.
- Reconfiguration is
  `WifiRuntime::apply_config(config) -> Result<bool, WifiError>`.
- AP network polling is `WifiRuntime::poll()`, called from the main loop while
  the runtime is alive.
- Setup AP DHCP packet logic lives in `dhcp_server::{parse_request,
  write_reply}` and is host-testable.
- `main::apply_target_wifi_config(app_state, wifi_peripheral, wifi_runtime)`
  bridges settings state to target radio ownership.

#### 3. Contracts

- Displaying a QR code is not sufficient. The target build must initialize the
  Wi-Fi heap, start `esp-rtos`, create an `esp-radio` Wi-Fi controller, and call
  `set_config` either through `wifi::new(...with_initial_config(...))` or
  `WifiRuntime::apply_config`.
- Initialize the TFT and draw the first visible screen before starting the
  Wi-Fi runtime. Radio/network allocation or startup failures must not leave the
  user staring at an uninitialized white TFT.
- The default TFT controller fallback for this hardware contract is ST7789.
  Runtime RDDID detection may select ILI9341 when the panel reports a matching
  ID; only change `board::tft::CONTROLLER` when verified hardware proves the
  fallback is wrong.
- When RDDID is unavailable or unrecognized during normal boot, the target
  firmware must stay on `board::tft::CONTROLLER` and
  `DeviceSettings.display_rotation`. Auto-probing ST7789/ILI9341, rotations,
  or inversion is a manual bring-up aid only and must stay behind
  `board::tft::AUTO_PROBE_ON_RDDID_MISS`.
- Display drawing, touch mapping, and touch calibration must use the same
  active display rotation source. Do not let a diagnostic probe rotation drift
  away from the rotation used for touch hit testing.
- During white-screen bring-up, each init/probe should issue a raw full-screen
  `RAMWR` color fill before windowed `CASET`/`RASET` drawing. If raw fill is
  visible but settings are not, focus next on window offsets and MADCTL.
- The `ili9341-tft` Cargo feature exists only to build an alternate ILI9341
  fallback image for hardware validation when RDDID/MISO is unavailable.
- Because this panel has no controllable TFT reset pin, wait
  `board::tft::POWER_ON_DELAY_MS` before the first RDDID read or init command.
- Keep that wait conservative during bring-up; a 1s wait is acceptable because
  it is cheaper than missing the first init commands on a no-RST TFT.
- The ST7789 init path must issue `SLPOUT`, wait, then write panel power/gamma
  setup before `DISPON`; sleep-out plus color-mode alone is not enough for
  every TPM408 module variant.
- Do not enable ST7789 display inversion by default. On this TFT path the UI
  draws black backgrounds; `INVON` makes a healthy screen look like a white
  boot failure.
- Keep `WifiRuntime` alive for as long as Wi-Fi should stay on. Dropping
  `WifiController` stops/deinitializes Wi-Fi.
- Do not consume the `WIFI` peripheral when
  `desired_runtime_config(...).mode == DesiredWifiMode::Off`; keep it available
  so touchscreen reprovisioning can start the setup AP later.
- Default firmware features include Wi-Fi setup AP transport. BLE/coexistence
  target radio features stay behind `ble-radio` until the real BLE adapter and
  native link behavior are validated.
- The setup AP QR payload must use the same `setup_ap_ssid` and
  `setup_ap_password` passed to `AccessPointConfig`.
- The setup AP QR payload must keep the phone-tested field order and flags:
  `WIFI:S:<escaped ssid>;T:WPA;P:<escaped password>;;`. The target AP transport
  uses `AuthenticationMethod::Wpa2Personal`; do not encode the QR as WPA3/SAE or
  as hidden unless the AP config changes to match.
- Starting the AP radio is not the same as providing an IPv4 setup network. If
  a phone accepts the password but stays on "connecting" or "obtaining IP",
  verify whether the target firmware is running a DHCP/static-IP network stack
  for the AP interface before treating it as a password/auth failure.
- Default firmware features must include the `net` feature so the setup AP gets
  an IPv4 stack in addition to the Wi-Fi radio.
- Setup AP IPv4 defaults are fixed for the local provisioning MVP:
  - ESP32 AP/server/router/DNS IP: `192.168.4.1/24`.
  - DHCP lease offered to the phone: `192.168.4.2`.
  - DHCP server listens on UDP/67 and replies to UDP/68.
- `WifiRuntime::poll()` must continue polling both the AP network runner and
  the DHCP socket from the main loop; configuring `embassy-net` resources
  without polling the runner is equivalent to no IP network.
- Current setup AP SSIDs must be generated as `fuckingBms_` plus a six-character
  random hexadecimal suffix. The setup AP password must be eight random digits.
- `main::ensure_first_boot_provisioning` must migrate old persisted SSID or
  password values that do not match the current policy, then save settings so
  the TFT QR screen shows the current credentials.

#### 4. Validation & Error Matrix

- QR screen visible but no `WifiRuntime` started -> phone cannot see AP.
- Backlight turns on before TFT init or Wi-Fi starts before the first screen is
  drawn -> target may look like a white-screen boot failure.
- Sending TFT commands immediately after ESP boot on a no-RST panel -> the
  display may still be powering up and remain white.
- `board::tft::CONTROLLER == Ili9341` on the documented ST7789 TPM408 path ->
  controller-specific power/gamma commands may leave the panel white.
- RDDID reads as all `00` or `ff` -> treat it as an SPI/MISO/CS/DC/connection
  diagnostic; do not auto-switch away from the ST7789 fallback.
- Unknown RDDID with a white TFT -> use the target auto-probe logs and watch
  whether any controller/rotation/inversion phase draws the settings screen
  before changing pins.
- Auto-probe phases should draw a full-screen color diagnostic before returning
  to the settings screen, so visibility does not depend on tiny text.
- ST7789 init omits power/gamma commands -> some modules may accept SPI traffic
  but never render the boot/settings screen.
- `board::tft::INVERT_COLORS == true` while UI clears to black -> the rendered
  screen appears white even though SPI drawing succeeded.
- `esp_rtos::start` missing before `wifi::new` -> radio runtime is invalid for
  `esp-radio`.
- `WifiController` stored in a local temporary and dropped -> AP starts then
  stops immediately.
- `DesiredWifiMode::Off` consumes `WIFI` -> later reprovisioning cannot start
  AP in the same boot.
- Default build pulls `ble`/`coex` before BLE transport validation -> release
  link may fail on native BT/coexistence symbols.
- QR payload uses `T:SAE`, `H:true`, or a different SSID/password from
  `AccessPointConfig` -> phone may decode the QR but fail to join the setup AP.
- AP radio is up but no DHCP/static IPv4 setup network is running -> phone may
  accept the password yet remain in "connecting" / "obtaining IP".
- AP IPv4 stack exists but `WifiRuntime::poll()` is not called -> DHCP requests
  are not consumed and the phone remains stuck obtaining IP.
- DHCP parser ignores malformed or unsupported DHCP message types; the phone
  should retry instead of the firmware panicking.
- Persisted `BMS-GPS-*` SSID or non-eight-digit password remains accepted
  forever -> the TFT keeps showing stale setup credentials after a firmware
  update.
- RDDID reads as all `00` or `ff` during normal boot -> keep the documented
  fallback controller/rotation, log the failed ID, and do not enter automatic
  portrait/landscape cycling unless `AUTO_PROBE_ON_RDDID_MISS` is explicitly
  enabled.
- XPT2046 `TOUCH_IRQ` stays high while pressure/raw samples are valid -> touch
  reads must still produce a tap; `read_raw_average()` and
  `wait_for_release()` must share the same effective touched predicate.

#### 5. Good/Base/Bad Cases

- Good: first boot generates SSID/password, starts `WifiRuntime`, renders a QR
  with matching credentials, and `espflash save-image` fits the app slot.
- Base: host tests cover provisioning strings and Wi-Fi state projection; target
  `cargo +esp check` and release build compile the radio path.
- Bad: UI sets `WifiLinkState::SetupApOnly` and draws a QR without starting or
  retaining the target Wi-Fi controller.

#### 6. Tests Required

- Keep host tests for `provisioning::wifi_qr_payload` and
  `wifi_control::desired_runtime_config`.
- `provisioning::wifi_qr_payload` tests must assert the exact `S`-first,
  `T:WPA`, visible-network payload shape.
- `provisioning` tests must assert the `fuckingBms_` SSID policy and the
  eight-digit password policy.
- `dhcp_server` host tests must cover Discover -> Offer, Request -> Ack,
  invalid packet ignore, and small output buffer rejection.
- Run `cargo +esp check --bin esp32-bms-gps -j1` after target Wi-Fi changes.
- Run `cargo +esp check --bin esp32-bms-gps -j1` after target TFT controller or
  display-init changes.
- Run `cargo +esp clippy --bin esp32-bms-gps -j1 -- -D warnings` after target
  TFT, touch, or serial diagnostic changes.
- Run `cargo +esp check --bin esp32-bms-gps --features ili9341-tft -j1` when
  changing the TFT fallback feature.
- Run `cargo +esp build --release -j1` after Cargo feature or native radio
  dependency changes.
- Use `espflash save-image --chip esp32 --partition-table partitions.csv
  --target-app-partition factory ...` to check app image size against the 1 MB
  slot.
- Hardware validation must still verify phone scan and join of the TFT QR setup
  AP before marking the acceptance criterion complete.

#### 7. Wrong vs Correct

##### Wrong

```rust
let _initial_wifi_config = wireless_wifi::esp_radio_config(
    wifi_control::desired_runtime_config(&app_state.settings),
);
```

##### Correct

```rust
let mut wifi_runtime = None;
apply_target_wifi_config(&mut app_state, &mut wifi_peripheral, &mut wifi_runtime);
```

### Scenario: Device HTTP Status And OTA Control Contracts

#### 1. Scope / Trigger

- Trigger: firmware HTTP/API changes cross backend control logic, embedded web
  UI fields, and runtime hardware actions.
- Applies to `src/local_api.rs`, `src/http_api.rs`, `src/http_server.rs`,
  `src/runtime_effects.rs`, `src/ota_job.rs`, and `src/web/index.html`.

#### 2. Signatures

- `GET /api/status` is produced by
  `local_api::write_status_json(output, app_state, firmware_version)`.
- Browser-hosted control pages may call the device API from a public HTTPS
  origin. API requests must use the current setup AP password through either
  `X-Setup-Password` or HTTP Basic `Authorization`; preflight `OPTIONS` is
  allowed without authentication.
- HTTP-side OTA triggers are `POST /api/ota/check` and
  `POST /api/ota/start`.
- Runtime OTA coordination is handled through
  `runtime_effects::apply_ota_actions(app_state, ota_job, actions, manifest_url)`.
- `ota_job::OtaJobCommand` is the transport boundary:
  `FetchManifest { url }`, `DownloadFirmware(plan)`, `SwitchAndReboot`, or
  `None`.
- BMS scan candidates are produced by
  `local_api::write_bms_candidates_json(output, app_state)` and exposed at
  `GET /api/bms/candidates`.
- HTTP-side BMS scan trigger is `POST /api/bms/scan`, which only reports
  `HttpEffects { bms_scan_requested: true, ... }`.
- HTTP-side BMS bind trigger is `POST /api/bms/bind`, which stores the selected
  MAC and reports both settings persistence and BMS scan effects.

#### 3. Contracts

- `/api/status` fields currently consumed by the embedded web UI:
  - `version`: string
  - `speed`: string, decimal text or `--`
  - `speed_unit`: `km/h` or `mph`
  - `gps_fix`: boolean
  - `bms`: `online` or `offline`
  - `pack_voltage_mv`: number or `null`
  - `current_deci_amps`: number or `null`
  - `soc_percent`: number or `null`
  - `local_battery_mv`: number or `null`
  - `wifi`: string runtime state
  - `setup_ap_enabled`: boolean
  - `ota`: string runtime state
- `/api/config` fields currently consumed by the embedded web UI:
  - `brightness`: number, 10 through 100
  - `display_rotation`: `portrait`, `landscape`, `inverted_portrait`, or
    `inverted_landscape`
  - `speed_unit`: `km/h` or `mph`
  - `language`: `zh` or `en`; default is `zh`
  - `setup_ap_ssid`: string
  - `external_wifi_saved`: boolean
  - `external_ssid`: string
  - `setup_ap_password_saved`: boolean
  - `setup_ap_state`: string
  - `bms_mac`: string or `null`
- `POST /api/config` may update `language`; invalid values must return
  `ApiError::InvalidLanguage`.
- Cross-origin device API responses must include CORS and Private Network
  Access headers:
  - `Access-Control-Allow-Origin: *`
  - `Access-Control-Allow-Methods: GET, POST, OPTIONS`
  - `Access-Control-Allow-Headers: Content-Type, X-Setup-Password, Authorization`
  - `Access-Control-Max-Age: 600`
  - `Access-Control-Allow-Private-Network: true`
- Unauthorized API responses must return HTTP 401 with
  `WWW-Authenticate: Basic realm="esp32-bms-gps", charset="UTF-8"` so direct
  browser access can use the native password prompt.
- `GET /` and `OPTIONS` remain unauthenticated. All other API routes require
  the current setup AP password even when the request is not cross-origin.
- Browser-hosted control pages that fetch the local HTTP API from a public
  HTTPS origin must annotate local requests with `targetAddressSpace: "local"`
  when the browser supports Local Network Access, and may retry the older
  `targetAddressSpace: "private"` PNA value for rollout compatibility. Keep the
  older PNA response header too, because Chrome versions and flags differ during
  rollout.
- Web UI battery display must use `local_battery_mv` first and fall back to
  `pack_voltage_mv`; do not introduce a separate `battery` field unless the API
  is updated and tested.
- OTA job stores URLs and versions in `FixedAscii` buffers. Keep URLs ASCII and
  bounded by `MAX_OTA_URL_LEN`.
- `/api/bms/candidates` returns exactly
  `{ "candidates": [{ "mac": "...", "name": string|null, "rssi": number }] }`.
  Candidate storage is fixed-capacity and no-heap; do not allocate a dynamic
  vector for scan results on the device.
- When no BMS is bound, an `ANT-*` advertisement should be recorded as a
  candidate but should not auto-connect. Auto-connect only happens for the
  stored bound MAC.

#### 4. Validation & Error Matrix

- Missing or invalid JSON fields -> `ApiError::InvalidJson` or a specific
  validation error.
- API request without the current setup password -> HTTP 401 and no settings
  mutation.
- PNA/CORS preflight request -> HTTP 204 with the required access-control
  headers.
- Invalid `language` -> `ApiError::InvalidLanguage` and HTTP 400.
- Oversized JSON response or bounded text -> `ResponseTooLarge` /
  `FixedTextError::TooLong`.
- Invalid OTA manifest -> `OtaJobError::Manifest`.
- Manifest `min_supported` newer than current firmware ->
  `OtaJobError::CurrentVersionUnsupported` and `AppState.ota = Failed`.
- Same-version manifest -> no download command and `AppState.ota = Idle`.
- BMS scan request -> no settings write and no fake online state; target runtime
  should start BLE scan and populate candidates from advertisements.
- BMS bind request -> settings write plus runtime scan request; the bound MAC is
  not considered online until BLE connect/subscribe/telemetry succeeds.

#### 5. Good/Base/Bad Cases

- Good: `/api/ota/start` requests a manifest; update manifest returns
  `DownloadFirmware(plan)`; verified image returns `SwitchAndReboot`.
- Good: web settings reads `language`, defaults to Chinese UI text, and posts
  `language` back through `/api/config` when changed in the Device settings
  section.
- Base: `/api/ota/check` with newer manifest sets `UpdateAvailable` and waits
  for an explicit start.
- Bad: web reads `data.battery` while API only emits millivolt fields; the UI
  will stay at `--` despite valid telemetry.

#### 6. Tests Required

- `local_api` tests must assert concrete JSON field names for any new status
  field and for BMS candidate JSON fields consumed by the web UI.
- `local_api` tests must assert `language` is emitted by config JSON, parsed
  from config POST bodies, and applied to `DeviceSettings`.
- `http_server` / `runtime_effects` tests must assert OTA HTTP effects and
  `OtaJobCommand` boundaries.
- `http_api` tests must assert setup password enforcement, Basic auth support,
  and PNA response headers.
- `ota_job` tests must cover check-only, start-after-check, same-version, and
  unsupported-current-version paths.

#### 7. Wrong vs Correct

##### Wrong

```javascript
document.getElementById("battery").textContent = data.battery ?? "--";
```

### Scenario: ESP32 Local Battery ADC Contract

#### 1. Scope / Trigger

- Trigger: target firmware samples the local battery sense pin and exposes it
  through shared `AppState` and `/api/status`.
- Applies to `src/battery.rs`, target-only `src/battery_adc.rs`,
  `src/app_state.rs`, `src/local_api.rs`, `src/display.rs`, and
  `src/web/index.html`.

#### 2. Signatures

- Host-testable conversion:
  `BatterySenseConfig::adc_to_battery_mv(raw: u16) -> Result<u32, BatterySenseError>`.
- Target reader:
  `BatteryAdc::sample_into_state(app_state, config) -> Result<Option<u32>, BatterySenseError>`.
- API field: `/api/status.local_battery_mv`.

#### 3. Contracts

- ESP32 must use GPIO34 / ADC1 for local battery sampling. Do not move this to
  ADC2 because Wi-Fi/BLE can occupy ADC2 on ESP32.
- Default conversion uses `adc_max=4095`, `reference_mv=3300`, and
  `divider_top_ohms=100000`, `divider_bottom_ohms=100000` until the real board
  divider is measured.
- Sampling should be low-rate relative to the UI loop; do not block display and
  touch work for long ADC loops.

#### 4. Validation & Error Matrix

- `adc_max == 0` -> `BatterySenseError::ZeroAdcMax`.
- `divider_bottom_ohms == 0` -> `BatterySenseError::InvalidDivider`.
- ADC read returns no sample -> preserve the previous battery state and return
  `Ok(None)`.
- Real board divider unknown -> document pending hardware calibration; do not
  present voltage accuracy as validated.

#### 5. Good/Base/Bad Cases

- Good: GPIO34 raw sample updates `AppState.battery.latest` and web UI displays
  `local_battery_mv` first.
- Base: no ADC sample yet, BMS pack voltage exists, web UI falls back to
  `pack_voltage_mv`.
- Bad: using GPIO26/ADC2 for battery while Wi-Fi is active; readings can fail
  or conflict with radio.

#### 6. Tests Required

- Keep host tests for raw ADC conversion and invalid divider cases.
- Keep API tests asserting the exact `local_battery_mv` field.
- Target ADC reader compile must be checked on an ESP Xtensa toolchain before
  claiming hardware completion.

#### 7. Wrong vs Correct

##### Wrong

```rust
let pin = adc_config.enable_pin(peripherals.GPIO26, Attenuation::_11dB);
```

### Scenario: BMS Bind Runtime Action Contract

#### 1. Scope / Trigger

- Trigger: touchscreen settings include BMS bind/scan, but the BLE transport is
  a separate runtime owner.
- Applies to `src/touch_ui.rs`, `src/runtime_effects.rs`, `src/bms_ble.rs`, and
  target `src/main.rs`.

#### 2. Signatures

- Touch input returns `UiAction::StartBmsBind`.
- Runtime bridge is `runtime_effects::apply_touch_action(action) -> RuntimeActions`.
- BMS command boundary is `BmsBleRuntime::start_scan() -> BmsBleCommand::Scan`.
- Web/API scan trigger is `POST /api/bms/scan` and maps through
  `HttpEffects::bms_scan_requested()` to `RuntimeActions.start_bms_scan`.

#### 3. Contracts

- Touch UI must not pretend a BMS is bound without BLE confirmation.
- `StartBmsBind` emits `RuntimeActions { start_bms_scan: true, ... }`.
- Target code that receives `start_bms_scan` should reset the Ant BMS frame
  assembler before starting a new scan.
- BLE transport adapter owns actual scan/connect/discover/subscribe I/O.
- Scan candidates live in `AppState.bms_scan_candidates` and are updated from
  matching advertisements. Bound-MAC matching and candidate recording are
  separate decisions.

#### 4. Validation & Error Matrix

- Non-BMS touch actions -> `start_bms_scan == false`.
- `StartBmsBind` -> `start_bms_scan == true` and BLE runtime phase becomes
  `Scanning` once consumed.
- Advertisement does not match bound MAC or `ANT-*` name -> ignore.
- Advertisement begins with `ANT-*` but no bound MAC exists -> record candidate
  without connecting.
- Advertisement matches bound MAC -> record candidate and connect.

#### 5. Good/Base/Bad Cases

- Good: settings row tap or `/api/bms/scan` creates `BmsBleCommand::Scan`.
- Base: no BLE transport yet; command is a pending boundary and UI remains
  responsive.
- Bad: writing a fake MAC or marking BMS online just because the button was
  tapped.

#### 6. Tests Required

- `runtime_effects` must assert `StartBmsBind` maps to `start_bms_scan`.
- `bms_ble` must assert scan/connect/discover/subscribe state transitions.
- Candidate list tests must assert dedupe/update by MAC, fixed-capacity
  behavior, and exact JSON field names.

#### 7. Wrong vs Correct

##### Wrong

```rust
UiAction::StartBmsBind => {
    app_state.bms.online = true;
}
```

##### Correct

```rust
UiAction::StartBmsBind => RuntimeActions {
    start_bms_scan: true,
    ..RuntimeActions::default()
}
```

##### Correct

```rust
let pin = adc_config.enable_pin(peripherals.GPIO34, Attenuation::_11dB);
```

##### Correct

```javascript
document.getElementById("battery").textContent =
  formatMillivolts(data.local_battery_mv ?? data.pack_voltage_mv);
```

---

## Code Review Checklist

<!-- What reviewers should check -->

(To be filled by the team)
