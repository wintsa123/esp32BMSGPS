# ESP32 GPS TFT BMS Firmware Implementation Plan

## Preconditions

- Do not start implementation until this task is moved from `planning` to
  `in_progress` with `task.py start`.
- Inline Codex workflow skips sub-agent jsonl curation; load project specs with
  `trellis-before-dev` before editing firmware code.
- The repository currently has no firmware scaffold. First implementation work
  starts by creating the Rust embedded project structure.

## Execution Order

### 1. Project Scaffold

- Create a Rust `no_std` ESP32 project for ESP32-WROOM-32E using `esp-hal`.
- Add basic dependencies only:
  - `esp-hal`
  - `esp-backtrace`
  - `esp-println`
  - `embassy-*` pieces required by `esp-hal` examples
  - `esp-radio`
  - `esp-storage`
  - `esp-bootloader-esp-idf`
- Add build/flash documentation.
- Add a central board pin module with all user-provided GPIO mappings.
- Add initial partition table for 4 MB flash with OTA-capable app slots.
- Add release-profile settings that keep size controlled.
- Add a build-time `OTA_MANIFEST_URL` configuration value with a development
  placeholder until the production endpoint is available.

Validation:

- `cargo fmt`
- `cargo build --release`
- `espflash` image generation or flashing command documented.

Rollback point:

- Scaffold should boot a minimal firmware before any feature drivers are added.

### 2. Boot, GPIO, And Status Smoke Test

- Initialize board pins without touching reserved SD/audio functionality.
- Drive TFT backlight GPIO21.
- Add optional RGB status output if it does not increase scope materially.
- Validate cold boot with TFT attached.
- Validate boot with GPIO4 audio enable and GPIO5 SD CS attached.
- Document any required pullups/pulldowns or hardware workarounds.

Validation:

- Device cold boots repeatedly.
- Flashing succeeds with current GPS-on-UART0 wiring or documented mitigation.

Rollback point:

- If boot strap pins fail, stop feature work and revise wiring/design.

### 3. ST7789 Display Driver

- Implement or integrate ST7789 SPI display init for 240 x 320 BGR mode.
- Use GPIO13/14/15/2/21 and optional GPIO12 MISO only if needed.
- Draw basic test patterns and text.
- Add partial redraw/tile buffer path.
- Avoid full-screen framebuffer.

Validation:

- Correct color order.
- Screen orientation verified.
- No large RAM allocation.
- Backlight control verified.

### 4. XPT2046 Touch Driver

- Implement touch SPI read path on GPIO36/39/32/33/25.
- Read pressure/touch state and raw coordinates.
- Add calibration transform to screen coordinates.
- Add basic touch event abstraction: down, move, up, tap.

Validation:

- Touch points map to expected display corners.
- Touch IRQ behavior verified.
- Calibration can be persisted or uses documented defaults.

### 5. GPS UART0 Parser

- Configure UART0 for 9600 baud NMEA.
- Parse at least `$GPRMC` / `$GNRMC` or equivalent speed-bearing sentence.
- Publish speed, fix validity, timestamp, and optional satellite/fix metadata.
- Reduce or disable continuous UART0 firmware logs after boot.
- Avoid sending GPS configuration commands in first MVP unless required.

Validation:

- GPS NMEA stream parsed while dashboard is running.
- Flashing/logging conflict with UART0 is documented.
- Speed unit conversion tested by host parser unit tests where possible.

Rollback point:

- If UART0 conflicts block flashing, document required disconnect/jumper and
  pause integration until accepted.

### 6. Settings Persistence

- Implement versioned config structure.
- Store settings through a small flash-backed config area.
- Use checksum/CRC and generation counter or dual-slot records.
- Persist:
  - setup AP password
  - external Wi-Fi SSID/password
  - brightness
  - screen orientation
  - speed unit
  - BMS MAC/binding
  - optional touch calibration
- Add restore-defaults behavior.

Validation:

- Settings survive reboot.
- Corrupt record falls back safely.
- Secrets are not printed in logs.

### 7. Wi-Fi Setup AP And QR

- Generate random setup AP password on first boot.
- Start password-protected setup AP during first provisioning.
- Render Wi-Fi QR code on TFT.
- Display local setup URL as text.
- Shut setup AP down after successful external Wi-Fi connection.
- Re-enable setup AP only from touchscreen reprovisioning.

Validation:

- Phone can scan QR and join AP.
- First-time provisioning works without internet.
- Reprovisioning can be triggered from TFT.
- AP is off after successful external Wi-Fi connection.

