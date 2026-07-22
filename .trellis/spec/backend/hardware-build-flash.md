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

#### EasyEDA schematic-to-PCB sync contract

- `eda.pcb_Document.importChanges(schematicUuid)` opens/applies the schematic
  delta, but newly imported components may initially be parked outside the
  board outline. That position is staging data, not an accepted PCB layout.
- After every component import, move all new components inside the board and
  outside mechanical keepouts, then run strict PCB DRC. The increment is not
  accepted until `Clearance Error = 0` and keepout errors are zero for the new
  parts, even if every component origin is numerically inside the board bounds.
- Save the schematic and PCB, close and reopen the documents, then re-read the
  component, pad, and net counts plus the new component coordinates. Persisted
  readback is the authority; an unsaved canvas is not completion evidence.
- Snapshot the `.eprj2` database before routing experiments and verify the copy
  with SHA-256 plus `PRAGMA integrity_check`. Compare copper-line and via counts
  before and after every autorouter call.
- Treat EasyEDA native autorouting as unavailable when it returns
  `success=false`, `duration=0`, or lists all board nets despite a requested net
  subset. Do not keep tuning parameters or describe the board as routed when
  copper-line/via counts remain unchanged.
- The EasyEDA autorouter/manufacturing boundary requires one merged, closed
  board-outline object. Separate layer-11 `LINE` primitives are not sufficient,
  even when every endpoint is exact and `pcb_Document.zoomToBoardOutline()`
  succeeds. Merge/convert imported lines into one closed `POLY` on layer 11.
  After save/reopen, require `pcb_ManufactureData.getDsnFile()` to return a file
  whose structure contains `boundary(path ...)`; zoom success alone is not an
  autorouter acceptance test.
- Treat external SES import as an untrusted conversion boundary. Verify units,
  the routed-net allowlist, every copper layer, and strict DRC before retaining
  imported copper. EasyEDA may quantize widths and may drop or reinterpret
  bottom-layer SES paths. Roll back by exact copper-layer line IDs and via IDs;
  never bulk-delete `pcb_PrimitiveLine.getAll()` without a layer filter because
  that also returns board-outline and mechanical lines.
- When creating a cross-layer route through the primitive API, create the via
  before the line segments that terminate on it. Creating the via last can
  leave geometrically coincident top/bottom tracks electrically disconnected in
  the ratline graph. Read the actual pad layer before routing test points; a
  bottom-only SMD test pad is not connected by a top-layer track at the same XY.
- Keep the USB connector-side and MCU-side nets on separate rule boundaries
  across the 22-ohm series resistors. Use differential pairs
  `USB_CONN_DP_DN` (`USB_DP`/`USB_DN`) and `USB_MCU_DP_DN`
  (`USB_DP_MCU`/`USB_DN_MCU`), with matching equal-length groups
  `USB_CONN_LENGTH_MATCH` and `USB_MCU_LENGTH_MATCH`. Never place all four nets
  in one equal-length group.

#### Toolchain and dependency contract

- Firmware is a pure ESP-IDF CMake application. Do not reintroduce a
  Rust/Cargo firmware path.
- `main/idf_component.yml` requires ESP-IDF `>=6.0.2, <6.1.0`; the current
  project helper and development environment target ESP-IDF `6.0.2`.
- `scripts/esp-idf-env.sh` loads `$IDF_PATH/export.sh` when available, otherwise
  `$HOME/esp/esp-idf-v6.0.2/export.sh`, verifies the resolved version, then
  forwards arguments to `idf.py`.
- The helper supplies localhost proxy defaults when the corresponding proxy
  variables are unset. Override `http_proxy`, `https_proxy`, and `all_proxy`
  explicitly on hosts that use a different proxy path.
- Declare direct managed-component requirements in `main/idf_component.yml`.
  Treat `dependencies.lock` as the resolved version authority; do not maintain
  a second dependency-version list in README.
- The root `CMakeLists.txt` intentionally sets `COMPONENTS main`; component
  reachability comes from `main/CMakeLists.txt` and component `REQUIRES`.
