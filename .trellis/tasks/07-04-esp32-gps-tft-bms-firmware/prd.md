# ESP32 GPS TFT BMS Firmware

## Goal

Build a Rust firmware for an ESP32-based vehicle / battery dashboard that:

- reads GPS data and displays speed on a TFT touchscreen
- connects to an Ant BMS over Bluetooth LE and displays battery telemetry
- lets the user configure key settings from the touchscreen
- serves a lightweight local Wi-Fi web UI for setup and configuration
- supports OTA firmware upgrade through a public version-service endpoint

## Confirmed Decisions

- Firmware language and stack: Rust, `no_std`, direct `esp-hal` ecosystem.
- Wireless stack direction: `esp-radio` for Wi-Fi/BLE and TrouBLE for BLE host /
  GATT client behavior.
- First hardware target: ESP32-WROOM-32E with 4 MB flash, no PSRAM assumed.
- Display: TPM408 2.8-inch resistive touchscreen, 240 x 320, ST7789, BGR color
  order.
- Touch controller: XPT2046 / XP2046-compatible resistive touch.
- GPS: 336H, UART0 NMEA, 9600 baud, already wired bidirectionally.
- BMS: Ant BMS over BLE, porting the BLE protocol behavior documented by
  `syssi/esphome-ant-bms`.
- Local web UI: served from ESP32, plain HTML/CSS/small vanilla JS, embedded in
  the firmware app image and updated with firmware OTA.
- TFT UI: direct lightweight drawing; no LVGL or full widget framework in the
  first MVP.
- First-boot provisioning: generate a random setup AP password, show Wi-Fi QR
  code on TFT, let the user configure Wi-Fi from the local web page.
- Setup AP lifecycle: enable only during first provisioning or touchscreen
  reprovisioning; turn off after successful external Wi-Fi connection.
- SD card and audio are out of first MVP scope; their pins remain reserved.
- Touchscreen does not expose OTA actions in the first MVP. OTA is exposed via
  local web/API when the device is connected to internet-capable Wi-Fi.
- OTA integrity for MVP: HTTPS where feasible plus SHA-256 verification.
  Firmware signing is recommended for production but not required for MVP.

## Hardware Map

### ESP32 Module

- ESP32-WROOM-32E.
- Current board has 4 MB flash.
- No PSRAM is assumed.

### TFT / ST7789

- Resolution: 240 x 320.
- Color order: BGR.
- `TFT_MISO = GPIO12`
- `TFT_MOSI = GPIO13`
- `TFT_SCLK = GPIO14`
- `TFT_CS = GPIO15`
- `TFT_DC = GPIO2`
- `TFT_RST = -1`
- `TFT_BL = GPIO21`, active-high.

Boot note: GPIO12, GPIO15, and GPIO2 are ESP32 boot-related pins. Cold boot with
the display attached must be validated before the pin map is treated as final.

### Touch / XPT2046

- `TOUCH_IRQ = GPIO36`
- `TOUCH_MISO = GPIO39`
- `TOUCH_MOSI = GPIO32`
- `TOUCH_CS = GPIO33`
- `TOUCH_CLK = GPIO25`

### GPS

- GPS module: 336H.
- Protocol: UART NMEA, 9600 baud.
- Wiring: UART0 bidirectional.
- `U0RXD/GPIO3` receives GPS TX.
- `U0TXD/GPIO1` drives GPS RX.

UART0 also carries bootloader/flashing/logging traffic. Firmware must avoid
continuous UART0 logging after boot, and hardware validation must verify flashing
with GPS connected or document the required disconnect/jumper/retry procedure.

### Other Pins

- Battery ADC: `BAT_ADC = GPIO34`.
- RGB LED: `R = GPIO17`, `G = GPIO22`, `B = GPIO16`.
- Audio reserved: `AUDIO_IN = GPIO26`, `AUDIO_EN = GPIO4`.
- Expansion SPI reserved: `SPI_CS = GPIO27`.
- SD SPI reserved: `MOSI = GPIO23`, `MISO = GPIO19`, `SCK = GPIO18`,
  `CS = GPIO5`.

