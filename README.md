# ESP32 BMS GPS Firmware

Rust `no_std` firmware scaffold for an ESP32-WROOM-32E dashboard with GPS speed,
TPM408 TFT/touch, Ant BMS BLE telemetry, local Wi-Fi setup UI, and OTA-ready
partitioning.

## Hardware Target

- MCU: ESP32-WROOM-32E, 4 MB flash, no PSRAM assumed.
- Display: TPM408 2.8 inch, ST7789, 240 x 320, BGR color order.
- Touch: XPT2046 / XP2046 compatible.
- GPS: 336H, UART NMEA, 9600 baud, wired to UART0.
- BMS: Ant BMS over BLE, service `0xFFE0`, characteristic `0xFFE1`.

## Pin Map

| Function | Pin |
| --- | --- |
| Battery ADC | GPIO34 |
| RGB LED R/G/B | GPIO17 / GPIO22 / GPIO16 |
| TFT MISO/MOSI/SCLK/CS/DC/BL | GPIO12 / GPIO13 / GPIO14 / GPIO15 / GPIO2 / GPIO21 |
| TFT reset | not connected |
| Touch IRQ/MISO/MOSI/CS/CLK | GPIO36 / GPIO39 / GPIO32 / GPIO33 / GPIO25 |
| GPS UART0 TX/RX | GPIO1 / GPIO3 |
| Audio reserved IN/EN | GPIO26 / GPIO4 |
| Expansion SPI CS reserved | GPIO27 |
| SD SPI reserved MOSI/MISO/SCK/CS | GPIO23 / GPIO19 / GPIO18 / GPIO5 |

Boot validation is required with GPIO2, GPIO4, GPIO5, GPIO12, and GPIO15 attached.
GPS on UART0 can interfere with flashing and logs; the current firmware prints
only a short boot smoke message.

## Build Prerequisites

Install the ESP Rust Xtensa toolchain and `espflash`, then load the toolchain
environment for your shell:

```bash
cargo install espup espflash
espup install
. "$HOME/export-esp.sh"
```

This checkout currently expects target `xtensa-esp32-none-elf`. On this
development machine, the ESP Rust `esp` toolchain is installed with Xtensa Rust
1.95.0.0, Xtensa GCC 15.2.0, and LLVM/libclang 20.1.1.

Default firmware features include settings storage plus the Wi-Fi setup AP
transport. The BLE/coexistence radio link is kept behind the non-default
`ble-radio` feature until the target BLE transport adapter is implemented and
linked on hardware.

## Build And Flash

On Windows, use the helper script from the repository root:

```powershell
.\scripts\flash.ps1
```

Useful options:

```powershell
.\scripts\flash.ps1 -Port COM3
.\scripts\flash.ps1 -Monitor
.\scripts\flash.ps1 -Profile debug
.\scripts\flash.ps1 -ManifestUrl "https://your-domain.example/firmware/manifest.json"
```

The script currently builds with:

```powershell
cargo +esp build -Zbuild-std=core --target xtensa-esp32-none-elf --release
```

and flashes with:

```powershell
espflash flash -p COMx --partition-table partitions.csv target\xtensa-esp32-none-elf\release\esp32-bms-gps
```

If you prefer to run the steps manually, the repository already includes
`.cargo/config.toml` with the ESP32 target, `build-std`, and linker arguments:

```powershell
cargo +esp build -Zbuild-std=core --target xtensa-esp32-none-elf --release
espflash flash -p COM3 --partition-table partitions.csv target\xtensa-esp32-none-elf\release\esp32-bms-gps
```

The OTA manifest endpoint is also build-time configurable via `-ManifestUrl`,
which sets `OTA_MANIFEST_URL` for the build.

## Flash Layout

`partitions.csv` starts with an OTA-capable 4 MB layout:

- `factory`: 1 MB
- `ota_0`: 1 MB
- `ota_1`: 1 MB
- `nvs`, `otadata`, and `phy_init` data partitions

Web assets are embedded in the app image with `include_bytes!`, so every OTA app
slot must fit the firmware plus static assets.

## Current Scope

Implemented in this scaffold:

