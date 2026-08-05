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

#### Profile-driven hardware assignments

| Configuration layer | Authority | Consumer |
| --- | --- | --- |
| Verified board wiring, GPIO direction, Flash and PSRAM | `firmware/catalog/board/*.env` | configurators and profile validation |
| Display and touch protocol, controller, timing, orientation | `firmware/catalog/display/*.env`, `firmware/catalog/input/*.env` | generated bridge configuration |
| Optional module GPIO roles | `firmware/catalog/module/*.env` | configurators and generated module settings |
| Dashboard UI inclusion | `firmware/catalog/dashboard/*.env` | `DASHBOARDS` in saved profile and `ESP_BMS_FEATURE_DASHBOARD_*` CMake flags |
| Saved user selection and CLI GPIO overrides | `firmware-builds/<profile>/firmware.env` | reproducible local and cloud builds |
| Generated CMake and C configuration | `firmware-builds/<profile>/generated/{profile.cmake,esp_bms_profile_hardware.h}` | `main`, runtime modules, audio, and LVGL bridge |

Dashboard UI selection is offered only when the selected module closure includes
`gps` or `controller`. The `controller` dashboard requires `controller`; the
S1000RR and Fireblade dashboards require either `gps` or `controller`. A
profile with neither module persists an empty `DASHBOARDS` value.

The LVGL bridge accepts only the generated `ESP_BMS_PROFILE_LVGL_CONFIG`; it
does not define a board, controller, GPIO, or display default. GPS, battery
ADC, and audio likewise read generated profile roles. A disabled module must
not require or emit its GPIO roles. If a selected module needs a role absent
from the selected board, interactive configuration collects a decimal GPIO;
non-interactive callers must provide `--gpio ROLE=PIN`.

Catalog records list bootstrap-sensitive and input-only pins for each MCU.
Changing a verified role requires the catalog/profile input, a regenerated
profile, a firmware build, and cold-boot validation where the board flags the
pin as dangerous. Do not add a source-code fallback GPIO.

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
.\scripts\flash.ps1 -Port COM6 -Monitor

# Windows side of the fixed RFC2217 bridge
.\scripts\serial_tcp_bridge.ps1 -PortName COM6
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
- `start.sh install-idf` installs Linux/macOS prerequisites through the detected
  package manager, clones ESP-IDF `v6.0.2` into the project root as
  `esp-idf-v6.0.2/`, installs its host tools into `esp-idf-tools/`, and records
  the install path in `$XDG_CONFIG_HOME/esp32-bms-gps/idf-path`. `start.ps1
  install-idf` uses `winget`, runs `install.ps1`, and persists `IDF_PATH` in
  the Windows user environment. Both accept `--dir` for non-interactive setup.
- ESP-IDF cloning uses at most three attempts. Each attempt clones into a
  unique hidden sibling directory and moves it to the requested installation
  directory only after `git clone` succeeds. A failed attempt must retain its
  partial directory and report its path; do not delete it automatically or
  change global Git transport settings.
- `scripts/esp-idf-env.sh` loads `$IDF_PATH/export.sh` when explicitly set,
  then the configured path, then the project-root `esp-idf-v6.0.2/export.sh`,
  and finally `$HOME/esp/esp-idf-v6.0.2/export.sh`; it pairs the project IDF
  with the project-root `esp-idf-tools/` when `IDF_TOOLS_PATH` is unset and
  verifies the resolved version before forwarding arguments to `idf.py`.
- The helper supplies localhost proxy defaults when the corresponding proxy
  variables are unset. Override `http_proxy`, `https_proxy`, and `all_proxy`
  explicitly on hosts that use a different proxy path.
- Declare direct managed-component requirements in `main/idf_component.yml`.
  Treat `dependencies.lock` as the resolved version authority; do not maintain
  a second dependency-version list in README.
- Profile builds generate `firmware-builds/<profile>/generated/idf_component.yml`
  from the selected display and touch catalog records. The generated manifest
  contains the common LVGL dependencies plus only the selected external driver
  packages; `none` contributes no touch package.
- Profile builds must copy the repository `main/` sources into the temporary
  ESP-IDF project before replacing its `CMakeLists.txt` and manifest. The
  temporary top-level CMake must set `COMPONENTS main` and use
  `EXTRA_COMPONENT_DIRS` for the repository components. Without the source copy,
  CMake cannot find `idf_main.c`; without `COMPONENTS main`, every local
  component is scanned and disabled modules can still fail the build.
- Component Manager resolves the manifest and writes
  `<profile-idf-project>/dependencies.lock`; a successful build copies that
  file to `firmware-builds/<profile>/generated/dependencies.lock`. Never copy the
  profile manifest or lock into the repository root.
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
- The ESP32-S3 defaults set `CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192`. The IDF
  default 3584-byte main stack exhausts during NVS initialization on the
  GT1151 S3 profile and can corrupt a later libc mutex; do not lower it without
  repeating cold-boot/NVS validation and checking main-task high-water marks.
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
- Interactive configurator builds default to local serial flashing. Remote
  RFC2217 requires an explicit target selection and URL.
- The project remote bridge is
  `rfc2217://192.168.2.10:4000?ign_set_control`, backed by Windows `COM6` and
  `scripts/serial_tcp_bridge.ps1`.
- Use RFC2217, not `socket://`, because esptool requires DTR/RTS line control.
- Use explicit `-b 115200` on the fixed bridge and allow only one bridge client
  at a time.
- A successful profile build publishes a portable flash bundle in
  `output/<profile>/`, before its isolated ESP-IDF build directory is removed:
  - `<profile>.bin` is the application/OTA image and must be written to the
    app address in `flash-manifest.json` (`0x10000` for the legacy ESP32
    partition table), never to `0x1000`.
  - `bootloader.bin`, `partition-table.bin`, and `ota_data_initial.bin` are
    the matching ESP-IDF support images. Their offsets come only from
    `flash-manifest.json`.
  - `<profile>-flash.bin` contains the complete image set with erased-byte
    padding and is the only one-file online-flash input; write it at `0x0`.
