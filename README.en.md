# ESP32 BMS GPS

[简体中文](./README.md) · [English](./README.en.md)

ESP32 smart dashboard firmware for electric motorcycles, e-bikes, and light vehicles. It brings BMS, motor-controller, GPS, touchscreen, device hotspot, Web control, and phone casting into one system.

> The project is under active development and hardware validation. The core firmware and primary interaction paths are usable; OTA, track storage, and some hardware compatibility work are not complete.

## Goals

- Provide glanceable speed, BMS, controller, and GPS dashboards on a 240 × 320 TFT.
- Connect to protection boards and controllers over BLE without fabricating telemetry.
- Support configuration, diagnostics, and maintenance through the device Setup AP, embedded Web UI, and public HTTPS control page.
- Extend maps, navigation, and complex views through low-latency Android casting.
- Remain bootable, recoverable, and maintainable within the ESP32-WROOM-32E limits of 4 MB Flash and no PSRAM.
- Use Chinese by default, with English selectable from device settings.

## Online Control Page

**Vercel control page: <https://esp-bms-setting.vercel.app/>**

To control the device through its hotspot API:

1. Enable the device hotspot in TFT settings and view its QR code, SSID, and password.
2. Connect the phone or computer to that hotspot.
3. Open the control page, allow local-network access, and connect to `http://192.168.4.1`.

The control page is Chinese-first and can switch to English. The hotspot HTTP API is the primary working transport. A Web Bluetooth entry also exists, but it requires the matching firmware-side BLE control service. The `/cast` path launches the Android casting app.

## Implementation Status

| Area | Status | Current scope |
| --- | --- | --- |
| ESP-IDF foundation | Implemented | Pure ESP-IDF/CMake migration, boot orchestration, NVS, dual OTA-slot foundation, and runtime snapshot |
| TFT and touch | Implemented, still being refined | ST7789, XPT2046, LVGL dashboards/settings, rotation, brightness, touch calibration, and quick panel |
| Setup AP and embedded Web | Implemented | Random hotspot credentials, QR, `192.168.4.1`, configuration APIs, and BMS scan/bind entry points |
| ANT BMS | Code implemented; full hardware validation pending | BLE scan, bind, connect, subscribe, poll, and status-frame parsing; long-running validation with a real board continues |
| FarDriver controller | Core path implemented | BLE protocol, telemetry, tire parameters, and gear-ratio conversion; device compatibility and calibration continue |
| GPS / PPS | Core path implemented | 336H UART NMEA, RMC speed/fix/UTC, and GPIO35 PPS diagnostics; track storage and maps are not complete |
| Audio feedback | Implemented | GPIO26 DAC and GPIO4 amplifier enable for connection prompts and volume feedback |
| Vercel control page | Deployed | React control UI supports the hotspot API; Web Bluetooth still depends on firmware service support |
| Android casting | In development | A standalone Kotlin app and casting protocol exist; latency, stability, and device compatibility are being refined |
| OTA / TF card / tracks and maps | Not complete | OTA does not yet provide a complete upgrade loop; TF-card recording, history, and maps are later phases |

“Implemented” means the code path exists; it does not mean every target hardware combination has completed long-duration validation.

## Target Hardware and GPIO Configuration

- MCU: ESP32-WROOM-32E, 4 MB Flash, no PSRAM.
- Display: TPM408 2.8-inch ST7789, 240 × 320, BGR.
- Touch: XPT2046 / XP2046.
- GPS: 336H, UART NMEA plus PPS.
- BMS: ANT BMS BLE is the current priority; the controller protocol is FarDriver BLE.

The GPIO map is intentionally not duplicated in the README. Code configuration lives in:

- Display, touch, and backlight: [`components/esp_bms_lvgl_bridge/include/esp_bms_lvgl_bridge.h`](./components/esp_bms_lvgl_bridge/include/esp_bms_lvgl_bridge.h)
- GPS, PPS, and local battery ADC: [`components/esp_bms_idf_runtime/esp_bms_idf_runtime.c`](./components/esp_bms_idf_runtime/esp_bms_idf_runtime.c)
- Audio DAC and amplifier enable: [`components/esp_bms_audio_feedback/esp_bms_audio_feedback.c`](./components/esp_bms_audio_feedback/esp_bms_audio_feedback.c)
- Full pin map, conflicts, and build contract: [`hardware-build-flash.md`](./.trellis/spec/backend/hardware-build-flash.md)

When a GPIO changes, update both its code authority and the project spec. Do not update the README alone.

## Development Stack

| Layer | Technology |
| --- | --- |
| Firmware | C, ESP-IDF 5.5.x (current environment: 5.5.4), FreeRTOS, CMake / `idf.py` |
| Display | LVGL 9.5, `esp_lvgl_adapter`, `esp_lcd`, ST7789, XPT2046 |
| Device services | NimBLE, Wi-Fi SoftAP, `esp_http_server`, NVS, UART NMEA, ADC, LEDC, DAC |
| Embedded Web | Single-page HTML / CSS / vanilla JavaScript embedded in the firmware image |
| Vercel control page | React 19, TypeScript, Vite 6, Vercel |
| Android casting | Kotlin, Android SDK 35, Java 17, Gradle 8.14.2 |
| Quality and workflow | GitNexus, Trellis, host protocol self-tests, ESP-IDF builds, and hardware logs |

Dependency versions, partitions, diagnostic images, and platform build commands are defined in the [project build contract](./.trellis/spec/backend/hardware-build-flash.md).

## Flashing

Install ESP-IDF 5.5.x. The repository wrapper first loads `$IDF_PATH/export.sh`, then falls back to `$HOME/esp/esp-idf-v5.5.4/export.sh`.

Linux local serial:

```bash
./scripts/esp-idf-env.sh -p /dev/ttyUSB0 flash monitor
```

Windows local serial:

```powershell
.\scripts\flash.ps1 -Port COM3 -Monitor
```

Use the project's fixed LAN RFC2217 bridge:

```bash
./scripts/esp-idf-env.sh \
  -p "rfc2217://192.168.2.10:4000?ign_set_control" \
  -b 115200 flash monitor
```

Start the bridge on Windows with:

```powershell
.\scripts\serial_tcp_bridge.ps1 -PortName COM3
```

Erase Flash once when switching from a different partition table. See the [firmware hardware, build, and flash contract](./.trellis/spec/backend/hardware-build-flash.md) for build-only commands, erase flow, diagnostic images, partition layout, and troubleshooting.

## Repository Layout

```text
main/                         Boot entry point and embedded Web UI
components/                   Runtime, display bridge, LVGL UI, protocol, and audio components
android-cast/                 Android low-latency casting app
vercel/                       Standalone Vercel control-page frontend
scripts/                      Build, flash, serial bridge, and diagnostic tools
tests/                        Host-runnable protocol and logic self-tests
.trellis/spec/                Project engineering specifications and executable contracts
preview/                      The only location for UI preview images
```

`main/idf_main.c` owns boot orchestration only. Hardware, protocol, state, and UI logic belong in focused ESP-IDF components.

## License

This project is available under the [PolyForm Noncommercial License 1.0.0](./LICENSE). You may use, modify, and distribute it only for noncommercial purposes. Commercial use requires separate written permission from the copyright holder.
