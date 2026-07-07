# Tech Stack

- Firmware stack: ESP-IDF CMake app, currently validated with ESP-IDF v5.5.4.
- Target chip in hardware logs: ESP32-D0WD-V3 rev v3.1, 4 MB flash.
- Runtime language: C in ESP-IDF components; do not reintroduce Rust/Cargo firmware paths.
- Display stack: LVGL 9.5.0, `espressif/esp_lvgl_adapter`, ST7789 SPI TFT, XPT2046 touch.
- Wireless/runtime stack: ESP-IDF Wi-Fi SoftAP/HTTP server, NimBLE BLE client for ANT BMS.
- Embedded Web UI: single framework-free `main/web/index.html` asset, vanilla HTML/CSS/JS embedded by the runtime component.
- Trellis Python scripts under `.trellis/scripts/` are workflow tooling, not product runtime.
