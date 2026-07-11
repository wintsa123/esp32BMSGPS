# Quality Guidelines

> Code quality standards for backend / firmware development.

## Current Firmware Path

The target firmware is an ESP-IDF CMake application. The normal build, flash,
and validation path must not use Cargo or the removed firmware source tree.

Primary validation:

```bash
./scripts/esp-idf-env.sh build
```

Run GitNexus change detection before commit:

```bash
node .gitnexus/run.cjs detect-changes -r esp32BMSGPS
```

## Scenario: LAN RFC2217 Serial Flash Bridge

### 1. Scope / Trigger

- Trigger: flashing or monitoring the ESP32 from this Linux development host
  when the board is attached to the Windows client machine through COM3.
- This is the default remote hardware path for this project; do not ask for a
  USB serial device first when this bridge information is available.

### 2. Signatures

- ESP-IDF port URL: `rfc2217://192.168.2.10:4000?ign_set_control`
- Windows-side serial port: `COM3`
- TCP bridge listen port: `4000`
- Windows host IP: `192.168.2.10`
- Allowed remote CIDR: `192.168.2.108/32`
- Firewall rule: `ESP_COM3_TCP_Bridge_4000`
- Bridge process PID from the recorded setup: `43276`
- Bridge log path on Windows: `target\serial-bridge\bridge.out.log`
- Known-good local validation: `esptool read_mac` succeeds through RFC2217;
  MAC is `20:e7:c8:5f:ab:a4`.

### 3. Contracts

- The bridge is Espressif's official `esp_rfc2217_server`, not a raw TCP
  socket bridge.
- Use `rfc2217://192.168.2.10:4000?ign_set_control`; never use `socket://` for
  this bridge.
- RFC2217 is required so the remote esptool can control DTR/RTS and reset the
  ESP32 into the bootloader.
- Use an explicit flash baud that matches the bridge configuration, normally
  `-b 115200`.
- Only one client should use the RFC2217 bridge at a time.

### 4. Validation & Error Matrix

- Port closed or filtered -> check the Windows bridge process, firewall rule,
  and that this host is still `192.168.2.108`.
- Connection opens but flash sync fails -> confirm the bridge is the RFC2217
  server, not a raw TCP bridge, and that no monitor client is still connected.
- Flash starts then corrupts/times out -> retry with `-b 115200`; do not change
  baud unless the Windows-side bridge was also changed.
- Monitor cannot start after flash -> close any previous client connected to
  the bridge and reconnect with
  `rfc2217://192.168.2.10:4000?ign_set_control`.

### 5. Good/Base/Bad Cases

- Good: `idf.py -p "rfc2217://192.168.2.10:4000?ign_set_control" -b 115200 flash monitor`
  flashes and then streams boot logs.
- Base: `idf.py -p "rfc2217://192.168.2.10:4000?ign_set_control" -b 115200 flash` succeeds; a
  separate RFC2217 monitor is used afterward.
- Bad: using `/dev/ttyUSB0`, `COM3`, or `socket://192.168.2.10:4000` from the
  Linux host.

### 6. Tests Required

- Before flashing, optionally test TCP reachability:

```bash
python3 - <<'PY'
import socket
socket.create_connection(("192.168.2.10", 4000), timeout=3).close()
PY
```

- Flash through the project wrapper:

```bash
./scripts/esp-idf-env.sh -p "rfc2217://192.168.2.10:4000?ign_set_control" -b 115200 flash
```

- Capture boot logs through ESP-IDF monitor:

```bash
./scripts/esp-idf-env.sh -p "rfc2217://192.168.2.10:4000?ign_set_control" -b 115200 monitor
```

### 7. Completion Flash Requirement

- After completing a code task, run one flash attempt through this LAN bridge
  before reporting final completion.
- Do not ask for a serial port when the recorded bridge is available; use
  `rfc2217://192.168.2.10:4000?ign_set_control`.
- If the flash attempt fails, report the exact esptool error and whether the
  RFC2217 endpoint was reachable.

### 8. Wrong vs Correct