- `esp-hal` boot entry for ESP32.
- ESP-IDF-compatible app descriptor for OTA metadata.
- TFT backlight GPIO21 driven high continuously during the main loop.
- GPIO17 red LED heartbeat toggles every 250 ms during the main loop.
- ST7789 driver initializes the display and can draw a low-memory dashboard,
  settings menu, calibration targets, touch feedback, filled rectangles, and a
  small built-in 5 x 7 ASCII font.
- Display rotation is a persisted device setting with portrait, landscape,
  inverted portrait, and inverted landscape values.
- Central board pin map.
- Host-testable RMC NMEA speed parser plus a byte-stream NMEA line buffer for
  the future UART0 task.
- Host-testable GPS service that feeds UART byte chunks into the NMEA line
  buffer, updates `AppState`, and tracks parse errors.
- Host-testable first-boot Wi-Fi setup AP state model.
- OTA manifest URL build config.
- Host-testable OTA manifest parser, SHA-256 hex validation, and version
  comparison.
- Embedded plain HTML/CSS/vanilla JS setup page placeholder.
- Flash-backed full device settings store using two records in the reserved tail
  region of flash. The loader can migrate the old single `TCHC` touch
  calibration record if present.
- Factory touch calibration for the current TPM408 module:
  `raw_x_min=453`, `raw_x_max=3549`, `raw_y_min=613`, `raw_y_max=3485`,
  `swap_xy=true`, `invert_x=false`, `invert_y=false`, `width=320`,
  `height=240`.
- Host-testable versioned settings record codec with generation counter, CRC,
  dual-slot latest-record selection, Wi-Fi credentials, AP password, BMS MAC,
  display settings, speed unit, and optional touch calibration.
- Host-testable local API route/settings contract for `/api/status`,
  `/api/config`, `/api/wifi`, `/api/ap-password`, `/api/bms/bind`,
  `/api/bms/candidates`, `/api/bms/scan`, `/api/ota/check`, and
  `/api/ota/start`.
- Host-testable HTTP dispatch layer that serves the embedded index page,
  returns JSON API responses, applies JSON settings updates to `AppState`, and
  maps API errors to HTTP status codes.
- Host-testable raw HTTP request parser and response-header writer for the
  future single-connection TCP server.
- Host-testable single-connection HTTP core that maps raw request bytes to
  response headers plus body slices, mutates `AppState` through the API
  dispatch layer, and reports runtime effects for settings persistence, Wi-Fi
  reconnect, and OTA actions.
- Host-testable runtime effect dispatcher that turns HTTP and Wi-Fi events into
  explicit actions such as `persist_settings`, `reconnect_wifi`, OTA check, and
  OTA download start.
- Host-testable Wi-Fi control-plane state model that chooses setup AP, station,
  or AP+STA mode and closes setup AP after a successful external Wi-Fi
  connection.
- Host-testable first-provisioning helpers for setup AP SSID/password and Wi-Fi
  QR payload generation.
- Host-testable no-heap QR encoder integration for first-boot Wi-Fi payloads.
- Host-testable local battery ADC conversion/state model for GPIO34 using an
  explicit voltage-divider configuration, with `/api/status` exposing
  `local_battery_mv`.
- Target firmware samples GPIO34 through ADC1 at a low rate and feeds the same
  battery state. The current default divider model is 100 kOhm / 100 kOhm and
  should be adjusted after measuring the real board divider.
- Host-testable Ant BMS frame builder, Modbus CRC16 validation, BLE notification
  frame assembler, status telemetry parser, and device-info parser. The parser
  follows the `syssi/esphome-ant-bms` BLE frame shape: service `0xFFE0`,
  characteristic `0xFFE1`, frame start `7E A1`, frame end `AA 55`.
- Host-testable Ant BMS BLE control-plane state machine for scan, filtered
  connect, service/characteristic discovery, notification subscription, polling,
  frame assembly, telemetry update, and offline backoff. The real ESP32 BLE
  transport adapter is still pending.
- Host-testable fixed-capacity Ant BMS scan candidate list. BLE advertisements
  matching `ANT-*` are exposed through `/api/bms/candidates`, and the web UI can
  request a scan through `/api/bms/scan` before binding a selected MAC.
