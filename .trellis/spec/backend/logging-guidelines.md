# Logging Guidelines

> How logging is done in this project.

## Overview

The ESP32 target uses ESP-IDF logging (`ESP_LOGI`, `ESP_LOGW`, `ESP_LOGE`) and
short module tags such as `bms_idf_main`, `bms_idf_runtime`, `bms_lvgl_bridge`,
and `bms_lvgl_ui`.

## Required Logs

- Log boot/runtime milestones and bounded heap snapshots.
- Log each non-empty local UI action from `main/idf_main.c` as a paired
  `resource action=<name> phase=begin|end` diagnostic. The end event covers a
  one-second post-action window (or ends early for the next action) and carries
  heap free/local-min values, `cpu_busy_pct`, and the top three non-idle tasks.
- Keep resource task snapshots static and bounded at 48 tasks. If the bound or
  local heap monitor is unavailable, log the unavailable field instead of a
  partial CPU ranking. FreeRTOS trace facility and run-time stats must stay
  enabled in every `config/sdkconfig/sdkconfig.defaults*` target variant.
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