#### Wrong

```bash
idf.py -p socket://192.168.2.10:4000 flash
idf.py -p /dev/ttyUSB0 flash
```

#### Correct

```bash
./scripts/esp-idf-env.sh -p "rfc2217://192.168.2.10:4000?ign_set_control" -b 115200 flash
```

## Scenario: NimBLE CCCD Discovery

### 1. Scope / Trigger

- Trigger: discovering descriptors for a known NimBLE GATT characteristic.

### 2. Signatures

- `ble_gattc_disc_all_dscs(conn_handle, chr_val_handle, end_handle, cb, arg)`
- The second handle is the characteristic value handle, not the first possible
  descriptor handle.

### 3. Contracts

- NimBLE advances past `chr_val_handle` internally before scanning descriptors.
- Pass the discovered `ble_gatt_chr.val_handle` unchanged. Adding one in the
  caller skips a CCCD located immediately after the value.
- Accept only UUID `0x2902` as the client characteristic configuration
  descriptor, then subscribe by writing `{ 1, 0 }` to its returned handle.

### 4. Validation & Error Matrix

- CCCD returned -> write notification enable value and enter online only after
  the write callback succeeds.
- Online -> send the first status query immediately, then poll every 500 ms
  while no GATT write is already in flight.
- Discovery ends without CCCD -> report `BMS NO CCCD` and terminate cleanly.
- Subscribe write fails -> report `BMS SUB` and terminate cleanly.

### 5. Good/Base/Bad Cases

- Good: value handle 16, CCCD 17 -> pass 16 and discover 17.
- Base: value handle 17, CCCD 18 -> pass 17 and discover 18.
- Bad: pass 18 for the base case; NimBLE starts after 18 and skips the CCCD.

### 6. Tests Required

- Build with `./scripts/esp-idf-env.sh build`.
- On hardware, assert logs show FFE1 value handle, the immediately following
  CCCD handle, subscription success, a status poll, notification RX, and
  telemetry parsing.

### 7. Wrong vs Correct

#### Wrong

```c
ble_gattc_disc_all_dscs(conn, chr->val_handle + 1U, service_end, cb, arg);
```

#### Correct

```c
ble_gattc_disc_all_dscs(conn, chr->val_handle, service_end, cb, arg);
```

## Scenario: NimBLE Scan Re-entry

### 1. Scope / Trigger

- Trigger: a BMS scan or refresh action arrives while GAP discovery is active.

### 2. Signatures

- `runtime_bms_ble_start_scan(runtime)`
- `ble_gap_disc_active()`

### 3. Contracts

- An active BMS discovery already satisfies a repeated scan request.
- Keep `BMS_SCAN_ACTIVE`, clear deferred `BMS_SCAN_REQUESTED`, and return
  `ESP_OK`; advertisements use `filter_duplicates = 0` and continue refreshing
  the cleared candidate list.
- Cache non-empty candidate names by MAC in bounded component memory. A later
  nameless advertisement or visible-list refresh must reuse that name rather
  than downgrade the row to `设备 N`.
- Candidate array slots are immutable during one scan. When the array is full,
  ignore unseen MACs; never reuse a visible row for a different device until an
  explicit refresh clears the list.

### 4. Validation & Error Matrix

- Discovery active -> reuse it and keep `BMS SCAN` visible.
- BMS connection active -> terminate and defer the scan through the disconnect
  callback.
- Address inference or a new `ble_gap_disc()` call fails -> keep the real
  failure path and log the ESP/NimBLE result.

### 5. Good/Base/Bad Cases

- Good: repeated refresh during discovery logs `reused active discovery`.
- Base: idle host starts a new 10-second discovery.
- Bad: call `ble_gap_disc_cancel()` and immediately call `ble_gap_disc()`;
  asynchronous cancellation can return busy and surface `BLE FAIL`.

### 6. Tests Required

- Build, flash, open the BMS candidate view, and tap refresh repeatedly during
  an active scan. Assert no intermittent `BLE FAIL` and candidates continue to
  populate.

