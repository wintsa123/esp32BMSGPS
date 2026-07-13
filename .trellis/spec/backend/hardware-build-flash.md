# Firmware Hardware, Build, and Flash Contract

This document is the project-level source of truth for active GPIO assignments,
reserved board pins, build inputs, partition layout, and supported flash paths.
README files link here instead of copying the full pin/build matrix.

## Scenario: ESP32 Firmware Hardware And Build Boundary

### 1. Scope / Trigger

- Apply this contract when changing GPIO assignments, board wiring, peripheral
  ownership, `sdkconfig*`, `partitions.csv`, component dependencies, CMake
  inputs, build scripts, flash scripts, or device runtime behavior.
- The code locations in the tables below are authoritative for active firmware
  assignments. This spec records the cross-component contract and hazards.
- A GPIO change is incomplete until both the code authority and this document
  agree. Do not add a second full pin table to either README.

### 2. Signatures

#### Active GPIO assignments

| Function | GPIO | Code authority |
| --- | --- | --- |
| TFT MISO / MOSI / SCLK / CS / DC | 12 / 13 / 14 / 15 / 2 | `ESP_BMS_LVGL_BRIDGE_DEFAULT_CONFIG()` in `components/esp_bms_lvgl_bridge/include/esp_bms_lvgl_bridge.h` |
| TFT reset / backlight | not connected / 21 | Same bridge default config; backlight uses LEDC channel 0 in `esp_bms_lvgl_bridge.c` |
| Touch IRQ / MISO / MOSI / CS / SCLK | 36 / 39 / 32 / 33 / 25 | Same bridge default config |
| Local battery ADC | 34 (`ADC1_CH6`) | `BATTERY_GPIO` and ADC initialization in `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c` |
| GPS module TX to ESP32 UART1 RX | 27 | `GPS_UART_RX_GPIO` in `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c` |
| GPS module RX from ESP32 UART1 TX | 18 | `GPS_UART_TX_GPIO` in the same runtime source |
| GPS PPS | 35 | `GPS_PPS_GPIO` in the same runtime source |
| Audio DAC / amplifier enable | 26 / 4 | `AUDIO_DAC_GPIO` and `AUDIO_ENABLE_GPIO` in `components/esp_bms_audio_feedback/esp_bms_audio_feedback.c` |

GPS UART1 currently runs at `115200` baud. GPIO35, GPIO36, and GPIO39 are
input-only; GPIO35 has no internal pull-up or pull-down. PPS must be a single
signal no higher than 3.3 V.

#### Reserved board assignments

These pins describe the board plan but are not active peripheral configuration
unless a source file explicitly enables them:

| Reserved function | GPIO | Conflict / requirement |
| --- | --- | --- |
| RGB LED R / G / B | 17 / 22 / 16 | No active RGB driver in the current firmware |
| Expansion SPI CS | 27 | Conflicts with the active GPS UART1 RX assignment |
| TF-card MOSI / MISO / SCLK / CS | 23 / 19 / 18 / 5 | GPIO18 conflicts with active GPS UART1 TX; resolve ownership before enabling TF |

GPIO2, GPIO4, GPIO5, GPIO12, and GPIO15 can affect ESP32 boot strapping or
attached-device boot behavior. Cold-boot validation is required whenever the
connected hardware, pull resistors, or peripheral power sequencing on these
pins changes.

#### Build and flash commands

```bash
# Normal firmware build
./scripts/esp-idf-env.sh build

# Local Unix-like serial flash and monitor
./scripts/esp-idf-env.sh -p /dev/ttyUSB0 flash monitor

# Fixed project RFC2217 bridge
./scripts/esp-idf-env.sh \
  -p "rfc2217://192.168.2.10:4000?ign_set_control" \
  -b 115200 flash monitor

# Drag diagnostic image, isolated from the normal build/sdkconfig
./scripts/esp-idf-drag-diag.sh build
./scripts/esp-idf-drag-diag.sh --double-buffer build
```

```powershell
# Windows local flash
.\scripts\flash.ps1 -Port COM3 -Monitor

# Windows side of the fixed RFC2217 bridge
.\scripts\serial_tcp_bridge.ps1 -PortName COM3
```

Companion builds:

```bash
npm ci --prefix vercel
npm run typecheck --prefix vercel
npm run build --prefix vercel

./scripts/build-android-cast.sh
RUN_TESTS=1 ./scripts/build-android-cast.sh
```

### 3. Contracts

#### Toolchain and dependency contract

- Firmware is a pure ESP-IDF CMake application. Do not reintroduce a
  Rust/Cargo firmware path.
- `main/idf_component.yml` requires ESP-IDF `>=5.5`; the current project helper
  and development environment target ESP-IDF `5.5.4`.
- `scripts/esp-idf-env.sh` loads `$IDF_PATH/export.sh` when available, otherwise
  `$HOME/esp/esp-idf-v5.5.4/export.sh`, then forwards arguments to `idf.py`.
- The helper supplies localhost proxy defaults when the corresponding proxy
  variables are unset. Override `http_proxy`, `https_proxy`, and `all_proxy`
  explicitly on hosts that use a different proxy path.
- Declare direct managed-component requirements in `main/idf_component.yml`.
  Treat `dependencies.lock` as the resolved version authority; do not maintain
  a second dependency-version list in README.