- `main/CMakeLists.txt` has two component-closure inputs: a generated
  `ESP_BMS_PROFILE_MAIN_REQUIRES` for profile builds and
  `ESP_BMS_MAIN_REQUIRES_DEFAULT` for direct builds. Every module whose
  `ESP_BMS_FEATURE_*` default is `1` and whose registry template includes a
  component header must be in the default closure too. For example,
  `ESP_BMS_FEATURE_GPS=1` requires `esp_bms_gps`; otherwise direct IDF builds
  generate an `esp_bms_gps.h` include without making that header reachable.

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
| `idf.py` is missing | Set `IDF_PATH` or install ESP-IDF 6.0.2; do not substitute another build system |
| Managed-component download fails | Check the explicit proxy environment and `dependencies.lock` before changing dependency versions |
| Direct build reports a missing module header from `esp_bms_module_registry.c` | Compare default `ESP_BMS_FEATURE_*` values, registry template includes, and `ESP_BMS_MAIN_REQUIRES_DEFAULT`; add the omitted owning component and a configurator self-test assertion |
| RFC2217 TCP port is closed | Check the Windows bridge process, firewall scope, host IP, and port 4000 |
| RFC2217 connects but flash sync fails | Close other clients, confirm the server is RFC2217 rather than raw TCP, and keep `-b 115200` |
| Firmware flashes but boot loops after a partition change | Erase Flash once, then flash the complete ESP-IDF image set |
| TFT or touch does not initialize | Compare wiring with the active bridge macro and validate boot-strapping pins under cold boot |
| GPS UART receives no bytes | Confirm crossed TX/RX, UART1 GPIO27/GPIO18 ownership, 115200 baud, power, and signal level |
| PPS never triggers | Check GPS fix/PPS output and GPIO35 voltage; do not enable nonexistent internal pulls |
| EasyEDA import succeeds but new parts are outside the board | Move them into an approved functional area, check mechanical keepouts, run strict DRC, then save/close/reopen and re-read coordinates |
| All component origins are inside but DRC reports pad clearance | Move the complete footprint, not just the origin; verify auxiliary/mechanical pads and rerun strict DRC |
| EasyEDA autorouter returns `success=false` / `duration=0` | Confirm copper-line/via counts did not change, preserve the pre-route snapshot, record the backend as unavailable, and continue with the reviewed manual/external routing plan |
| Autorouter ignores the requested net subset | Reject the result; never allow an unbounded run to touch USB, RF, power hot loops, display clocks, audio BTL, or ground-plane constraints |
| EasyEDA says the board outline is missing or not closed | Read back layer 11; merge/replace separate lines with one closed layer-11 `POLY`, save/reopen, and require DSN export to contain `boundary(path ...)` |
| SES import changes scale, drops bottom paths, or adds clearance errors | Reject and remove only the newly added copper-layer lines/vias by exact ID; preserve layer 11 and all mechanical projections |
| A via and its top/bottom tracks share an XY but remain disconnected | Delete that net's affected primitives, recreate the via first and the track segments second, then require the target net to disappear from strict DRC connection errors |
| A track reaches a test-pad XY but the pad remains disconnected | Read `pcb_PrimitivePad.layer`; route the final segment on the actual SMD pad layer or use a validated via transition |
| Vercel page cannot reach `192.168.4.1` | Join the Setup AP and grant browser local/private-network permission; do not proxy device credentials through Vercel |
| Android build cannot find its toolchain | Provide Android SDK 35 and Java 17; keep Gradle selection inside `scripts/build-android-cast.sh` |

### 5. Good / Base / Bad Cases

- Good: change the GPIO macro in its owning component, update this table, run
  the firmware build, flash through the correct transport, and validate the
  affected hardware plus cold boot when a strapping pin is involved.
- Good: import an EasyEDA delta, move staged parts inside the board, clear all
  new-part clearance/keepout errors, save/close/reopen, and verify persisted
  component/pad/net counts before calling placement complete.