### 7. Wrong vs Correct

#### Wrong

```c
ble_gap_disc_cancel();
ble_gap_disc(...);
```

#### Correct

```c
if (BMS_SCAN_ACTIVE || ble_gap_disc_active()) return ESP_OK;
```

## Scenario: Persistent Touch Calibration

### 1. Scope / Trigger

- Trigger: adding or changing TFT touch calibration, display rotation, or the
  UI action contract that carries calibration samples.

### 2. Signatures

- Actions: `START_TOUCH_CALIBRATION`, `ADD_TOUCH_CALIBRATION_SAMPLE`, and
  `CANCEL_TOUCH_CALIBRATION`.
- Sample fields: target index plus observed and target `uint16_t` coordinates.
- Storage: NVS namespace `esp_bms`, blob key `touch_cal`, with an explicit
  calibration version.

### 3. Contracts

- Calibration is user initiated. Starting a session disables the active user
  mapping but retains it for cancel/error rollback.
- Convert display coordinates to canonical panel coordinates by undoing swap
  first and mirrors second. Apply correction in canonical coordinates, then
  mirror and swap back to the active rotation.
- Accept all four corner samples before saving. A successful save applies
  immediately; cancel, invalid geometry, or NVS failure restores the previous
  mapping.
- Restoring defaults erases `touch_cal` and returns to the driver mapping.

### 4. Validation & Error Matrix

- Duplicate start while active -> `ESP_ERR_INVALID_STATE`.
- Invalid target index, incomplete geometry, reversed axis, or insufficient
  span -> reject without overwriting the previous calibration.
- NVS save failure -> restore the previous in-memory mapping and report error.
- Missing NVS blob at boot -> continue with the factory touch mapping.

### 5. Good/Base/Bad Cases

- Good: four separated corner samples produce bounded X/Y ranges, save, and
  remain correct after rotation and reboot.
- Base: no saved blob; touch uses the existing XPT2046 scaling and rotation.
- Bad: calibrate directly in current landscape coordinates and reuse those
  ranges in portrait; axes and mirrors become rotation dependent.

### 6. Tests Required

- Run a host math check for valid bounds, invalid spans, and round-trip
  conversion in all four rotations.
- Run `git diff --check` and `./scripts/esp-idf-env.sh build`.
- Flash through RFC2217 and confirm boot loads a valid blob without errors.
- On hardware, complete calibration, reboot, restore defaults, and exercise all
  four rotations.

### 7. Wrong vs Correct

#### Wrong

```c
calibrated_x = map(display_x, saved_landscape_min, saved_landscape_max);
```

#### Correct

```c
touch_display_to_canonical(display_x, display_y, &x, &y);
x = touch_calibration_map(x, calibration.x_min, calibration.x_max, width - 1);
touch_canonical_to_display(x, y, &display_x, &display_y);
```

## Required Patterns

- Keep `main/idf_main.c` as orchestration only. Subsystem logic belongs in
  components.
- Keep the top-level ESP-IDF `CMakeLists.txt` `COMPONENTS main` setting before
  `project.cmake`.
- Add dependencies through `main/idf_component.yml` and component `REQUIRES`,
  not by scanning unrelated components into the graph.
- Use ESP-IDF APIs for NVS, Wi-Fi, netif, event handling, HTTP server, ADC,
  UART, OTA, and BLE.
- Keep the embedded Web UI at `main/web/index.html`; embed it from
  `components/esp_bms_idf_runtime/CMakeLists.txt`.
- Use bounded buffers and explicit length checks for HTTP JSON parsing and
  response formatting.
- Do not mutate LVGL/display hardware from the HTTP server task. Queue changes
  and apply them from the main runtime loop.
- Setup AP / HTTP service tasks must not preempt the LVGL adapter or the main
  runtime action loop; keep HTTP priority below the action loop and capture
  bounded heap snapshots around AP client joins and Web API requests when
  diagnosing touch stalls.
- Keep runtime snapshot updates as the contract between hardware/runtime state
  and `esp_bms_lvgl_ui`.
