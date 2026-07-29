# S3 S1000RR 静态仪表底图优化

## Goal

Reduce LVGL CPU work for the S3 480x320 S1000RR speed dashboard by drawing its unchanging gauge decoration from an RGB565 snapshot image. Keep all live telemetry and interaction behavior unchanged.

## Background

- The legacy ESP32 320x240 S1000RR dashboard already uses an embedded RGB565 static background.
- On S3 480x320, the dashboard draw callback still redraws static gauge borders, ticks, dividers, and inactive segments whenever its render signature changes.
- The native BMS and Fireblade dashboards already use 480x320 snapshot caches. They are out of scope.

## Requirements

1. Add a 480x320 S1000RR RGB565 static background used only for the S3 native landscape dashboard.
2. Preserve the live speed band, battery/SOC, GPS, controller status, text labels, page transitions, and portrait behavior.
3. Reuse the existing LVGL snapshot cache mechanism; do not add an embedded image asset, a renderer, or a configuration surface.
4. Keep the change limited to the LVGL UI component and its simulator stress assertion.

## Acceptance Criteria

- [x] On S3 480x320, the S1000RR background image replaces only the static gauge decoration.
- [x] Dynamic telemetry remains visible and updates correctly at 480x320.
- [x] Legacy ESP32 320x240 static-background behavior remains unchanged.
- [x] Portrait and non-native resolutions retain the existing procedural rendering path.
- [x] The headless simulator feature matrix and native carousel stress test pass.

## Out Of Scope

- BMS, controller, settings, BLE picker, and roller changes.
- A static cache for controller or portrait dashboards.
- New runtime settings, image dependencies, or redraw abstractions.

## Validation

- `cmake --build simulator/build -j2` passed.
- Headless S1000RR checks passed at `480x320`, `320x240`, `320x480`, and `240x320`; the native carousel stress test passed with 680 deferred snapshots.
- The S3 profile `phone-media-verify` built successfully with the 480x320 cache enabled.
- An RFC2217 flash attempt reached the bridge, but its attached device identifies as `ESP32`, not `ESP32-S3`; esptool stopped before writing. Validate on the matching S3 board before claiming physical-device coverage.