### 8. Embedded Local Web UI/API

- Embed plain HTML/CSS/small vanilla JS in the firmware app image.
- Serve static UI from the device HTTP server.
- Add minimal JSON API:
  - `GET /api/status`
  - `GET /api/config`
  - `POST /api/config`
  - `POST /api/wifi`
  - `POST /api/ap-password`
  - `POST /api/bms/bind`
- Keep server single-connection or low-concurrency if needed for RAM.
- Keep web assets small and avoid external CDN dependencies.

Validation:

- Local setup page loads from phone while connected to setup AP.
- Wi-Fi credentials can be submitted and persisted.
- App binary size remains within OTA slot budget.

### 9. BLE GATT Client Spike

- Use `esp-radio` + TrouBLE to scan for BLE devices.
- Detect advertised names beginning with `ANT-`.
- Connect to the selected BMS MAC.
- Discover service `0xFFE0` and characteristic `0xFFE1`.
- Subscribe to notifications.
- Write a status request frame.
- Capture one raw status response frame.

Validation:

- BLE central/GATT client works against the actual BMS.
- Wi-Fi disabled and enabled cases are tested separately.
- If Wi-Fi/BLE coexistence fails, record the failure and adjust task scheduling.

Rollback point:

- If BLE GATT client is blocked in the `esp-hal` stack, stop and decide whether
  to use wired UART BMS, change hardware, or revisit ESP-IDF-based Rust.

### 10. Ant BMS Protocol Parser

- Port frame assembly and CRC rules from `syssi/esphome-ant-bms`.
- Keep parser independent from BLE transport.
- Decode MVP telemetry:
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
  - device model/software version
- Add host-side parser tests using captured/sample frames.

Validation:

- Parser rejects bad length/CRC.
- Parser handles notification chunking.
- Parser does not panic on short/malformed frames.

### 11. Dashboard And Touch Settings

- Implement dashboard with GPS speed and key BMS data.
- Implement settings screens:
  - Wi-Fi reprovisioning
  - brightness
  - screen orientation
  - speed unit
  - BMS scan/bind or MAC entry
  - restore defaults
- Do not add touchscreen OTA action in first MVP.
- Add user-visible offline/error states for GPS, BMS, Wi-Fi, and OTA.

Validation:

- Dashboard remains responsive while GPS and BMS refresh.
- Settings write persisted values and update UI.
- No layout overlap on 240 x 320 display.

### 12. OTA Version-Service Flow

- Define manifest schema in code and docs.
- Implement `POST /api/ota/check`.
- Implement `POST /api/ota/start`.
- Stream firmware from `firmware_url` to inactive app partition.
- Verify `sha256`.
- Activate next partition and reboot.
- Mark new firmware valid after startup checks.

Validation:

- OTA update succeeds using a test manifest and test firmware URL.
- Bad hash is rejected.
- Interrupted update leaves current firmware bootable.
- App binary plus embedded web assets fit the selected OTA partition.

### 13. Documentation

- Add README with:
  - hardware pin map
  - wiring caveats
  - build command
  - flash command
  - first-boot provisioning flow
  - OTA manifest format
  - known limitations
- Document UART0 GPS flashing/logging mitigation.
- Document SD/audio as reserved and out of MVP.

## Quality Gates

Run before reporting implementation complete:

- `cargo fmt --check`
- `cargo clippy --target xtensa-esp32-none-elf --release -- -D warnings` if
  toolchain support permits.
- `cargo build --target xtensa-esp32-none-elf --release`
- Host unit tests for pure parsers/config logic if present.
- Firmware image size check against OTA slot size.
- Hardware smoke tests:
  - cold boot
  - ST7789 color/orientation
  - XPT2046 touch calibration
  - GPS NMEA parse
  - setup AP + QR join
  - external Wi-Fi connect and AP shutdown
  - BMS BLE scan/connect/status frame
  - OTA hash failure and success path

If a command cannot run because the environment lacks the ESP Rust toolchain or
hardware, report that explicitly and mark the corresponding hardware validation
pending.

## Review Before `task.py start`

Before starting implementation, review these remaining decisions:

- Actual Ant BMS advertised name/MAC for hardware validation.
- Production OTA version-service URL, when available. A build-time development
  placeholder is acceptable for scaffold and early hardware work.
- HTTPS with SHA-256 is the MVP integrity target; firmware signing remains a
  production hardening follow-up.
- Final flashing/logging procedure for GPS bidirectional UART0 wiring.