- Log secrets only as lengths or presence flags.

## Scenario: On-Demand Runtime Service Startup

### 1. Scope / Trigger

- Trigger: ESP-IDF runtime services that allocate Wi-Fi, HTTP server, or NimBLE
  resources.
- Boot should keep LVGL, touch, base runtime, ADC, GPS, and audio alive without
  starting Setup AP, HTTP, Wi-Fi STA scan/connect, or NimBLE unless the service
  is explicitly needed.

### 2. Signatures

- `esp_bms_idf_runtime_start_setup_ap(runtime)` starts SoftAP only.
- `esp_bms_idf_runtime_start_http_server(runtime)` starts HTTP only after
  Setup AP is active.
- `esp_bms_idf_runtime_start_bms_ble_if_bound(runtime)` remains available for
  explicit bound-device connection flows but is not called during boot.
- `esp_bms_idf_runtime_start_bms_ble_for_bind(runtime)` starts NimBLE for a
  user-triggered bind or refresh scan.
- `esp_bms_idf_runtime_start_wifi_scan(runtime)` starts Wi-Fi STA/APSTA for a
  user-triggered Wi-Fi scan.

### 3. Contracts

- `app_main()` must not unconditionally start Setup AP, HTTP, Wi-Fi STA, or
  NimBLE after the first UI draw.
- Setup AP credentials may be loaded, regenerated, and saved during runtime
  initialization so the QR page has stable current values, but this must not
  start Wi-Fi.
- Setup AP and HTTP are started from the hotspot/config entry action.
- BMS BLE discovery is never started during boot, including when a bound MAC is
  present. Opening the BMS candidate list or refreshing it starts NimBLE and
  discovery through the user-triggered bind path.
- Wi-Fi STA scan/connect is started from Wi-Fi settings actions only.

### 4. Validation & Error Matrix

- No bound BMS MAC -> return `ESP_OK`, leave NimBLE off, log that no bound MAC is
  configured.
- HTTP requested before Setup AP -> return `ESP_ERR_INVALID_STATE`.
- Wi-Fi scan requested while Wi-Fi stack is off -> initialize Wi-Fi stack and
  start STA, or APSTA when Setup AP is already active.
- NimBLE scan requested before host sync -> mark scan requested and treat it as
  deferred, not a fatal startup failure.

### 5. Good/Base/Bad Cases

- Good: boot logs show `first_ui` and display settings with no BMS scan, even
  when a BMS MAC is already bound.
- Base: tapping hotspot starts SoftAP and HTTP; tapping Wi-Fi scan starts Wi-Fi
  scan; tapping BMS bind starts NimBLE scan.
- Bad: calling `esp_bms_idf_runtime_start_setup_ap()` from boot as a catch-all
  initializer that also starts HTTP, Wi-Fi STA, or NimBLE.

### 6. Tests Required

- Build with `./scripts/esp-idf-env.sh build`.
- Flash and inspect boot logs for absence of automatic BMS discovery with and
  without a bound BMS MAC; opening the candidate list must then start a scan.
- Exercise hotspot, Wi-Fi scan/connect, and BMS bind paths on hardware.
- Run `node .gitnexus/run.cjs detect-changes -r esp32BMSGPS` before commit.

### 7. Wrong vs Correct

#### Wrong

```c
ESP_ERROR_CHECK(esp_bms_idf_runtime_start_setup_ap(&runtime));
```

#### Correct

```c
if (action == ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING) {
    ESP_ERROR_CHECK(esp_bms_idf_runtime_start_setup_ap(&runtime));
    ESP_ERROR_CHECK(esp_bms_idf_runtime_start_http_server(&runtime));
}
```

## Forbidden Patterns

- Do not reintroduce a target Cargo firmware path.
- Do not log setup AP passwords, external Wi-Fi passwords, OTA credentials,
  private token-bearing URLs, or raw request bodies.
- Do not start Wi-Fi before the first LVGL screen is drawn.
- Do not start Setup AP, HTTP, Wi-Fi STA scan/connect, or NimBLE from boot unless
  the on-demand service startup contract above explicitly allows it.