- `scripts/publish-flash-artifacts.py` derives this bundle from the completed
  build's `flasher_args.json`. Both `start.sh` and `start.ps1` must call this
  one publisher rather than separately copying an app image.
- Codex-generated test or validation firmware is disposable: set
  `FIRMWARE_BUILD_ROOT` and `FIRMWARE_OUTPUT_ROOT` to temporary directories.
  Do not leave its generated profile under the default `firmware-builds/` or
  its bundle under `output/`, because `start` treats retained profiles in
  `firmware-builds/` as user-selectable saved configurations.
- Flash acceptance does not require a separate full-image hash/readback pass
  (for example, `esptool verify-flash`). A successful write command followed by
  reset and the relevant runtime behavior check is sufficient. Any checksum
  line emitted internally by the flashing tool is informational, not an
  additional acceptance gate.
- Documentation-only, preview-only, Trellis/spec, and agent-file changes do not
  require a firmware build or hardware flash unless the user explicitly asks.
  Firmware-impacting changes require the normal build and the project hardware
  validation flow before completion.

### 4. Validation & Error Matrix

| Condition | Required response |
| --- | --- |
| `idf.py` is missing | Set `IDF_PATH` or install ESP-IDF 6.0.2; do not substitute another build system |
| ESP-IDF clone ends with RPC/early EOF or a Schannel close-notify error | Retry through the configured proxy up to three times. Keep every partial sibling clone, report those paths after the last failure, and leave the requested installation directory absent for a later retry. |
| Managed-component download fails | Check the explicit proxy environment and `dependencies.lock` before changing dependency versions |
| Profile CMake cannot find `main/idf_main.c` | Recreate the temporary project with a copy of repository `main/` sources before writing the wrapper CMake |
| Profile build compiles a disabled local module | Confirm the temporary top-level CMake contains `set(COMPONENTS main)` and that the generated main closure excludes the module |
| Profile lock is missing after a successful build | Inspect the temporary profile project first; do not fall back to or overwrite the repository root `dependencies.lock` |
| Direct build reports a missing module header from `esp_bms_module_registry.c` | Compare default `ESP_BMS_FEATURE_*` values, registry template includes, and `ESP_BMS_MAIN_REQUIRES_DEFAULT`; add the omitted owning component and a configurator self-test assertion |
| RFC2217 TCP port is closed | Check the Windows bridge process, firewall scope, host IP, and port 4000 |
| RFC2217 connects but flash sync fails | Close other clients, confirm the server is RFC2217 rather than raw TCP, and keep `-b 115200` |
| Firmware flashes but boot loops after a partition change | Erase Flash once, then flash the complete ESP-IDF image set |
| Online tool received only `<profile>.bin` at `0x1000` | Erase Flash once, then write `<profile>-flash.bin` at `0x0`, or follow `flash-manifest.json` for every separate file |
| Profile build has no `flasher_args.json` or one referenced image is missing | Fail publication and retain the build error; never silently publish an app-only image as a complete flash package |
| TFT or touch does not initialize | Compare the generated profile header with the selected catalog record and validate board-designated dangerous pins under cold boot |
| GPS UART receives no bytes | Confirm the generated GPS RX/TX roles, crossed wiring, selected UART baud, power, and signal level |
| PPS never triggers | Check the generated PPS role, GPS fix/PPS output, voltage, and required external pull configuration |
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

- Good: update verified catalog wiring or an explicit profile override,
  regenerate the profile, run the firmware build, flash through the correct
  transport, and validate affected hardware plus cold boot when a dangerous
  pin is involved.
- Good: select `<profile>-flash.bin` in an online flasher and set its address
  to `0x0`; alternatively use each `flash-manifest.json` file/offset pair.
- Bad: write `<profile>.bin` at `0x1000`. It is an application image for
  `0x10000` and overwrites the bootloader when placed at the bootloader offset.
- Good: import an EasyEDA delta, move staged parts inside the board, clear all
  new-part clearance/keepout errors, save/close/reopen, and verify persisted
  component/pad/net counts before calling placement complete.
- Good: represent the board frame as one closed layer-11 polyline, prove the
  autorouter sees it by exporting DSN with a boundary, then constrain low-speed helper routing to an explicit net allowlist and
  Top/Bottom, create vias before tracks, then require zero target-net connection
  errors and zero clearance/keepout errors after save/reopen.
- Good: when a GitHub pack transfer is interrupted, keep the existing proxy,
  retry in a unique sibling clone directory, and move only the complete clone
  into the requested installation path.
- Good: generate a profile manifest, build from an isolated project containing
  copied `main/` sources and `COMPONENTS main`, then retain the resolved lock
  under that profile's `generated/` directory.
- Base: change only README or Trellis/spec documentation, validate links and
  Markdown, and do not flash unchanged firmware.
- Bad: copy a pin map into README, add a source-code GPIO fallback, accept a
  conflicting profile role, or use `socket://` for an esptool flash.
- Bad: make a module default-enabled in the registry template but rely on a
  profile-only `REQUIRES` list; unprofiled `idf.py build` then cannot resolve
  the component header.
- Bad: create a profile project with only a wrapper `main/CMakeLists.txt`, or
  omit `set(COMPONENTS main)` from its top-level CMake. The first fails on a
  missing `idf_main.c`; the second builds unselected local modules and can fail
  on GPIO defaults that the profile intentionally omitted.
- Bad: use the repository root `idf_component.yml` or `dependencies.lock` as a
  write target for a profile build; concurrent profiles will overwrite each
  other's dependency state.
- Bad: leave imported components below the board outline, check only component
  origins, or accept an autorouter result whose duration is zero and whose
  copper-line/via counts did not change.