- Good: represent the board frame as one closed layer-11 polyline, prove the
  autorouter sees it by exporting DSN with a boundary, then constrain low-speed helper routing to an explicit net allowlist and
  Top/Bottom, create vias before tracks, then require zero target-net connection
  errors and zero clearance/keepout errors after save/reopen.
- Base: change only README or Trellis/spec documentation, validate links and
  Markdown, and do not flash unchanged firmware.
- Bad: copy a pin map into README, edit only that copy, enable TF-card GPIO18
  while GPS still owns it, or use `socket://` for an esptool flash.
- Bad: make a module default-enabled in the registry template but rely on a
  profile-only `REQUIRES` list; unprofiled `idf.py build` then cannot resolve
  the component header.
- Bad: leave imported components below the board outline, check only component
  origins, or accept an autorouter result whose duration is zero and whose
  copper-line/via counts did not change.
- Bad: assume touching layer-11 lines or `zoomToBoardOutline() = true` prove the
  autorouter has a board boundary, trust SES layer
  names without readback, create vias after their tracks, or connect a
  bottom-only test pad with a top-layer endpoint.

### 6. Tests Required

- Firmware source, component, `sdkconfig*`, partition, embedded Web asset, or
  build-input changes:

```bash
./scripts/esp-idf-env.sh build
node .gitnexus/run.cjs detect-changes -r esp32BMSGPS
```

- When changing the module registry or its CMake dependency closure, also run
  `./tests/configurator_selftest.sh`; it must prove both that a disabled
  profile excludes its component and that the unprofiled default closure
  contains `esp_bms_gps`.

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
- EasyEDA schematic/PCB changes: save and reopen both documents; assert expected
  component, pad, and net counts; assert all new parts are inside the outline;
  run strict ERC/DRC; assert no new clearance/keepout errors; record copper-line
  and via counts around autorouter experiments; validate the `.eprj2` snapshot
  with SHA-256 and SQLite integrity check.
- EasyEDA routing changes: assert layer 11 contains one closed `POLY`,
  `zoomToBoardOutline()` succeeds, and DSN export contains `boundary(path ...)`;
  assert every copper line/via net is in the
  approved allowlist; assert no target net remains under `Connection Error`;
  assert USB differential/equal-length groups remain split across the series
  resistors; then save, close/reopen, and repeat the assertions.

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

#### Default module closure

Wrong:

```cmake
# ESP_BMS_FEATURE_GPS defaults to 1, but direct builds cannot include its header.
set(ESP_BMS_MAIN_REQUIRES_DEFAULT esp_bms_idf_runtime)
```

Correct:

```cmake
# Every default-enabled registry module is directly reachable.
set(ESP_BMS_MAIN_REQUIRES_DEFAULT esp_bms_gps esp_bms_idf_runtime)
```

#### EasyEDA import placement

Wrong:

```text
importChanges() returned true -> imported components are considered placed
```

Correct:

```text
importChanges() -> move staged parts inside -> strict DRC clearance/keepout = 0
-> save -> close/reopen -> persisted component/pad/net and coordinate readback
```

#### EasyEDA board outline and layer-safe routing

Wrong:

```text
four exact, geometrically closed LINE primitives on layer 11
zoomToBoardOutline() == true
getDsnFile() == undefined

import SES -> delete pcb_PrimitiveLine.getAll() when DRC fails
```

Correct:

```text
one closed POLY on layer 11:
top-left -> top-right -> bottom-right -> bottom-left -> top-left
save/reopen -> getDsnFile() contains boundary(path ...)

import/create allowlisted copper -> read back Top/Bottom by layer
-> delete only new copper-layer line IDs and via IDs on failure
```

## Scenario: Modular Firmware Profiles And Localized Configurator

### 1. Scope / Trigger

- Apply this contract when changing a firmware module catalog, generated
  profile, component `REQUIRES`, the `start.*` configurators, or the local
  build wrapper.