- Do not add a second local Web UI asset; the embedded page is the single local
  UI source.
- Do not synthesize BMS telemetry. BMS fields stay offline/invalid until a real
  BLE transport parses telemetry.
- Do not treat `idf.py build` success alone as OTA readiness; image size must
  fit the active OTA app slot.

## Setup AP Contract

- SSID must be `fuckingBms_` plus a six-character lowercase hexadecimal suffix.
- Setup AP password must be eight random digits.
- Missing or stale NVS setup AP credentials must regenerate and save before
  applying the ESP-IDF Wi-Fi config.
- TFT/Web UI credential text and QR payload must match the active SoftAP config.
- QR payload shape remains `WIFI:S:<ssid>;T:WPA;P:<password>;;`.
- Setup AP/server/gateway IP is `192.168.4.1/24`; DHCP is provided by ESP-IDF's
  AP netif.

## LVGL / TFT Contract

- Display path uses LVGL 9.5.0 plus `espressif/esp_lvgl_adapter`.
- ST7789 is initialized through ESP LCD panel APIs and registered through the
  LVGL adapter.
- Touch uses XPT2046 through `esp_lcd_touch_xpt2046` and the LVGL adapter.
- Brightness changes must call `esp_bms_lvgl_bridge_set_brightness()` and drive
  GPIO21 through LEDC PWM.
- Rotation changes must keep ST7789 panel flags, LVGL resolution/layout, and
  XPT2046 touch transforms in sync.
- Dynamic LVGL labels and QR widgets should update only when their rendered
  value changes.
- Runtime snapshot updates should be deferred while page drag/settle is active.

## Scenario: Cross-Component C Flag Storage

### 1. Scope / Trigger

- Trigger: changing public or cross-component C structs that carry many
  independent boolean state fields, such as runtime snapshots or UI action
  events.
- Use this for RAM/layout optimization only when the struct is copied, stored in
  long-lived state, or appears multiple times in larger runtime/UI objects.

### 2. Signatures

- Public snapshot flags live in `esp_bms_dashboard_snapshot_t.flags`.
- Public action event flags live in `esp_bms_lvgl_action_event_t.flags`.
- Runtime private flags live in `esp_bms_idf_runtime_t.flags`.
- Access through the existing inline helpers:
  `esp_bms_dashboard_snapshot_flag_get/set`,
  `esp_bms_dashboard_snapshot_temperature_valid/set`,
  `esp_bms_lvgl_action_event_flag_get/set`, and
  `esp_bms_idf_runtime_flag_get/set`.

### 3. Contracts

- Public/cross-component structs use explicit integer masks, not C bit-fields.
- Public ABI size changes must update `_Static_assert(sizeof(...))` and all C
  consumers in runtime, UI, JSON writers, and `main/idf_main.c`.
- Private structs may use masks when they reduce persistent RAM and keep callers
  readable.
- Do not use `__attribute__((packed))` for normal runtime state.
- Do not use `restrict`, atomics, memory barriers, or lock-free patterns unless
  the aliasing/concurrency contract is proven from code.

### 4. Validation & Error Matrix

- Old boolean field access still compiles or appears in grep -> update the
  missed consumer before build.
- Flag bit count exceeds integer width -> widen the mask type or keep booleans.
- Public struct size changed without `_Static_assert` update -> reject the
  change.
- Generated font/table file needs manual rewrite -> skip unless regenerated by a
  repeatable generator.

### 5. Good/Base/Bad Cases

- Good: a snapshot has one `uint32_t flags` plus named flag constants and helper
  reads in all consumers.
- Base: a tiny setup config keeps readable `bool` fields because compaction
  saves negligible RAM.
- Bad: using C bit-fields in a public struct copied between runtime and UI.

### 6. Tests Required

- Run `rg` for removed boolean field names across `main/` and `components/`.
- Run `git diff --check`.
- Run `./scripts/esp-idf-env.sh build`.
- Run `node .gitnexus/run.cjs detect-changes -r esp32BMSGPS --scope all` and
  summarize any HIGH/CRITICAL scope.