- Bad: assume touching layer-11 lines or `zoomToBoardOutline() = true` prove the
  autorouter has a board boundary, trust SES layer
  names without readback, create vias after their tracks, or connect a
  bottom-only test pad with a top-layer endpoint.
- Bad: clone directly into the final ESP-IDF directory and then auto-delete a
  failed clone before the user can inspect it.

### 6. Tests Required

- Firmware source, component, `sdkconfig*`, partition, embedded Web asset, or
  build-input changes:

```bash
./scripts/esp-idf-env.sh build
node .gitnexus/run.cjs detect-changes -r esp32BMSGPS
```

- Profile dependency and temporary-project changes additionally require one
  build for a cached existing driver profile and one build for a newly selected
  driver package. Assert that the generated manifest and lock are profile-local,
  that the component list contains only the selected external display/touch
  drivers, and that the firmware binary is produced.

- When changing the module registry or its CMake dependency closure, also run
  `./tests/configurator_selftest.sh`; it must prove both that a disabled
  profile excludes its component and that the unprofiled default closure
  contains `esp_bms_gps`.

- When changing either `install-idf` implementation, run
  `./tests/configurator_selftest.sh`; its fake Git scenario must fail once,
  then succeed, and assert that the final installation directory contains the
  completed clone after exactly two clone attempts.

- Hardware-impacting changes: flash through the developer's local serial port
  by default. Use the fixed RFC2217 bridge only when remote hardware validation
  is explicitly required, then inspect boot logs and the affected peripheral.
  Pin changes involving GPIO2/4/5/12/15 require a power-cycle test.
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

## Scenario: Cancel an in-progress BMS BLE connection

### 1. Scope / Trigger

- Trigger: the LVGL BMS connection refresh control is pressed while the BMS
  transport is connecting or discovering services.

### 2. Signatures

- `ESP_BMS_LVGL_ACTION_CANCEL_BMS_CONNECTION`
- `bool (*stop)(esp_bms_idf_runtime_t *runtime)` in
  `esp_bms_idf_runtime_bms_ble_driver_t`

### 3. Contracts

- The UI dispatches the cancel action only for BMS connection phases
  (`BMS CONN`, `BMS DISC`, `BMS SVC`, `BMS CHR`, `BMS CCCD`, or `BMS SUB`).
- The driver cancels discovery, pending GAP connection, or an established GAP
  connection as applicable; it clears `BMS_BIND_ACTIVE` and
  `BMS_SCAN_REQUESTED`, then publishes `BMS OFF`.
- `stop()` can run after BMS type selection even when no BMS was bound and the
  NimBLE host was never started. Call `ble_gap_*` only when both
  `BMS_BLE_READY` and `BMS_BLE_SYNCED` are set; local runtime, telemetry, and
  snapshot cleanup always runs.
- Cancelling does not erase the persisted BMS MAC. A later explicit refresh
  can reconnect.

### 4. Validation & Error Matrix

| Condition | Required response |
| --- | --- |
| Pending GAP connection | Call `ble_gap_conn_cancel()` |
| Connected discovery stage | Call `ble_gap_terminate()` |
| Discovery scan active | Call `ble_gap_disc_cancel()` |
| No active BMS operation | Return unchanged; do not remove binding |
| NimBLE host not ready or synced | Skip every `ble_gap_*` call, then reset local BMS state safely |

### 5. Good / Base / Bad Cases

- Good: refresh during `BMS DISC` immediately changes the snapshot to `BMS OFF`
  and does not schedule reconnect.
- Good: selecting a BMS type with no bound MAC clears local BMS state without
  initializing or querying NimBLE.
- Base: refresh while scanning keeps the original scan behavior.
- Bad: only change the visible label while the NimBLE GAP operation continues,
  or query `ble_gap_disc_active()` before the host is ready.

### 6. Tests Required

- The simulator action dispatcher must map the cancel action to offline BMS
  state.
- Run the LVGL headless capability matrix and an ESP-IDF legacy profile build.
- On hardware with no bound MAC, select two different BMS types and assert no
  Guru Meditation or reset; then verify an active scan or connection still
  cancels through the appropriate GAP operation.

### 7. Wrong vs Correct

#### Wrong

```c
label_set_text_if_changed(status, "已取消");
```

#### Correct

```c
(void)ble_gap_conn_cancel();
RUNTIME_SET_FLAG(runtime, BMS_BIND_ACTIVE, false);
bms_set_info(runtime, "BMS OFF");
```

#### Wrong

```c
if (ble_gap_disc_active()) {
    (void)ble_gap_disc_cancel();
}
```

#### Correct

```c
if (RUNTIME_FLAG(runtime, BMS_BLE_READY) && RUNTIME_FLAG(runtime, BMS_BLE_SYNCED)) {
    if (ble_gap_disc_active()) {
        (void)ble_gap_disc_cancel();
    }
}
```

#### Wrong

```markdown
<!-- A second pin table in README becomes stale while code changes. -->
GPS RX: GPIO3 at 9600 baud
```

```bash
# Raw TCP cannot provide the line control expected by esptool.
idf.py -p socket://192.168.2.10:4000 flash
```

