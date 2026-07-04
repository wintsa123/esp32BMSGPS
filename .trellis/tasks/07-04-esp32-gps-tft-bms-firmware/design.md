# ESP32 GPS TFT BMS Firmware Design

## Architecture

The first MVP is a single `no_std` Rust firmware built on the `esp-hal`
ecosystem for ESP32-WROOM-32E with 4 MB flash. The application uses explicit
driver/service boundaries so hardware-specific code, protocol parsing, UI, web
configuration, and persistence can be tested or replaced independently.

Primary stack:

- `esp-hal` for GPIO, SPI, UART, ADC, timers, PWM, and core peripheral access.
- `esp-radio` for Wi-Fi, BLE, and Wi-Fi/BLE coexistence.
- TrouBLE for the BLE host stack and Ant BMS GATT client behavior.
- Embassy executor/networking for async tasks and TCP/IP.
- `esp-storage` plus `esp-bootloader-esp-idf` for flash access and OTA slot
  switching.
- Hand-drawn TFT UI, not LVGL.
- Device-hosted plain HTML/CSS/small vanilla JS embedded in the firmware image.

## Hardware Boundaries

All GPIO numbers live in one central board module. Drivers consume named pins,
not raw GPIO numbers.

Board target:

- ESP32-WROOM-32E, 4 MB flash, no PSRAM assumed.
- TPM408 2.8-inch TFT touchscreen.
- GPS 336H on UART0, NMEA, 9600 baud.
- Ant BMS over BLE using the `syssi/esphome-ant-bms` BLE protocol reference.

Display:

- ST7789, 240 x 320, BGR color order.
- TFT SPI pins: MISO GPIO12, MOSI GPIO13, SCLK GPIO14, CS GPIO15, DC GPIO2,
  RST not connected, BL GPIO21 active-high.
- Use small tile or line buffers only; no full-screen framebuffer.
- Validate cold boot because GPIO12/GPIO15/GPIO2 are boot-related pins.

Touch:

- XPT2046 / XP2046-compatible resistive touch.
- Touch pins: IRQ GPIO36, MISO GPIO39, MOSI GPIO32, CS GPIO33, CLK GPIO25.
- Calibrate raw touch coordinates to screen coordinates and persist calibration
  if needed.

GPS:

- UART0 is already wired bidirectionally:
  - GPS TX -> ESP32 U0RXD/GPIO3.
  - ESP32 U0TXD/GPIO1 -> GPS RX.
- Read NMEA at 9600 baud. Treat GPS command writes as optional and avoid them
  unless required.
- UART0 is also used for bootloader/flashing/logging. The firmware should avoid
  continuous UART0 logging after boot. Hardware validation must include flashing
  with the GPS connected; if flashing is unreliable, document disconnect/jumper
  or retry procedure.

Reserved first-MVP-out pins:

- Battery ADC GPIO34 is in MVP.
- RGB GPIO17/GPIO22/GPIO16 may be used for status indication if low effort.
- Audio GPIO26/GPIO4 is reserved only.
- SD card SPI GPIO23/GPIO19/GPIO18/GPIO5 is reserved only.
- Expansion SPI CS GPIO27 is reserved only.

## Runtime Task Model

The firmware should be structured as cooperative async tasks where possible:

- `gps_task`: read UART0, parse NMEA, publish latest fix/speed.
- `bms_ble_task`: scan/connect to Ant BMS, subscribe to notifications, send
  request frames, publish latest battery telemetry.
- `display_task`: render dashboard and settings from shared app state using
  partial redraws.
- `touch_task`: read XPT2046, emit UI events.
- `wifi_task`: manage setup AP, station mode, and AP shutdown after successful
  external connection.
- `http_task`: serve embedded web assets and JSON endpoints.
- `ota_task`: check version manifest and stream OTA image into the inactive app
  slot when requested by the web/API flow.
- `config_task` or config module: persist settings atomically in flash.

Shared state should use small, explicit structs:

- `GpsState`: fix validity, speed, timestamp, satellite count if available.
- `BmsState`: online status, voltage, current, SOC, cell min/max/delta,
  temperatures, MOS/balancer status, model/version.
- `UiSettings`: brightness, speed unit, screen orientation, active settings
  screen.
- `WifiSettings`: setup AP SSID/password, external SSID/password, setup AP
  state, and external connection state.
- `DeviceSettings`: BMS MAC/binding and OTA metadata. The version-service URL is
  a build-time configuration value for the first MVP.

## BMS BLE Protocol

The BLE transport and protocol parser are separate modules.

BLE transport responsibilities:

- Scan for advertised names beginning with `ANT-`.
- Bind by selected MAC address after user confirmation.
- Connect as BLE central / GATT client.
- Discover service `0xFFE0` and characteristic `0xFFE1`.
- Subscribe to notifications.
- Write command frames without response where supported.
- Reconnect with backoff and publish offline state after missed responses.

Protocol parser responsibilities:

- Assemble notification chunks into complete frames.
- Detect frame start `0x7E 0xA1` and end `0xAA 0x55`.
- Enforce max frame size and validate length before indexing.
- Validate CRC16 using the same Modbus-style polynomial as the reference.
- Support at least:
  - status command `0x01`
  - device-info command `0x02`
  - optional write-register command `0x51` only after read-only behavior is
    stable
- Decode MVP fields: total voltage, current, SOC, cell count, cell voltages,
  min/max/delta cell voltage, temperatures, MOS states, balancer state,
  battery status, capacity remaining, and device model/software version.

The first MVP should remain read-oriented. BMS setting writes are out of scope
unless explicitly added after read telemetry is stable.

## TFT UI

Use a direct drawing UI:

- Dashboard first screen: large GPS speed, GPS fix indicator, BMS online status,
  pack voltage/current/SOC, min/max cell voltage, temperature summary, Wi-Fi
  status, firmware version.
- Settings screens:
  - Wi-Fi provisioning/reprovisioning.
  - Brightness.
  - Screen orientation: portrait, landscape, inverted portrait, inverted
    landscape.
  - Speed unit: km/h or mph.
  - BMS scan/bind or MAC entry.
  - Restore defaults.
- OTA is not a touchscreen action in the first MVP.

Rendering constraints:

- Avoid full-screen framebuffer.
- Keep fonts minimal.
- Redraw only changed regions or tile bands.
- Backlight brightness uses GPIO21 through PWM if hardware supports it; otherwise
  GPIO21 is on/off only.
- Screen orientation is a user setting. The display driver owns ST7789 `MADCTL`
  mapping and reports logical width/height for the selected orientation so
  dashboard and touch mapping use the same coordinate system.

## Wi-Fi Provisioning And Local Web

First boot:

- Generate a random setup AP password.
- Start password-protected setup AP.
- Display a Wi-Fi QR code on the TFT using the setup AP SSID/password.
- Serve the local setup web UI.

After successful external Wi-Fi connection:

- Turn off setup AP by default.
- Re-enable setup AP only through touchscreen reprovisioning.

Wi-Fi state contract:

- `FirstBoot`: setup AP is enabled until external Wi-Fi credentials are saved
  and station connection succeeds.
- `Disabled`: setup AP is off after a successful external Wi-Fi connection.
- `Reprovisioning`: setup AP is enabled by touchscreen action and remains
  enabled until new external Wi-Fi connection succeeds.
- Web and touch handlers change desired state; the Wi-Fi task is the only owner
  that starts/stops AP and station mode.

The QR code encodes only Wi-Fi join credentials. The TFT may show the local
setup URL as text, for example `http://192.168.4.1`.

The device-hosted web UI:

- Plain HTML/CSS/small vanilla JS.
- Embedded with `include_bytes!` or equivalent into the firmware app image.
- No frontend framework in the first MVP.
- No large fonts, images, icon packs, or charting libraries.
- Must distinguish setup AP password from external Wi-Fi password.