- These changes cross the catalog -> profile -> CMake closure -> generated
  registry -> firmware image boundary. A successful configuration command alone
  is not evidence that a feature was removed from the image.

### 2. Signatures

```text
./start.sh [--lang zh|en] <command> [options]
.\start.ps1 <command> [--lang zh|en] [options]
scripts/build-profile.sh [--lang zh|en] --config firmware.env
```

- No-argument `start.sh`, `start.ps1`, and `start.cmd` executions must first
  offer `1`/`zh` for Simplified Chinese and `2`/`en` for English.
- The interactive wizard then shows a title, numbered catalog options, a build
  summary, and an explicit create/cancel confirmation. After creation it asks
  whether to build locally, prepare an online build request, or keep only the
  generated configuration. It must not ask the user for an internal profile
  name.
- Profiles set `ESP_BMS_FEATURE_{AUDIO,BMS,CONTROLLER,GPS,NETWORK,OTA}` and
  `ESP_BMS_PROFILE_MAIN_REQUIRES`; these are the component-closure contract.
- A saved profile has the build inputs
  `firmware-builds/<profile>/{firmware.env,sdkconfig.defaults,partitions.csv}`.
  Its board-menu entry is a build shortcut, not a catalog record.

### 3. Contracts

- Language defaults to `zh` for non-interactive commands and remains in the
  current process only. `FIRMWARE_LANG` may carry it from the configurator to
  the build wrapper, but it must never be written to `firmware.env`,
  `normalized.env`, `profile.cmake`, or a preference file.
- Keep commands, options, exit codes, `KEY=VALUE` fields, paths, module IDs,
  and generated CMake ASCII. Localize human-facing help, prompts, status, and
  diagnostics only.
- `start.ps1` contains Chinese UI text and must be UTF-8 **with BOM** so the
  built-in Windows PowerShell 5.1 decodes it correctly. `start.cmd` launches
  `%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe` explicitly
  and, for a no-argument launch, pauses after a failure so double-click users
  can read the error.
- The Windows local-build path must dot-source ESP-IDF `export.ps1` (first
  from `$env:IDF_PATH`, then `%USERPROFILE%\\esp\\esp-idf-v6.0.2`) and invoke
  `idf.py` directly in the repository root. It must never invoke the Unix-only
  `scripts/esp-idf-env.sh`. Read `LASTEXITCODE` only after that native
  `idf.py` invocation and only when the variable exists under strict mode.
- PowerShell localized-text mappings that can differ only by letter case (for
  example, `profile` and `Profile`) must be an ordered array of source/target
  pairs, not a hash literal: PowerShell hash keys are case-insensitive and
  reject those entries during parsing before the configurator can start.
- Interactive selection derives `PROFILE` from the selected board ID. A future
  `custom-*` board may fall back to its selected MCU ID, but it must still have
  catalog wiring before it can pass validation. `--profile` remains a
  non-interactive compatibility override and is not shown in the wizard.
- When both standard input and output are attached to a terminal, module
  selection is a keyboard multi-select: `Up`/`Down` moves the focus, `Space`
  toggles the focused module, and `Enter` continues. The menu starts with the
  default modules checked; an empty checked set means no optional modules. With
  redirected input or output, retain the comma-separated number/ID prompt for
  scripts, pipes, and CI compatibility.
- After an interactive configuration is written, option `1` must use the same
  isolated local-build path as `build-local`; option `2` must use the same
  pending cloud-request diagnostic and non-zero status as `build-cloud`; option
  `0` (and an empty or EOF response) leaves the generated configuration in
  place without building. The pending cloud status lets no-argument
  `start.cmd` retain its window so the operator can read the message.
- After board selection, derive the MCU and offer only display/input catalog
  options compatible with that board's buses. When the previous default is
  incompatible, choose the first compatible catalog option before prompting.
- Every non-custom board record includes `DISPLAY_DATA_WIDTH`: use `0` for a
  serial display without a parallel data bus, and `8` or `16` for an I80
  display. Its selected display record must carry the same `DATA_WIDTH`; both
  Bash and PowerShell validate this catalog contract before generating a
  profile.