```bash
# A network interruption leaves a partial final directory that blocks retry.
git clone --branch v6.0.2 --depth 1 https://github.com/espressif/esp-idf.git "$install_dir"
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

```bash
# Each attempt keeps the final installation directory untouched until success.
clone_dir="$(dirname "$install_dir")/.esp32-bms-idf-clone-$RANDOM"
git clone --branch v6.0.2 --depth 1 --recursive --shallow-submodules \
  https://github.com/espressif/esp-idf.git "$clone_dir" && mv "$clone_dir" "$install_dir"
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
scripts/dispatch-cloud-build.py --config firmware.env
```

- No-argument `start.sh`, `start.ps1`, and `start.cmd` executions must first
  offer `1`/`zh` for Simplified Chinese and `2`/`en` for English.
- The interactive wizard then shows a title, numbered catalog options, a build
  summary, and an explicit create/cancel confirmation. After creation it asks
  whether to build locally, trigger an online build, or keep only the generated
  configuration. It must not ask the user for an internal profile name.
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
- The Windows local-build path must discover and dot-source ESP-IDF `export.ps1`
  from the current or persisted `IDF_PATH`, project-adjacent directories, and
  common v6.0.2 installation roots before invoking `idf.py` directly in the
  repository root. On an interactive local build, if detection fails, it asks
  to install ESP-IDF and collects the install directory. It must never invoke
  the Unix-only `scripts/esp-idf-env.sh`. Read `LASTEXITCODE` only after that
  native `idf.py` invocation and only when the variable exists under strict mode.
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
  `build-cloud` dispatch path; option `0` (and an empty or EOF response) leaves
  the generated configuration in place without building.
- `build-cloud` and interactive option `2` pass the generated, already
  validated `firmware.env` to `scripts/dispatch-cloud-build.py`. The dispatcher
  accepts only a GitHub `origin`, a checked-out branch whose `origin` SHA equals
  local `HEAD`, and a token from `ESP_BMS_GITHUB_TOKEN` or a non-echoed terminal
  prompt. It sends `workflow_dispatch` to `cloud-build.yml` with `ref` and the
  Base64 `firmware_env_base64` input; it never commits, pushes, persists, or
  prints the token.
- `.github/workflows/cloud-build.yml` uses ESP-IDF `v6.0.2`, decodes and
  revalidates the configuration, runs `build-local`, and uploads the generated
  profile and firmware output. A dispatch HTTP 204 confirms only queuing, not
  a completed build or a downloadable artifact.
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
| Interactive user selects online build | Write the profile and call the same cloud dispatcher as `build-cloud` |
| Current branch is detached, absent on `origin`, or its remote SHA differs from local `HEAD` | Do not send HTTP; exit with the localized instruction to push the current branch first |
| `ESP_BMS_GITHUB_TOKEN` is absent outside a terminal, or the prompt is empty | Do not send HTTP; exit with a localized token diagnostic |
| Actions API returns a non-204 status or network error | Do not print the token or request payload; exit with a localized HTTP-status or network diagnostic |
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
- Good: push the commit containing `cloud-build.yml`, set
  `ESP_BMS_GITHUB_TOKEN` only in the invoking session, then let `build-cloud`
  queue the already-validated profile for GitHub Actions.
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
- Bad: submit a local-only HEAD, put a GitHub token in `firmware.env`, or report
  an HTTP 204 as completed firmware output.

### 6. Tests Required

```bash
bash -n start.sh scripts/build-profile.sh tests/configurator_selftest.sh
./tests/configurator_selftest.sh
python3 -m unittest tests/test_cloud_dispatch.py
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
- Assert the Bash and PowerShell cloud entry points invoke the shared
  dispatcher, the workflow declares `workflow_dispatch`, and the Python tests
  cover GitHub-origin parsing, pushed-HEAD matching, Base64 request payloads,
  and HTTP 204 acceptance without exposing the token.
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

#### Wrong

```text
build-cloud -> write firmware.env -> exit 3 with a future-workflow message
```

#### Correct

```text
build-cloud -> validate generated firmware.env -> verify pushed GitHub branch
-> workflow_dispatch(ref, firmware_env_base64) -> HTTP 204 means queued
```

## Scenario: Firmware version and shared BLE scan ownership

### 1. Scope / Trigger

- Trigger: changing a saved firmware profile, generated hardware header, runtime
  status snapshot, BMS/controller BLE scan path, or either Bluetooth settings list.

### 2. Signatures

```text
./start.sh configure --firmware-version VALUE ...
.\\start.ps1 configure --firmware-version VALUE ...
FIRMWARE_VERSION=VALUE
ESP_BMS_PROFILE_FIRMWARE_VERSION
esp_bms_idf_runtime_start_{bms,controller}_scan(runtime)
esp_bms_idf_runtime_resume_bms_scan(runtime)
BLE_GAP_EVENT_DISC_COMPLETE
```

### 3. Contracts

- `FIRMWARE_VERSION` is required, non-empty ASCII `KEY=VALUE` data in every
  saved `firmware.env`; configurators default it to `dev` and accept
  `--firmware-version VALUE`. The generator emits the escaped C string macro
  `ESP_BMS_PROFILE_FIRMWARE_VERSION`, and `idf_main` copies it to the runtime
  snapshot. `/api/status.version` and the About-device row use that snapshot,
  never a source-code version literal.
- NimBLE has one active GAP discovery callback. Starting a BMS scan while a
  controller scan is active, or the reverse, must cancel the active discovery,
  clear the superseded source's pending flag, and preserve only the latest
  request. The active callback must start that pending source from its
  `BLE_GAP_EVENT_DISC_COMPLETE` handler; do not depend on BMS/controller tick
  ordering. The initiating source owns only its own active flag, revision, and
  candidate array; do not reuse BMS candidate data for the controller list.
- The legacy ESP32 catalog entry `ili9341-2p8-spi` is an ESP32-compatible
  240x320 ILI9341 SPI display. It remains selectable with `xpt2046-spi`; the
  generated profile must report size `2.8`, ILI9341, and XPT2046.
- A profile may compile out every I2C touch or I80 panel driver. Declarations
  used only by those driver constructors must use the same preprocessor guard,
  so the bridge produces no `-Wunused-variable` warning in a SPI-only profile.

### 4. Validation & Error Matrix