Suggested HTTP/API surface:

- `GET /` serves the setup/status page.
- `GET /api/status` returns GPS/BMS/Wi-Fi/firmware status.
- `GET /api/config` returns editable config excluding secret plaintext where
  possible.
- `POST /api/config` updates settings.
- `POST /api/wifi` updates external Wi-Fi credentials and starts reconnect.
- `POST /api/ap-password` updates setup AP password.
- `POST /api/bms/bind` stores selected BMS MAC.
- `POST /api/ota/check` checks the public version service.
- `POST /api/ota/start` starts OTA when the device is on internet-capable Wi-Fi.

## Persistence

Use a small reserved flash data/config area through `esp-storage` or a thin
embedded-storage abstraction. Do not add a general-purpose filesystem in the
first MVP.

Persisted settings:

- Setup AP password and generated device/setup SSID.
- External Wi-Fi SSID/password.
- Brightness.
- Screen orientation.
- Speed unit.
- BMS MAC/binding.
- Optional touch calibration.
- OTA/current firmware metadata needed for validation and rollback bookkeeping.

Persistence rules:

- Use versioned config records.
- Include CRC or checksum for each record.
- Write atomically with at least two slots or generation counters.
- Never log Wi-Fi passwords.
- Restore defaults clears user settings but keeps immutable build/hardware
  constants.

## OTA

The first OTA path is internet-based after the device connects to external
Wi-Fi.

Flow:

1. Firmware has a build-time configured public version-service URL.
2. Device requests a small JSON manifest.
3. Device compares manifest version with current firmware version.
4. Device streams the firmware image from `firmware_url` into the inactive app
   partition.
5. Device verifies `sha256`.
6. Device activates the next partition and reboots.
7. New firmware marks itself valid after basic startup checks pass.

Manifest fields:

```json
{
  "latest": "0.1.0",
  "min_supported": "0.1.0",
  "firmware_url": "https://example.com/firmware.bin",
  "sha256": "...",
  "size": 0,
  "notes": ""
}
```

Security:

- SHA-256 is required in the first MVP for corruption detection and basic
  integrity checking.
- Firmware signing is recommended before production use. The first MVP requires
  SHA-256 only, but the manifest should be designed so a signature field can be
  added without breaking clients.
- HTTPS is preferred, but TLS memory/flash cost must be validated on the 4 MB
  ESP32-WROOM-32E target.

Partition strategy:

- Use OTA-capable app slots from the start.
- Because web assets are embedded in the app image, each app slot must fit the
  firmware plus static web assets.
- Start from the official `esp-hal` OTA example partition strategy for the OTA
  spike, then finalize the 4 MB partition table after measuring actual release
  binary size.
- No separate web filesystem partition in the first MVP.

Recovery:

- USB flashing remains the required recovery path.
- Local web upload OTA can be added later if internet-based OTA is not enough.

## Compatibility And Risks

High-risk items to validate first:

- BLE central/GATT client over `esp-radio` + TrouBLE against the actual Ant BMS.
- Wi-Fi AP+STA coexistence with BLE on ESP32-WROOM-32E.
- OTA slot size with embedded web assets in 4 MB flash.
- UART0 GPS coexistence with flashing/logging.
- Cold boot with boot-strapping pins used by TFT, audio enable, and SD CS.
- ST7789 BGR initialization and XPT2046 touch calibration on the TPM408 module.

Design fallback points:

- If BLE client support blocks the BMS path, implement wired UART Ant BMS as a
  fallback only if hardware wiring is available.
- If app image exceeds OTA slot size, reduce web assets/fonts first, then adjust
  partition layout; do not add SD or filesystem complexity to solve first-MVP
  size issues.
- If UART0 GPS breaks flashing, require a hardware disconnect/jumper or move GPS
  in a later board revision.
- If Wi-Fi/BLE coexistence is unstable, reduce concurrent scanning and make BMS
  polling interval configurable.