- Host-testable app-state snapshot model combining GPS, BMS, Wi-Fi, OTA, and
  user settings for the dashboard renderer.
- Embedded web UI now displays the local ADC battery voltage when available and
  falls back to BMS pack voltage. It also includes a framework-free BMS scan
  list that can fill and bind the BMS MAC without typing it manually.
- Host-testable touchscreen UI state machine for dashboard/settings navigation,
  Wi-Fi reprovisioning, brightness cycling, screen rotation, speed-unit toggle,
  BMS bind scan action, and restore-defaults behavior.
- Touchscreen BMS bind now emits a runtime `start_bms_scan` action and drives
  the host-testable BLE control state machine to `Scan`; the real BLE radio
  adapter still needs to consume that command on target hardware.
- Host-testable OTA download core that streams chunks to an image writer,
  enforces manifest size, and verifies SHA-256 before an OTA slot switch is
  allowed.
- Host-testable OTA job state machine that turns web/API update requests into
  explicit runtime commands: fetch manifest, download verified firmware, switch
  slot, and reboot. It also keeps `AppState.ota` aligned with check, available,
  download, verify, ready-to-reboot, and failed states.
- Firmware `main` now loads persisted settings, starts the ESP RTOS scheduler
  and Wi-Fi heap for target wireless builds, generates first-boot setup AP
  SSID/password if missing, starts the `esp-radio` setup AP when provisioning is
  enabled, applies factory touch calibration as fallback, initializes UART0 at
  9600 baud for GPS NMEA, polls buffered GPS bytes into `AppState`, renders a
  scannable Wi-Fi QR setup screen while setup AP is enabled, renders the
  dashboard/settings screens otherwise, and persists settings changed by
  touchscreen actions.
- Target firmware initializes the `esp-println` logger and emits Wi-Fi
  diagnostics for desired mode, AP SSID, password lengths, `esp-radio`
  controller/config errors, and the current limitation that station credentials
  are configured but the async external-AP connect task is not running yet.
- OTA-ready partition table.

Smoke-test interpretation:

- Red LED blinking and screen black: firmware is alive; continue with ST7789
  initialization, backlight polarity, and display wiring checks.
- Red LED not blinking: check reset loop, power, boot strap pins, and flashing
  with GPS attached to UART0.
- Blue LED on briefly during display init, green LED on afterward: the firmware
  finished sending the ST7789 init and color-bar writes. This still does not
  prove the LCD received them, because ST7789 writes are one-way in this setup.

Next implementation phases:

- GPS UART0 hardware validation and async/task cleanup.
- `embassy-net` station connection lifecycle and TCP socket read/write adapter
  around the HTTP connection core.
- Async station connect task using `WifiController::connect_async()` so saved
  external Wi-Fi credentials progress beyond the current "connecting" state.
- Ant BMS BLE scan/connect transport.
- OTA manifest download transport, inactive partition writer, boot partition
  switch, and post-boot validity marking.

## Current Validation

Passing on the development machine:

```bash
cargo fmt --check
cargo test --target x86_64-unknown-linux-gnu --lib
cargo clippy --target x86_64-unknown-linux-gnu --lib -- -D warnings
```

Current host test count: 96.

Passing for the ESP32 target on the development machine:

```bash
. "$HOME/export-esp.sh"
cargo +esp check --bin esp32-bms-gps -j1
cargo +esp clippy --bin esp32-bms-gps -j1 -- -D warnings
cargo +esp build --release -j1
```

Current release ELF:

- `target/xtensa-esp32-none-elf/release/esp32-bms-gps` (3,324,472 bytes)

Current generated factory app image:

- `/tmp/esp32-bms-gps.bin` from `espflash save-image`: 549,488 bytes
  (52.40% of the 1 MB app slot)

Pending on hardware:

- Hardware validation remains pending for GPS UART0, GPIO34 ADC scaling against
  a multimeter, phone scan and join of the setup AP shown on the TFT QR screen,
  BLE BMS, full settings Flash writes on device, and OTA slot switching.