| Condition | Required response |
| --- | --- |
| Missing or invalid `FIRMWARE_VERSION` | Configurator/generator exits before writing a buildable profile |
| Legacy profile has no version | Regenerate or explicitly set it; do not silently compile a blank runtime value |
| A second scan begins during GAP discovery | Cancel discovery, clear the older pending flag, then start the latest source from `BLE_GAP_EVENT_DISC_COMPLETE` and publish no false active state |
| Controller scan returns advertisements | Update `controller_scan_candidates` and its revision so the controller list refreshes |
| SPI-only profile compiles no I2C/I80 driver | Do not declare `touch_config` or `panel_config` outside their matching preprocessor guards |

### 5. Good / Base / Bad Cases

- Good: `FIRMWARE_VERSION=v1.2.3` produces the same value in the generated
  header, About page, and `/api/status` JSON.
- Base: a single BMS or controller scan runs normally and populates only that
  source's list.
- Bad: hard-code `0.1.0`, let both pending scan flags survive a handoff, rely
  on tick ordering after cancellation, leave a controller scan marked active
  after its callback was displaced, or display BMS candidates in the
  controller UI.

### 6. Tests Required

- `tests/configurator_selftest.sh` must assert `FIRMWARE_VERSION` persistence
  and `ESP_BMS_PROFILE_FIRMWARE_VERSION` generation, plus the legacy
  `ili9341-2p8-spi` + `xpt2046-spi` generated header values.
- Build a SPI-only legacy profile and an I2C/I80 profile; the former must have
  no `touch_config`/`panel_config` unused-variable warning and the latter must
  still invoke its selected constructors.
- Run the legacy profile build and check the device boot/status output.
- On hardware, open both BLE list pages in succession and verify the log shows
  a completion-event handoff and the second page receives its own candidates.

### 7. Wrong vs Correct

#### Wrong

```c
snprintf(json, sizeof(json), "{\\\"version\\\":\\\"0.1.0\\\"}");
```

#### Correct

```c
snprintf(json, sizeof(json), "{\\\"version\\\":\\\"%s\\\"}",
         runtime->snapshot.firmware_version);
```

#### Wrong

```c
/* The next tick may start the old source first. */
ble_gap_disc_cancel();
```

#### Correct

```c
/* Keep only the latest request, then hand it off when discovery completes. */
RUNTIME_SET_FLAG(runtime, BMS_SCAN_REQUESTED, false);
RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_REQUESTED, true);
ble_gap_disc_cancel();
```

## Scenario: Panel-Only Mirror Compensation

### 1. Scope / Trigger

- Apply when a catalog display needs a physical panel mirror correction while
  the paired touch controller already reports correct coordinates.

### 2. Signatures

```text
firmware/catalog/display/*.env: PANEL_MIRROR_X=0|1
esp_bms_lvgl_bridge_config_t.panel_mirror_x
```

### 3. Contracts

- `PANEL_MIRROR_X` is optional and defaults to `0`; the profile generator
  validates a supplied value as `0` or `1` and emits `panel_mirror_x`.
- The bridge combines this flag only with `esp_lcd_panel_mirror`. It must not
  enter `touch_rotation_flags` or alter GT1151 calibration coordinates.
- For `st7796u-i80` on the Huiqin ESP32-S3 N16R8 reference board, retain
  `RGB_ORDER=BGR`, `I80_SWAP_COLOR_BYTES=0`, and `INVERT_COLOR=1`. The vendor
  landscape flags are `swap_xy=1`, `mirror_x=0`, `mirror_y=0`; after the
  generic landscape transform, that requires `PANEL_MIRROR_X=1`.
- The validated RGB565 configuration for this board is:

  ```text
  RGB_ORDER=BGR
  I80_SWAP_COLOR_BYTES=0
  INVERT_COLOR=1
  custom_draw_bitmap: lv_draw_sw_rgb565_swap() -> esp_lcd_panel_draw_bitmap()
  ```

  Red, green, and blue render correctly with this exact combination. Do not
  replace it with `RGB_ORDER=RGB` or `I80_SWAP_COLOR_BYTES=1`.
- On that board, RGB565 byte swapping belongs exclusively to
  `custom_draw_bitmap`: call `lv_draw_sw_rgb565_swap()` immediately before
  `esp_lcd_panel_draw_bitmap()`. Do not rely on the adapter flush path or
  enable `I80_SWAP_COLOR_BYTES`, which would duplicate the conversion.
- ESP LCD Touch applies `MIRROR_X` and `MIRROR_Y` before `SWAP_XY`. For this
  board's final landscape orientation, `SWAP_XY=1`, `MIRROR_X=1`, and
  `MIRROR_Y=0` produce `screen_y = 320 - raw_x`.
- Use `RGB_ORDER=RGB` or `BGR` for channel order. Do not use color inversion
  to compensate for a red/blue channel swap.

### 4. Validation & Error Matrix

| Condition | Required response |
| --- | --- |
| `PANEL_MIRROR_X` is not `0` or `1` | Profile generation fails before compilation |
| Landscape panel is vertically mirrored but touch is correct | Set `PANEL_MIRROR_X=1`; do not change `ROTATION` |
| Red and blue are exchanged | Correct `RGB_ORDER`; retain `INVERT_COLOR` for luminance inversion only |
| Vendor reference uses a swapped-axis orientation | Compare its final `swap_xy` and both mirror flags; do not infer the native mirror axis from the visual symptom |
| Landscape touch is vertically reversed | Mirror the raw X axis before `SWAP_XY`; do not toggle `MIRROR_Y` |

### 5. Good / Base / Bad Cases

- Good: the ST7796U S3 record enables `PANEL_MIRROR_X=1`; touch remains unchanged.
- Base: displays without the field generate `panel_mirror_x = false`.
- Bad: change `LANDSCAPE` to `INVERTED_LANDSCAPE` to fix a panel-only mirror.

### 6. Tests Required

- `tests/configurator_selftest.sh` must assert the S3 generated header contains
  `.panel_mirror_x = true` and the selected RGB order.