- `ota` implies `network`. When OTA is disabled, the generated closure must not
  name `esp_bms_ota`, runtime must return `501 Not Implemented` for `/api/ota`,
  and no `esp_ota_*` update symbol may appear in the final ELF.
- ESP-IDF can build `app_update` through the private dependencies of
  `esp_partition`, `spi_flash`, and `espressif__esp_mmap_assets`. Its build
  directory or archive alone is not OTA feature evidence; verify the selected
  BMS component and final ELF symbols instead.
- `write_profile` and `Write-Profile` must copy the selected catalog CSV into
  `firmware-builds/<profile>/partitions.csv`, remove any inherited
  `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME` line from the target-specific
  defaults, then append the absolute path to that copied CSV. ESP-IDF resolves
  a relative filename from the repository root, so copying the file without
  this override silently selects the root legacy partition table.
- The Bash and PowerShell board menus may list only non-hidden, valid saved
  profile directories. Selecting one reloads and revalidates `firmware.env`,
  then enters the existing local-build path without repeating hardware or GPIO
  questions.

### 4. Validation & Error Matrix

| Condition | Required response |
| --- | --- |
| `--lang` is missing or not `zh`/`en` | Exit 2 with a localized diagnostic; do not write a profile |
| Interactive language answer is invalid | Re-prompt before any configuration prompt |
| Interactive board choice changes bus | Replace stale display/input defaults with compatible choices before prompting |
| User cancels the displayed build plan | Exit successfully without creating a configuration directory |
| `start.cmd` cannot invoke PowerShell | Print the resolved executable path; on a no-argument launch, pause instead of closing the error window |
| Localized text map contains case-only duplicate keys in a hash literal | Do not start; replace the literal with ordered source/target pairs and run the script under an available PowerShell runtime |
| Windows local build has no ESP-IDF `export.ps1` or no `idf.py` after import | Exit with a localized toolchain diagnostic; do not read an unset `LASTEXITCODE` or call the Bash wrapper |
| Module wizard runs in a non-terminal or redirected session | Keep the line-based comma-separated number/ID parser; do not attempt raw key reads |
| Interactive user selects online build | Write the profile, print the pending cloud-workflow diagnostic, and exit 3 so the Windows wrapper holds the message open |
| Profile defaults retain a relative partition CSV filename | Regenerate the profile with an absolute `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME`; do not build against the repository root CSV |
| Saved profile is hidden, malformed, or fails validation | Exclude it from the board menu; do not import or build it |
| `ota` is selected | Resolve `network` and set both corresponding features |
| OTA is off | Omit `esp_bms_ota`; prove no BMS OTA handler or `esp_ota_{begin,write,end,set_boot_partition}` symbol is linked |
| Network is off | Omit `esp_bms_network` and its embedded `index.html` symbols |
| RFC2217 server rejects parameter change | Treat the flash as not written; record the exact error and check bridge ownership/configuration before a new attempt |

### 5. Good / Base / Bad Cases

- Good: use `--lang en` for an automation assertion, then compare generated
  `firmware.env`/`normalized.env` bytes independently of displayed language.
- Good: PowerShell dot-sources `export.ps1`, runs `idf.py -B <profile>/idf-build
  ... build`, and forwards its native exit code.
- Good: show all hardware and module choices with their catalog IDs and a
  localized description, derive the output directory from the board, and let
  the user confirm the summary.
- Good: validate network/OTA on-off profiles through component descriptions,
  archives, map files, and final ELF symbols.
- Good: build an S3 profile from its generated defaults and verify IDF reports
  `0x600000` as the smallest app partition; build the legacy profile separately
  and verify its `0x1e0000` OTA-slot limit.
- Base: observe `app_update` in an OTA-off ESP-IDF build, then attribute it to
  its SDK dependency path and still prove no application OTA code is linked.
