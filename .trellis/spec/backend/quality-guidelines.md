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