- Build the S3 profile, flash its complete image through RFC2217, and verify
  panel orientation, RGB channels, and touch independently on hardware.

### 7. Wrong vs Correct

#### Wrong

```c
rotation_flags(ESP_BMS_DISPLAY_ROTATION_INVERTED_LANDSCAPE, &swap, &mirror_x, &mirror_y);
apply_touch_rotation(ESP_BMS_DISPLAY_ROTATION_INVERTED_LANDSCAPE);
```

#### Correct

```c
panel_rotation_flags(rotation, &swap, &mirror_x, &mirror_y);
esp_lcd_panel_mirror(s_panel, mirror_x, mirror_y);
/* Keep touch_rotation_flags(rotation, ...) unchanged. */
```

## Scenario: Android BLE Phone Media Bridge

### 1. Scope / Trigger

- Trigger: adding Android phone media controls to the device carousel without
  Classic Bluetooth AVRCP.

### 2. Signatures

```text
module: phone-media (REQUIRES_CAPABILITIES=BLE)
profile: ESP_BMS_FEATURE_PHONE_MEDIA=0|1
service: 5f9b7f60-9f16-4edf-a2e8-472a8aa1b201
command notify: ...b202, 1=previous, 2=next, 3=volume down, 4=volume up
state write: ...b203, [version=1, flags, UTF-8 title up to 96 bytes]
```

### 3. Contracts

- `phone-media` is opt-in. A disabled profile must omit the music page, GATT
  service, advertising UUID, and media actions. The configurator rejects it
  on an MCU without `BLE`.
- Register the private service on the existing NimBLE host before it starts;
  never create a second host or alter the BMS/controller scanner ownership.
  Accept state writes only on an encrypted connection and hand them to the
  runtime through a fixed queue.
- Both `PHONE_MEDIA_GATT_SERVICES` characteristics must set
  `.access_cb = runtime_phone_media_gatt_access_cb`, including the command
  notify characteristic. NimBLE rejects a characteristic with no callback at
  `ble_gatts_count_cfg()` with `BLE_HS_EINVAL` (`rc=3`), before the BMS scan
  can initialize the host.
- `flags bit0=ready`, `bit1=active MediaSession`, and `bit2=playing`. Validate
  version, payload length, and UTF-8 before updating the snapshot. Do not log
  raw titles or state payloads.
- Phone volume commands use Android `AudioManager.STREAM_MUSIC`. They must not
  update `runtime.volume_percent`, audio feedback volume, or NVS.
- The Android app needs a connected-device foreground service and a notification
  listener. On Android 10/11, declare and request `ACCESS_FINE_LOCATION` before
  BLE scanning; on Android 12+, request `BLUETOOTH_SCAN` and
  `BLUETOOTH_CONNECT` instead. Android 12+ BLE permission and notification-access
  failures must remain visible to the user instead of claiming the bridge is ready.
- `ESP_BMS_LVGL_ACTION_ENABLE_BLUETOOTH_ADVERTISING` is available when any of
  BMS, controller, or `phone-media` is enabled. A phone-media-only profile
  starts the existing NimBLE host from the device Settings > System >
  discoverable control; it must not depend on a BMS binding.
- A flash package is target-specific. Use an `esp32` package for the legacy
  ESP32 and an `esp32s3` package for S3 hardware; esptool must reject a
  mismatched chip rather than writing it.

### 4. Validation & Error Matrix

| Condition | Required response |
| --- | --- |
| Feature disabled | No media UUID/page/actions are present |
| No BLE capability | Configurator rejects `phone-media` before profile generation |
| Phone-media-only profile | Discoverable control starts local advertising without BMS/controller |
| Unencrypted or malformed state write | Reject it without changing the dashboard snapshot |
| Android 10/11 location denied | Do not scan; request location permission before starting the service |
| Command CCCD not subscribed or link disconnected | Do not notify or imply the control is available |
| Android BLE/notification access denied | Keep the service safe and show an actionable status |
| Flash target differs from the connected chip | Stop at esptool chip validation; rebuild the matching profile |

### 5. Good / Base / Bad Cases

- Good: an encrypted, subscribed Android connection receives one-byte commands
  and writes a bounded UTF-8 title back to the music page.
- Base: no bound phone leaves the BLE host idle and the music page reports an
  unavailable control state without affecting BMS/controller operation.
- Bad: use AVRCP, persist a title, write the device volume for a phone-volume
  command, or flash an S3 image to an ESP32.

### 6. Tests Required

- Run `./scripts/run-host-selftests.sh` and `./tests/configurator_selftest.sh`;
  the latter must reject `phone-media` without BLE.
- Build both a disabled profile and a feature-enabled profile, asserting
  `ESP_BMS_FEATURE_PHONE_MEDIA=0` and `=1` respectively.
- Build an isolated profile with only `phone-media`, and assert BMS/controller
  are absent while the `bt` component and media GATT service remain linked.
- Run `RUN_TESTS=1 ./scripts/build-android-cast.sh` for protocol UTF-8
  truncation, Android 10/11 location permission selection, and Android compilation.
- Build the LVGL simulator with `ESP_BMS_FEATURE_PHONE_MEDIA=1` and capture
  the `music` page in `preview/`.
- Flash the matching target package through RFC2217 at 115200 and check boot
  logs for a normal `app_main` path with no panic or watchdog.

### 7. Wrong vs Correct

#### Wrong

```c
runtime->volume_percent = requested_phone_volume;
```

#### Correct

```c
return runtime_phone_media_send_command(runtime,
                                        ESP_BMS_PHONE_MEDIA_COMMAND_VOLUME_UP);
```

## Scenario: App-Free BLE HID Media Control

### 1. Scope / Trigger

- Trigger: adding phone media controls without an Android companion app on an
  ESP32 or ESP32-S3 profile that already uses NimBLE for BMS/controller BLE.