- Run one RFC2217 flash attempt before reporting completion.

### 7. Wrong vs Correct

#### Wrong

```c
typedef struct {
    bool gps_fix_valid;
    bool bms_online;
} esp_bms_dashboard_snapshot_t;
```

#### Correct

```c
#define ESP_BMS_DASHBOARD_FLAG_GPS_FIX_VALID (UINT32_C(1) << 1)
#define ESP_BMS_DASHBOARD_FLAG_BMS_ONLINE (UINT32_C(1) << 2)

typedef struct {
    uint32_t flags;
} esp_bms_dashboard_snapshot_t;
```
- After the first UI is visible, touch/UI action failures must not use
  `ESP_ERROR_CHECK` in the main loop for brightness, rotation, or snapshot
  refresh. Log `esp_err_to_name(ret)`, keep the loop alive, and do not persist
  display settings when the hardware/UI apply step failed.
- LVGL long-press handlers that must not also run the click action should call
  `lv_indev_wait_release(lv_indev_active())` from `LV_EVENT_LONG_PRESSED`;
  LVGL 9 can still emit `LV_EVENT_CLICKED` when the pointer is released after a
  long press.
- LVGL timers that hide transient UI objects must be deleted before deleting or
  rebuilding the screen root. Otherwise a rotation rebuild can leave a timer
  callback pointing at a deleted object.
- Quick-panel return gestures should start from the panel/background return
  area, not from ordinary quick button events. Button press/release/click paths
  should not share a broad upward-return start condition, or normal taps can be
  swallowed as panel-return drags.
- Quick-panel controls should be interactive only after the panel is fully open
  and settled. During pull-to-open, return-to-home, or y-position settle
  animation, quick button and level-control event handlers should ignore
  actions to avoid touch drift firing a button through a partially visible
  panel.
- The root TFT settings page should stay a stacked-card scrolling category
  list. Do not fill it with unrelated one-off action buttons; top-level
  entries should stay limited to Wi-Fi, hotspot, Bluetooth, BMS/protection
  board, system, and about-device categories until dedicated subpages exist.
- Settings carousel scroll handlers must not refresh every card during
  `LV_EVENT_SCROLL`. Register expensive style refresh only on
  `LV_EVENT_SCROLL_END`; carousel refresh may update lightweight
  translate/border state, but must not use transform scale/width/height effects
  that force extra redraw work while the user is dragging.
- TFT icon controls should prefer LVGL built-in `LV_SYMBOL_*` glyphs from the
  enabled Montserrat/FontAwesome fonts, or simple LVGL primitives for tiny
  custom icons. Enable any required built-in font size in both `sdkconfig` and
  `sdkconfig.defaults`. Do not depend on external iconfont assets until a
  device-side font/image loading path is explicitly added.
- User-provided/generated LVGL font C files may be compiled into a component
  and referenced with `LV_FONT_DECLARE` for explicit icon requirements. Keep
  the glyph range and UTF-8 literal in the same change, and validate with
  `./scripts/esp-idf-env.sh build`.
- Do not use LVGL `transform_scale`, animated object opacity, or other effects
  that force off-screen draw layers for TFT quick controls. After Wi-Fi and
  NimBLE start, free heap is tight enough that layer allocation during a quick
  button redraw can stall the LVGL worker and trip the task watchdog. Use
  border/color state changes unless the effect is validated on hardware with
  Wi-Fi, HTTP, NimBLE, and touch active.
- Keep visible TFT text ASCII by default. A feature may use Chinese or other
  non-ASCII TFT text only when it ships an explicit compiled LVGL font subset
  for the required glyphs, keeps the glyph list scoped to that feature, and is
  validated with preview plus `./scripts/esp-idf-env.sh build`.

## Web API Contract

The embedded Web UI currently calls these routes:

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

Every route must either be implemented or intentionally return a clear error
while the remaining ESP-IDF subsystem is being built. Do not return fake BMS
candidates or fake OTA success.