- The root `CMakeLists.txt` intentionally sets `COMPONENTS main`; component
  reachability comes from `main/CMakeLists.txt` and component `REQUIRES`.

#### Target and partition contract

- Target hardware is ESP32-WROOM-32E revision 3 or newer, 4 MB Flash, with no
  PSRAM. `sdkconfig.defaults` is the source of truth for these constraints.
- `partitions.csv` is the custom partition-table source:
  - `nvs`: `0x9000`, size `0x4000`
  - `otadata`: `0xd000`, size `0x2000`
  - `phy_init`: `0xf000`, size `0x1000`
  - `ota_0`: `0x10000`, size `0x1E0000`
  - `ota_1`: `0x1F0000`, size `0x1E0000`
- `0x3F0000..0x400000` remains unallocated for a future settings or reserved
  partition. Do not consume it without updating the partition contract and
  validating both OTA slots.
- When a device previously used another partition table, erase Flash once or
  flash the new bootloader, partition table, and app together before judging
  boot behavior.

#### Flash transport contract

- Local paths may use a real serial device such as `/dev/ttyUSB0` or `COM3`.
- The project remote bridge is
  `rfc2217://192.168.2.10:4000?ign_set_control`, backed by Windows `COM3` and
  `scripts/serial_tcp_bridge.ps1`.
- Use RFC2217, not `socket://`, because esptool requires DTR/RTS line control.
- Use explicit `-b 115200` on the fixed bridge and allow only one bridge client
  at a time.
- Documentation-only, preview-only, Trellis/spec, and agent-file changes do not
  require a firmware build or hardware flash unless the user explicitly asks.
  Firmware-impacting changes require the normal build and the project hardware
  validation flow before completion.

### 4. Validation & Error Matrix

| Condition | Required response |
| --- | --- |
| `idf.py` is missing | Set `IDF_PATH` or install ESP-IDF 5.5.4; do not substitute another build system |
| Managed-component download fails | Check the explicit proxy environment and `dependencies.lock` before changing dependency versions |
| RFC2217 TCP port is closed | Check the Windows bridge process, firewall scope, host IP, and port 4000 |
| RFC2217 connects but flash sync fails | Close other clients, confirm the server is RFC2217 rather than raw TCP, and keep `-b 115200` |
| Firmware flashes but boot loops after a partition change | Erase Flash once, then flash the complete ESP-IDF image set |
| TFT or touch does not initialize | Compare wiring with the active bridge macro and validate boot-strapping pins under cold boot |
| GPS UART receives no bytes | Confirm crossed TX/RX, UART1 GPIO27/GPIO18 ownership, 115200 baud, power, and signal level |
| PPS never triggers | Check GPS fix/PPS output and GPIO35 voltage; do not enable nonexistent internal pulls |
| Vercel page cannot reach `192.168.4.1` | Join the Setup AP and grant browser local/private-network permission; do not proxy device credentials through Vercel |
| Android build cannot find its toolchain | Provide Android SDK 35 and Java 17; keep Gradle selection inside `scripts/build-android-cast.sh` |

### 5. Good / Base / Bad Cases

- Good: change the GPIO macro in its owning component, update this table, run
  the firmware build, flash through the correct transport, and validate the
  affected hardware plus cold boot when a strapping pin is involved.
- Base: change only README or Trellis/spec documentation, validate links and
  Markdown, and do not flash unchanged firmware.
- Bad: copy a pin map into README, edit only that copy, enable TF-card GPIO18
  while GPS still owns it, or use `socket://` for an esptool flash.

### 6. Tests Required

- Firmware source, component, `sdkconfig*`, partition, embedded Web asset, or
  build-input changes:

```bash
./scripts/esp-idf-env.sh build
node .gitnexus/run.cjs detect-changes -r esp32BMSGPS
```

- Hardware-impacting changes: flash through the fixed RFC2217 bridge unless the
  user selects another explicit port, then inspect boot logs and the affected
  peripheral. Pin changes involving GPIO2/4/5/12/15 require a power-cycle test.
- Vercel control-page changes:

```bash
npm run typecheck --prefix vercel
npm run build --prefix vercel
```

- Android casting changes:

```bash
RUN_TESTS=1 ./scripts/build-android-cast.sh
```

- README/spec-only changes: check local links, language parity, command spelling,
  and `git diff --check`; no firmware flash is required.

### 7. Wrong vs Correct

#### Wrong

```markdown
<!-- A second pin table in README becomes stale while code changes. -->
GPS RX: GPIO3 at 9600 baud
```

```bash
# Raw TCP cannot provide the line control expected by esptool.
idf.py -p socket://192.168.2.10:4000 flash
```

#### Correct

```c
/* Active assignment stays in the owning runtime component. */
#define GPS_UART_PORT UART_NUM_1
#define GPS_UART_RX_GPIO 27
#define GPS_UART_TX_GPIO 18
#define GPS_UART_BAUD 115200
```

```bash
./scripts/esp-idf-env.sh \
  -p "rfc2217://192.168.2.10:4000?ign_set_control" \
  -b 115200 flash
```

README links to this contract and the owning source files instead of repeating
the complete matrix.