### 2. Signatures

```text
module: ble-media-hid (REQUIRES_CAPABILITIES=BLE, CONFLICTS=phone-media)
profile: ESP_BMS_FEATURE_BLE_MEDIA_HID=0|1
service: HID over GATT 0x1812, appearance 0x03C0
input report: id 1, 2-byte little-endian Consumer Usage ID
usages: 0x00B6/0x00CD/0x00B5/0x00EA/0x00E9
security: CONFIG_BT_NIMBLE_SM_LVL=2, bond=1, sc=1, mitm=1, io_cap=DISP_ONLY
pairing PIN: 123456
secured attributes: HID Information, Report Map, Protocol Mode, Input Report, CCCD writes
```

### 3. Contracts

- `ble-media-hid` and `phone-media` are mutually exclusive. A disabled HID
  profile omits the HIDS service, HID advertising UUID/appearance, worker, and
  music page.
- Register HIDS directly on the existing NimBLE Host before it starts. Do not
  use a second Host or `esp_hid`, because the runtime owns peripheral phone
  connections while BMS/controller retain central connection ownership.
- Register Generic Attribute service with `ble_svc_gatt_init()` before the
  hand-written DIS/BAS/HIDS services so Android hosts do not see a partial GATT
  database after cache changes.
- The validated HID pairing path is device-initiated SMP after the ACL
  connection settles: keep the default PHY, call `ble_gap_security_initiate()`
  after the local 1000 ms delay, require `CONFIG_BT_NIMBLE_SM_LVL=2`, and use
  `BLE_SM_IO_CAP_DISP_ONLY` with MITM enabled. `PASSKEY_ACTION` for display or
  input injects and displays `123456`; numeric comparison may be accepted by
  firmware.
- While the passkey is active, the device Settings > Bluetooth detail page must
  show `PIN 123456` on the existing discoverability row. Do not add a new
  top-level prompt or a separate Bluetooth page.
- Do not switch this profile to no-PIN Just Works (`NO_IO`, `mitm=0`) unless it
  is a deliberate UX experiment with a fresh Android/Windows hardware pass.
  Bluetooth headphones can control media without a PIN because they usually use
  Classic Bluetooth AVRCP after audio-profile pairing; that does not satisfy the
  ESP32-S3-compatible BLE HID requirement.
- Keep the local phone HID connection on the default PHY during pairing. Do not
  request Coded PHY for HIDS; BMS/controller long-range BLE may still request
  Coded PHY through their own central connection paths.
- After an encrypted link subscribes to the input report, each touch action
  sends the 2-byte little-endian Consumer Usage ID, then an all-zero 2-byte
  release report after 30 ms. Refuse actions while unpaired, unsubscribed,
  disconnected, or suspended.
- HID volume usages control the phone only. They must never change
  `runtime.volume_percent`, audio-feedback volume, or NVS.

### 4. Validation & Error Matrix

| Condition | Required response |
| --- | --- |
| No BLE capability or both media modules selected | Configurator rejects the profile |
| HID disabled | No HIDS UUID, HID appearance, page, or worker in the image |
| Android connects but has not paired | Firmware starts SMP after 1000 ms and surfaces PIN `123456` |
| `PASSKEY_ACTION` is display/input | Inject passkey `123456`; keep the connection open until SMP succeeds or the peer disconnects |
| User opens Bluetooth detail while passkey is active | Existing `可被发现` row shows `PIN 123456` |
| Android HID phone connects | Keep default PHY; do not issue a local Coded PHY request |
| Link not encrypted/subscribed/suspended | Keep controls disabled and do not queue a report |
| GATT registration fails | Fail BLE-host initialization without starting an unusable advertiser |
| Phone disconnects | Clear HID state and let existing BMS scan arbitration resume |

### 5. Good / Base / Bad Cases

- Good: Android pairs in system Bluetooth settings, then receives five standard
  Consumer Control usages without a companion app. The verified full ESP32 HID
  profile uses PIN `123456` and 2-byte Usage ID reports.
- Base: an unpaired phone sees `PAIR PHONE`; BMS/controller operation remains
  unchanged.
- Bad: use AVRCP/A2DP, modify local volume for a phone-volume action, or enable
  private `phone-media` alongside HIDS.
- Bad: revert the report map to a one-byte bitmask; some hosts do not treat it
  as a normal Consumer Control selector.
- Bad: remove the active SMP/PIN path merely because Bluetooth headphones pair
  without a PIN; headphones are not evidence that this BLE HID profile can use
  Classic AVRCP.

### 6. Tests Required

- Run `./scripts/run-host-selftests.sh` and `./tests/configurator_selftest.sh`;
  assert the five usage-to-2-byte-report mappings and the module conflict.
- Build HID-on and HID-off BMS/controller profiles for both `esp32` and
  `esp32s3`. HID-on images must contain `[hid]`; HID-off images must not.
- Capture and inspect the HID music page at 480x320 and 240x320 under
  `preview/`.
- Flash a matching target through RFC2217 and verify Android pairing, the five
  actions, reconnection, and BMS/controller coexistence. The log should show
  `HID pairing will start after 1000 ms`, `HID passkey supplied`, `pairing
  started`, and then successful encryption/subscription before accepting media
  button validation.

### 7. Wrong vs Correct

#### Wrong

```c
runtime->volume_percent = requested_phone_volume;
```

#### Correct

```c
return runtime_ble_media_hid_enqueue(runtime,
                                     ESP_BMS_BLE_MEDIA_HID_USAGE_VOLUME_INCREMENT);
```

## Scenario: ESP32 Classic HID Media Profile

### 1. Scope / Trigger

- Trigger: adding an ESP32-only Classic Bluetooth media-control profile while
  keeping BLE HID as the fallback for ESP32-S3 and other BLE-only MCUs.

### 2. Signatures

