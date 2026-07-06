# Logging Guidelines

> How logging is done in this project.

## Overview

The ESP32 target uses ESP-IDF logging (`ESP_LOGI`, `ESP_LOGW`, `ESP_LOGE`) and
short module tags such as `bms_idf_main`, `bms_idf_runtime`, `bms_lvgl_bridge`,
and `bms_lvgl_ui`.

## Required Logs

- Log boot/runtime milestones and bounded heap snapshots.
- Log Wi-Fi desired mode, setup AP SSID, external station SSID, and password
  lengths only.
- Log AP start/stop separately from config acceptance.
- Log SoftAP client connect/disconnect separately from DHCP lease assignment.
- Log HTTP server start and route-not-implemented cases.
- Log settings persistence failures with `esp_err_to_name(ret)`.
- Log BLE and OTA state transitions when those subsystems are implemented.

## Forbidden Logs

- Never log setup AP passwords.
- Never log external Wi-Fi passwords.
- Never log OTA credentials or token-bearing URLs.
- Never log raw HTTP request bodies.

## Validation

- Setup AP success should show mode/config, AP started, and DHCP lease logs
  when a phone joins.
- Station credentials should log SSID and password length only.
- HTTP failures should report route/status without dumping request bodies.