Notes:

- `GPIO34` is ADC1 and suitable for battery measurement while Wi-Fi is active.
- `GPIO26` is ADC2 and may conflict with Wi-Fi; audio is out of MVP scope.
- `GPIO4` and `GPIO5` are boot-related pins; boot must be validated with audio
  enable and SD CS attached even though SD/audio are not implemented in MVP.

## Functional Requirements

- Read and parse GPS NMEA data from the 336H module.
- Display GPS speed and fix state on the TFT dashboard.
- Connect to Ant BMS over BLE as a central/GATT client.
- Scan or bind BMS devices by advertised name/MAC, prioritizing devices whose
  name begins with `ANT-`.
- Discover BMS service `0xFFE0` and characteristic `0xFFE1`.
- Subscribe to BMS notifications and write request frames.
- Decode and display key BMS telemetry:
  - pack voltage
  - current
  - SOC
  - cell count
  - cell voltages
  - min/max/delta cell voltage
  - temperatures
  - charge/discharge MOS status
  - balancer status
  - battery status
  - capacity remaining
  - device model/software version where available
- Provide touchscreen settings for:
  - Wi-Fi provisioning/reprovisioning
  - screen brightness
  - screen orientation: portrait / landscape / inverted portrait /
    inverted landscape
  - speed unit: `km/h` / `mph`
  - BMS MAC address or scan/bind flow
  - restore defaults
- Serve a local web UI from the ESP32 for Wi-Fi and device settings.
- Persist settings across reboot:
  - setup AP password
  - external Wi-Fi SSID/password
  - brightness
  - screen orientation
  - speed unit
  - BMS MAC/binding
  - optional touch calibration
- Generate a random setup AP password on first boot.
- Display a Wi-Fi QR code on TFT for automatic phone joining to setup AP.
- Keep the setup AP active only during first provisioning or explicit
  touchscreen reprovisioning.
- Turn setup AP off after successful external Wi-Fi connection.
- Include a build-time configured OTA version-service URL. Early development may
  use a test URL; production firmware must set the real public endpoint.
- Check the version service when connected to internet-capable Wi-Fi.
- Stream firmware from the manifest `firmware_url` into the inactive OTA app
  slot; do not buffer the full firmware in RAM.
- Verify downloaded firmware with SHA-256 before activating the new partition.
- Keep USB flashing as the required recovery path.

## MVP Constraints

- Use OTA-capable app partitions from the beginning.
- Fit firmware plus embedded web assets within the selected 4 MB flash partition
  plan.
- Avoid large web assets: no frontend framework runtime, no large fonts, no
  images, no icon packs, no charting libraries.
- Avoid a full-screen display framebuffer; use small line/tile buffers and
  partial redraw.
- Keep BMS behavior read-oriented in the first MVP. BMS register writes are out
  of scope until read telemetry is stable.
- Keep SD card, audio, mobile app, cloud telemetry, CAN bus, and historical trip
  logging out of first MVP.

## Acceptance Criteria

- [ ] A Rust `esp-hal` firmware scaffold exists for ESP32-WROOM-32E and has
      documented build/flash commands.
- [ ] The central board pin module contains the full user-provided pin map.
- [ ] The firmware boots on the target board with the TPM408 attached.
- [ ] The ST7789 display initializes at 240 x 320 with correct BGR colors.
- [ ] The XPT2046 touch controller reports calibrated screen coordinates.
- [ ] GPS NMEA data from 336H is parsed and speed is shown on the TFT dashboard.
- [ ] Ant BMS BLE scan/connect discovers service `0xFFE0` and characteristic
      `0xFFE1`.
- [ ] Ant BMS telemetry frames are assembled, CRC-checked, decoded, and
      displayed.
- [ ] Touch settings include Wi-Fi provisioning/reprovisioning, brightness,
      screen orientation, speed unit, BMS scan/bind or MAC entry, and restore
      defaults.