```text
module: classic-media-hid (REQUIRES_CAPABILITIES=BT_CLASSIC, CONFLICTS=ble-media-hid,bms,controller,phone-media)
profile: ESP_BMS_FEATURE_CLASSIC_MEDIA_HID=0|1
sdkconfig defaults: sdkconfig.defaults.esp32-classic-media-hid
transport: Bluedroid Classic HID device, not NimBLE
manual send API: esp_bms_classic_media_hid_send_usage(uint16_t consumer_usage)
state API: esp_bms_classic_media_hid_tick(bool *connected, bool *suspended, bool *discoverable)
```

### 3. Contracts

- Classic HID is a separate ESP32 build profile. Do not initialize Bluedroid
  Classic beside the NimBLE BLE runtime in one firmware image.
- ESP32 catalog entries may advertise `BT_CLASSIC`; ESP32-S3 and BLE-only MCUs
  must reject `classic-media-hid` and use `ble-media-hid` instead.
- The Classic profile reuses the existing Bluetooth discoverability action and
  existing music page. Do not add a second music UI or a timer/demo task.
- `esp_bms_classic_media_hid_tick()` only projects connected/suspended/
  discoverable state into the existing runtime snapshot. It must never send a
  media usage.
- A media usage is sent only when a user action reaches
  `esp_bms_idf_runtime_apply_action_event()`, which calls
  `esp_bms_classic_media_hid_send_usage()`. The send function presses one
  Consumer Usage and releases it after 30 ms.

### 4. Validation & Error Matrix

| Condition | Required response |
| --- | --- |
| ESP32 profile selects `classic-media-hid` | Configurator enables `ESP_BMS_FEATURE_CLASSIC_MEDIA_HID=1` and disables BLE media HID |
| ESP32-S3 selects `classic-media-hid` | Configurator rejects with missing `BT_CLASSIC` capability |
| User toggles Bluetooth discoverability | Classic scan mode changes; no media usage is sent |
| User opens music page and taps a control while connected | Exactly one press/release Consumer Usage pair is sent |
| User does nothing after connecting | No periodic media usage is sent |

### 5. Good / Base / Bad Cases

- Good: ESP32 Classic profile pairs as a Classic HID device and media controls
  only fire from the five existing music-page buttons.
- Base: unsupported MCUs fall back to BLE HID profile selection.
- Bad: add a `while` loop, timer, FreeRTOS task, or 2-second demo sender for
  Classic media validation.

### 6. Tests Required

- Run `./tests/configurator_selftest.sh`; assert ESP32 accepts
  `classic-media-hid` and ESP32-S3 rejects it.
- Build the ESP32 Classic profile with `sdkconfig.defaults.esp32-classic-media-hid`.
- Grep the Classic component to confirm `esp_bms_classic_media_hid_send_usage`
  is only called from manual LVGL action dispatch.

### 7. Wrong vs Correct

#### Wrong

```c
while (true) {
    esp_bms_classic_media_hid_send_usage(ESP_BMS_BLE_MEDIA_HID_USAGE_PLAY_PAUSE);
    vTaskDelay(pdMS_TO_TICKS(2000));
}
```

#### Correct

```c
case ESP_BMS_LVGL_ACTION_MEDIA_PLAY_PAUSE:
    return esp_bms_classic_media_hid_send_usage(
               ESP_BMS_BLE_MEDIA_HID_USAGE_PLAY_PAUSE) == ESP_OK;
```

## Scenario: Legacy ESP32 On-Demand Radio Heap Budget

### 1. Scope / Trigger

- Trigger: enabling Wi-Fi, NimBLE, or additional LVGL pages on the 4 MB,
  no-PSRAM ESP32-WROOM-32E profile.

### 2. Signatures

```text
CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=1
CONFIG_LV_DRAW_THREAD_STACK_SIZE=8192
CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=4096
```

### 3. Contracts

- The base legacy ESP32 defaults must keep one LVGL software draw unit. A
  second unit permanently consumes another 8192-byte internal-DRAM task stack
  and can prevent the on-demand Wi-Fi or NimBLE task from starting.
- PSRAM-capable targets may opt into more draw units only in their target
  defaults and only after radio coexistence validation.
- Hidden carousel pages still own their LVGL objects. Treat every prebuilt page,
  QR code, draw buffer, and task stack as part of the boot-time internal-DRAM
  budget; Flash capacity does not make this memory allocatable as heap.

### 4. Validation & Error Matrix

| Condition | Required response |
| --- | --- |
| NimBLE initialized but Host task log is absent | Treat startup as failed; inspect largest free block against the Host stack requirement |
| `wifi nvs cfg alloc out of memory` | Reduce permanent internal-DRAM use before changing Wi-Fi retry logic |
| Free heap recovers to the same value after retries | Do not classify it as a retry leak without a descending trend |
| `bms_display` blocks in LVGL allocation under low heap | Restore heap headroom; do not mask it by disabling the watchdog |

### 5. Good / Base / Bad Cases

- Good: one draw unit leaves enough contiguous internal DRAM for BMS scanning
  and Setup AP startup after all normal UI pages are built.
- Base: a radio remains off until requested and consumes no controller/Host task
  heap before that request.
- Bad: enable a second 8 KB draw worker on legacy ESP32 because it has two CPU
  cores, without measuring the radio startup path.

### 6. Tests Required

- Assert the generated legacy `sdkconfig` contains
  `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=1` and no PSRAM.
- Build the feature-enabled ESP32 profile and run host plus LVGL simulator
  self-tests.
- On hardware, start BMS scan and Setup AP separately; require `NimBLE host task
  started`, `NimBLE synced`, `BLE scan started`, and no Wi-Fi `ESP_ERR_NO_MEM`.

### 7. Wrong vs Correct

```ini
# Wrong for legacy ESP32 without a measured radio heap budget
CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=2

# Correct
CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=1
```