## Scenario: ANT BMS BLE Telemetry

### 1. Scope / Trigger

- Trigger: implementing or modifying ESP-IDF BLE BMS scan, bind, connection,
  notification, or telemetry parsing in `esp_bms_idf_runtime`.

### 2. Signatures

- BLE service UUID: `0xFFE0`.
- BLE characteristic UUID: `0xFFE1`.
- Web routes involved: `POST /api/bms/scan`, `GET /api/bms/candidates`,
  `POST /api/bms/bind`, and `GET /api/status`.

### 3. Contracts

- Scan records candidates only from ANT-looking advertisements, service
  advertisements, or the currently bound MAC.
- Binding persists a normalized uppercase MAC in NVS key `bms_mac`.
- A saved or newly bound MAC should trigger a scan; a matching advertisement
  may start a GATT connection.
- Notifications must validate ANT frame start/end bytes, protocol length, and
  Modbus CRC before updating `esp_bms_dashboard_snapshot_t`.
- Status frame `0x11` is the only frame that may set `bms_online` or populate
  voltage/current/SOC/cell/capacity fields.

### 4. Validation & Error Matrix

- Invalid MAC in `/api/bms/bind` -> `400 Bad Request`.
- BLE not ready during scan -> queue/defer the scan and log without marking
  fake telemetry online.
- Missing service, characteristic, or CCCD -> terminate the connection and keep
  BMS telemetry offline/invalid.
- Bad frame length, tail, or CRC -> reject the notification and keep the last
  valid snapshot unchanged.

### 5. Good/Base/Bad Cases

- Good: bound MAC appears, GATT discovers `0xFFE0/0xFFE1`, CCCD subscribe
  succeeds, a valid status frame updates dashboard telemetry.
- Base: scan finds candidates but no bound MAC; candidates are exposed through
  `/api/bms/candidates`, telemetry remains offline.
- Bad: scan has no candidates or notifications fail validation; do not
  synthesize BMS values.

### 6. Tests Required

- `./scripts/esp-idf-env.sh build` must pass after BLE changes.
- Add/keep parser coverage for known ANT status frames when a C/component test
  harness is available.
- Hardware validation must confirm candidate scan, bind persistence, connection,
  notification parsing, and Web/TFT telemetry update with a real ANT BMS.

### 7. Wrong vs Correct

#### Wrong

Returning a successful BMS status or fabricated candidates when BLE scan,
GATT discovery, or frame parsing has not produced real data.

#### Correct

Keep BMS fields offline/invalid until validated BLE notifications from the
bound device update the runtime snapshot.

## Validation Matrix

- QR screen visible but phone cannot see AP -> inspect whether
  `esp_bms_idf_runtime_start_setup_ap()` ran after first UI draw and whether
  logs show AP start.
- Phone associates but waits for IP -> inspect AP netif IP and DHCP assignment
  logs.
- Phone gets IP but Web UI fails -> inspect `esp_http_server` startup and auth
  headers.
- Settings POST returns success but TFT does not change -> inspect pending
  config application in `esp_bms_idf_runtime_tick()`.
- AP password POST returns success but QR still shows old password -> inspect
  pending password application, NVS save, Wi-Fi config update, and snapshot QR
  regeneration.
- BMS scan returns empty forever -> inspect NimBLE init/sync, scan start logs,
  ANT advertisement filters, and whether the bound MAC is present; do not
  fabricate data.
- OTA routes fail -> expected until the ESP-IDF OTA transport is implemented;
  do not mark OTA acceptance complete.

## Tests Required

- Run `./scripts/esp-idf-env.sh build` after firmware, component, Web UI asset,
  LVGL config, Wi-Fi, HTTP, NVS, BLE, or OTA changes.
- Run `node .gitnexus/run.cjs detect-changes -r esp32BMSGPS` before commit.
- Hardware validation is still required for phone scan/join of the displayed
  setup AP, Web UI load at `192.168.4.1`, GPS UART0, GPIO34 ADC scaling, BLE
  BMS, and OTA slot switching.