- [ ] Settings persist across reboot and can be restored to defaults.
- [ ] First boot starts a password-protected setup AP, displays a scannable
      Wi-Fi QR code, and serves a local setup page.
- [ ] The local setup page can update the ESP32 setup AP password and external
      Wi-Fi SSID/password.
- [ ] After successful external Wi-Fi connection, setup AP turns off and can be
      re-enabled only from touchscreen reprovisioning.
- [ ] Device-hosted web assets are plain HTML/CSS/small vanilla JS embedded in
      the firmware app image.
- [ ] OTA checks a public version-service manifest, downloads the firmware URL,
      verifies SHA-256, switches OTA slot, and reboots into the new firmware.
- [ ] Interrupted or bad-hash OTA leaves the current firmware bootable.
- [ ] The display remains responsive while GPS and BMS data refresh.
- [ ] Validation commands are documented and run, or unavailable hardware /
      toolchain validation is explicitly marked pending.

## Out Of Scope For First MVP

- Server-hosted management frontend.
- React/Vue/Svelte/Preact or other web framework for device-hosted UI.
- LVGL or another TFT widget framework.
- Mobile app integration.
- Cloud telemetry.
- CAN bus.
- Multi-board automatic hardware detection.
- SD card storage/logging.
- Audio input/output.
- Full historical trip logging.
- BMS protection-parameter writes.
- Firmware signing enforcement.
- Local web upload OTA unless internet-based OTA proves insufficient.

## Validation Pending

These are not product-scope questions, but they must be validated during design
spike or early implementation:

- Actual Ant BMS advertised BLE name and MAC address for first hardware test.
- BLE central/GATT client over `esp-radio` + TrouBLE against the real BMS.
- Wi-Fi AP+STA coexistence with BLE on ESP32-WROOM-32E.
- Cold boot with GPIO12/GPIO15/GPIO2 attached to the TFT.
- Cold boot with GPIO4 audio enable and GPIO5 SD CS attached.
- Flashing/logging behavior with GPS bidirectionally wired to UART0.
- OTA app-slot size with embedded web assets.
- HTTPS/TLS memory and flash cost on the 4 MB ESP32-WROOM-32E target.

## Deployment Inputs

- Exact public OTA version-service endpoint for production firmware.

## Evidence

- Repository inspection on 2026-07-04: no firmware source or build config found.
- Espressif's ESP32-WROOM-32E documentation describes the module family as
  Wi-Fi + Bluetooth + Bluetooth LE. Common suffixes include 4 MB / 8 MB / 16 MB
  flash options, and some `R2` variants include 2 MB PSRAM.
- `syssi/esphome-ant-bms` documents Ant BMS monitoring over UART or BLE and
  provides an `ant_bms_ble` component.
- `syssi/esphome-ant-bms` BLE examples scan for names beginning with `ANT-`,
  connect by MAC address, use service UUID `0xFFE0`, characteristic UUID
  `0xFFE1`, and poll at a 5s interval.
- `syssi/esphome-ant-bms` BLE frames begin with `0x7E 0xA1`, end with
  `0xAA 0x55`, use CRC16 validation, and include status command `0x01`,
  device-info command `0x02`, and write-register command `0x51`.
- `esp-rs/esp-hal` is the selected lower-level `no_std` HAL path.
- `esp-rs/esp-hal` currently includes `esp-radio`, which lists ESP32 support for
  Wi-Fi, BLE, Wi-Fi+BLE coexistence, and ESP-NOW. `esp-radio` requires enabling
  the `unstable` feature on `esp-hal` and recommends TrouBLE for Bluetooth.
- `embassy-rs/trouble` includes ESP32 examples for BLE central, peripheral,
  scanner, and L2CAP use cases.
- `esp-rs/esp-hal` includes Wi-Fi AP+STA and OTA examples. The OTA example uses
  `esp-storage` and `esp-bootloader-esp-idf`, with a sample 4 MB partition table
  containing factory, `ota_0`, and `ota_1` app slots of 1 MB each.
