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
- Keep runtime snapshot updates as the contract between hardware/runtime state
  and `esp_bms_lvgl_ui`.
- Log secrets only as lengths or presence flags.

## Forbidden Patterns

- Do not reintroduce a target Cargo firmware path.
- Do not log setup AP passwords, external Wi-Fi passwords, OTA credentials,
  private token-bearing URLs, or raw request bodies.
- Do not start Wi-Fi before the first LVGL screen is drawn.
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
- Keep visible TFT text ASCII until a separate font task approves a wider font
  plan.

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
