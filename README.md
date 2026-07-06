# ESP32 BMS GPS Firmware

ESP-IDF firmware for an ESP32-WROOM-32E dashboard with GPS speed, TPM408
TFT/touch, local setup AP/Web UI, local battery ADC, and OTA-ready partitioning.

The target firmware is now built and flashed only through ESP-IDF. The previous
Rust/Cargo firmware path has been removed.

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

Boot validation is required with GPIO2, GPIO4, GPIO5, GPIO12, and GPIO15
attached. GPS on UART0 can interfere with flashing and logs.

## Build Prerequisites

Install ESP-IDF 5.5.x. The helper script loads `$IDF_PATH/export.sh` when
available, otherwise `$HOME/esp/esp-idf-v5.5.4/export.sh`.

The ESP-IDF component lock currently pins:

- `lvgl/lvgl` 9.5.0
- `espressif/esp_lvgl_adapter` 0.6.2
- `atanisoft/esp_lcd_touch_xpt2046` 1.0.6

## Build And Flash

Build the ESP-IDF app:

```bash
./scripts/esp-idf-env.sh build
```

Flash from a Unix-like shell:

```bash
./scripts/esp-idf-env.sh -p /dev/ttyUSB0 flash monitor
```

On Windows, the flash helper wraps the same ESP-IDF flow:

```powershell
.\scripts\flash.ps1 -Port COM3
.\scripts\flash.ps1 -Port COM3 -Monitor
```

For a raw TCP serial bridge, use `socket://`, not `rfc2217://`. Raw TCP cannot
change the Windows-side serial baud rate, so the flash baud must match the
bridge process. `scripts/flash.ps1` defaults socket ports to `-b 115200`:

```powershell
.\scripts\flash.ps1 -Port socket://192.168.2.10:4000
.\scripts\flash.ps1 -Port socket://192.168.2.10:4000 -Monitor
.\scripts\flash.ps1 -Port socket://192.168.2.10:4000 -BaudRate 115200
```

The raw bridge in `scripts/serial_tcp_bridge.ps1` can reset the ESP32 into the
bootloader on the first TCP write, but it cannot expose RFC2217 line-control
commands to esptool.

In non-interactive shells where `idf.py monitor` refuses to run without a TTY,
capture the same raw socket with:

```bash
python3 scripts/raw-socket-monitor.py socket://192.168.2.10:4000 --duration 30
```

## Runtime Status

Implemented on the ESP-IDF path:

- `app_main` orchestration in `main/idf_main.c`.
- LVGL 9.5 display path through Espressif's LVGL adapter.
- ST7789 SPI display bridge and XPT2046 touch registration.
- GPIO21 backlight brightness through LEDC PWM.
- LVGL dashboard/settings UI with brightness, rotation, speed unit, language,
  setup AP QR, and BMS bind actions.
- Display settings persisted in NVS.
- GPIO34 local battery sampling through ADC1.
- UART0 RX GPIO3 NMEA RMC parsing for GPS speed/fix.
- Native ESP-IDF setup AP:
  - SSID policy: `fuckingBms_` plus six lowercase hex characters.
  - Password policy: eight random digits.
  - AP/server/gateway: `192.168.4.1/24`.
  - DHCP provided by ESP-IDF AP netif.
- Setup AP credentials persisted in NVS, with stale values regenerated and
  saved before applying Wi-Fi config.
- Embedded Web UI served from `main/web/index.html` through
  `esp_http_server`.
- Authenticated API routes for status, config, external Wi-Fi, AP password, BMS
  scan trigger/candidates, and BMS bind persistence.
- External Wi-Fi credentials persisted in NVS through `/api/wifi`; when present,
  the firmware starts ESP-IDF Wi-Fi in AP+STA mode so the setup AP stays
  available while station connection is attempted.
- NimBLE-backed BMS discovery: `/api/bms/scan` starts a BLE scan and
  `/api/bms/candidates` returns deduplicated ANT BMS candidates from live
  advertisements.
- Bound BMS connection flow: the ESP-IDF runtime connects to a saved or newly
  bound ANT BMS MAC, discovers service `0xFFE0` / characteristic `0xFFE1`,
  subscribes to notifications, writes status/device-info request frames, and
  maps valid status notifications into the TFT/Web dashboard snapshot.

Still pending on the ESP-IDF path:

- OTA manifest parsing, image download, verification, partition switch, and
  post-boot validity marking. `/api/ota/check` and `/api/ota/start` currently
  return an explicit unavailable response instead of reporting fake success.
- Hardware validation of the new BMS BLE connection and notification telemetry
  path with a real ANT BMS.

## Flash Layout

`partitions.csv` uses a 4 MB two-OTA layout:

- `ota_0`: offset `0x10000`, size `0x1E0000`
- `ota_1`: offset `0x1F0000`, size `0x1E0000`
- `nvs`, `otadata`, and `phy_init` data partitions before `ota_0`
- `0x3F0000..0x400000` is intentionally left free for a future settings or
  reserved partition

When flashing a board that previously used a different partition table, erase
flash once or flash the new partition table together with the app before judging
boot behavior.

## Diagnostics

The IDF boot log prints heap snapshots at `runtime_init`, `lvgl_bridge`,
`first_ui`, `display_settings`, and `setup_ap_http`. Use `dma_free` and
`dma_largest` before enabling double buffering or increasing the SPI draw
buffer height on hardware.

Drag diagnostics are disabled by default:

- `CONFIG_ESP_BMS_LVGL_UI_DRAG_DIAGNOSTICS`
- `CONFIG_ESP_BMS_LVGL_UI_DRAG_SAMPLE_DIAGNOSTICS`

Use the drag diagnostic helper for separate diagnostic images:

```bash
./scripts/esp-idf-drag-diag.sh build
./scripts/esp-idf-drag-diag.sh -p /dev/ttyUSB0 flash monitor
./scripts/esp-idf-drag-diag.sh --double-buffer build
```

## Web UI

The embedded Web UI is a framework-free HTML/CSS/vanilla JS page in
`main/web/index.html`. It is embedded into the firmware image by
`components/esp_bms_idf_runtime/CMakeLists.txt`.

Default UI language is Chinese. English is selectable from device settings.

## Vercel Control Page

The repository also contains a browser-hosted control page under
`vercel/index.html`. Deploy the repo to Vercel, join the phone/laptop to the
ESP32 setup AP, and press `连接热点 API`. The page prompts for the current setup
password shown on the TFT QR screen and sends it as both `X-Setup-Password` and
HTTP Basic auth.

The ESP32 HTTP API returns CORS plus Private Network Access headers so a public
HTTPS Vercel origin can call `http://192.168.4.1`.

## Current Validation

Primary validation:

```bash
./scripts/esp-idf-env.sh build
```

Hardware validation remains pending for GPS UART0, GPIO34 ADC scaling against a
multimeter, phone scan and join of the setup AP shown on the TFT QR screen,
HTTP access to `http://192.168.4.1`, BLE BMS, and OTA slot switching.