- Bad: persist a UI language choice in a profile or declare OTA removed merely
  because `esp_bms_ota` is absent while final `esp_ota_*` symbols remain.
- Bad: execute `esp-idf-env.sh` from `start.ps1` and then dereference an unset
  `LASTEXITCODE`; Windows PowerShell does not provide the Bash execution path.
- Bad: prompt an operator for `Profile [legacy]`, retain a display/input choice
  after its board becomes incompatible, or create files after cancellation.
- Bad: copy an S3 CSV into a profile while leaving
  `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"`; IDF will use the
  root CSV instead of the copied S3 layout.

### 6. Tests Required

```bash
bash -n start.sh scripts/build-profile.sh tests/configurator_selftest.sh
./tests/configurator_selftest.sh
cmake --build firmware-builds/<profile>/idf-build
node .gitnexus/run.cjs detect-changes --repo esp32BMSGPS
```

- Test both language positions accepted by `start.sh`, the default Chinese
  output, English override, invalid language, no persistence, and an invalid
  interactive answer followed by a valid selection.
- Test the title, numbered board/module options, automatic board-derived
  `PROFILE`, no visible `Profile` prompt, compatible S3 display/input defaults,
  and cancellation without generated files.
- Under a pseudo-terminal, deselect two default modules with `Space`,
  `Down`, `Space`, then `Enter`; assert that only the remaining sorted IDs are
  written. Keep the existing piped interactive tests to prove the line-input
  fallback remains automation-compatible.
- From a piped interactive run, select next-step option `2`; assert that it
  writes the configuration, emits the cloud-workflow diagnostic, and returns
  exit code `3`.
- Validate the default board/display pair and assert both configurators
  recognize `DISPLAY_DATA_WIDTH` and `DATA_WIDTH`; this prevents a catalog
  extension from failing only on one host platform.
- Assert the first three bytes of `start.ps1` are `EF BB BF` and that
  `start.cmd` uses the explicit built-in Windows PowerShell path. When a
  PowerShell runtime is available, execute `start.ps1` through `pwsh`,
  `powershell`, or `powershell.exe` and compare its normalized output with the
  Bash result. Statically assert that `start.ps1` invokes `idf.py` directly,
  has the guarded `LASTEXITCODE` read, and contains no
  `scripts/esp-idf-env.sh` reference.
- Build network+OTA, network-only, and neither profile. Assert component
  closure and final symbols, not just presence of an ESP-IDF archive.
- Configure S3 with GPS disabled and assert no `GPIO_GPS_*` key is emitted.
  With GPS selected on a board that has no GPS catalog roles, require CLI or
  interactive decimal GPIO values and assert they are persisted. Exercise a
  saved-profile board selection with a test `idf.py` and assert it reaches the
  local build path while hidden and invalid profile directories remain absent.
- For every profile build, assert the generated `sdkconfig.defaults` and final
  `sdkconfig` contain the absolute profile `partitions.csv` path, then run
  `idf.py size` and verify the smallest app partition matches that profile.

### 7. Wrong vs Correct

#### Wrong

```text
OTA off -> app_update archive exists -> report OTA trimming failed
```

#### Correct

```text
OTA off -> no esp_bms_ota component/archive -> no esp_bms_ota or esp_ota_* ELF
symbols -> app_update explained by ESP-IDF esp_partition dependency
```

#### Wrong

```powershell
& "$Root/scripts/esp-idf-env.sh" build
$script:BuildExitCode = $LASTEXITCODE
```

#### Correct

```powershell
. $IdfExport
& idf.py @IdfArgs
if (Test-Path -LiteralPath Variable:global:LASTEXITCODE) {
    $script:BuildExitCode = $global:LASTEXITCODE
}
```

#### Wrong

```text
copy firmware/partitions/esp32s3-n16r8.csv -> profile/partitions.csv
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
```

#### Correct

```text
copy firmware/partitions/esp32s3-n16r8.csv -> profile/partitions.csv
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="/absolute/.../profile/partitions.csv"
idf.py size -> smallest app partition = 0x600000
```
