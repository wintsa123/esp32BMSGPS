# Legacy ESP32 Compatibility Baseline

Captured: 2026-07-21T02:03:02+08:00

This baseline is an identity for the pre-migration working tree, not a clean
commit build. No reset, checkout, or source cleanup was performed.

## Source Identity

- Branch: `main`
- HEAD: `900b464273c224310f0db308f2d2eb4ee7242846` (`2026-07-17T09:39:54+08:00`)
- GitNexus: indexed commit equals HEAD; status was `up-to-date`.
- At collection start, the worktree had 21 tracked modifications and 160
  untracked, non-ignored paths. The firmware-input tracked diff has Git blob
  hash `b0e63186f384bf66da92e7fe5e521e50fa9a1f70`.
- The direct firmware-input hashes below identify the exact dirty source state
  used for the build. `scripts/run-host-selftests.sh` was an untracked input.

```text
12f441741c6155ab1c2498e1785114c20c37c4e554986a0b970eb7bf5b6cb4df  CMakeLists.txt
0a9b2fc56065e66caf0cd42eacaab5c1b48fcda8cb70102cb6a2fdd4d427bcc9  dependencies.lock
db13ad6f106a4a94cd285daa8ff3f48a2ee0551e7aa86b2c3392aa71a0f4284d  main/CMakeLists.txt
1d6632d1b4e45a06c85fadf6ceb3fb65fe18debc3d057557e5ff5d245df728a1  main/idf_component.yml
1acf7d5c0861ef3beb10b927eabd2149b2a76fae1c9ce17c584497ae45e68679  partitions.csv
ba10aea135accc48b752d485904e4b78056610f69b584ecce27cf800791c0548  scripts/esp-idf-env.sh
0db5f59d1676f04a4cfbe921db2119bb12f5bee573e8503226536daaad1ba261  scripts/run-host-selftests.sh
b5ff889d22963d45274d93b411e215d089f2c848846766c14b6e7c0f10cc7923  sdkconfig.defaults
e6485c39a0bbca0ff3cc48fe1c2544e054bce7f85e2cc7ffd07fbf9063e6b171  components/esp_bms_idf_runtime/esp_bms_idf_runtime.c
e0f4b5d0fcc6c8695b6352049a1095b68166c83485776333a44f79834caeec8c  components/esp_bms_idf_runtime/include/esp_bms_idf_runtime.h
5dc718cdccdb4ff83597bcd931990061ae751a466974691d8af7aa905be4e03b  components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c
c80a762d8938c150a32bb1fa12f6bfb1b0ed97610cf217c493090fdd9126c8e8  components/esp_bms_lvgl_ui/include/esp_bms_lvgl_ui.h
ab062329d63a0b3d1a7876d8e642bc0cadb8403de756f513bcccd0de9305cafc  components/esp_bms_lvgl_ui/settings_zh_10.c
fbf70edd264f81fa1f98dee03f8bc40188dfda05bac16fd03ad57d7bc011b73f  components/esp_bms_lvgl_ui/settings_zh_13.c
2e49d650b02a5cebeafe2d24ad569b019a8affe358e04a1891696179f5c6e8f8  components/esp_bms_lvgl_ui/settings_zh_16.c
c92c51fe3209988580af24f22ba74ffa1c1b95409f8aa54f26e668480cf5c7bc  main/idf_main.c
0dc74fe6b738e454d3c0c95f60c8acdbd8cd855710fb4a59832125840f22933a  simulator/CMakeLists.txt
6be76c207814fb77e4e5862383a08edb1c28b428d12835dc1e615b71bad835ea  simulator/main.c
```

## Toolchain And Configuration

- Python: `3.11.2`
- ESP-IDF: `v5.5.4` at `/home/wintsa/esp/esp-idf-v5.5.4`
- Dependency lock SHA-256: `0a9b2fc56065e66caf0cd42eacaab5c1b48fcda8cb70102cb6a2fdd4d427bcc9`
- Root `sdkconfig` SHA-256: `cbef5086850ccec426298dc52b7cfd26ea0b62bb8e254e8db63a7ceb21f0e7e1`
- Generated build config SHA-256: `ea24ade74b38ac0c53e4c43b55a0208ea6e61757ce30337bf1480af3557e4697`
- `sdkconfig.defaults` and the effective `sdkconfig` both select ESP32,
  4 MB flash, the custom `partitions.csv`, and revision 3 minimum. The
  effective configuration additionally uses DIO and 40 MHz flash.

## GPIO And Partition Contract

| Function | Baseline |
| --- | --- |
| TFT MISO / MOSI / SCLK / CS / DC | GPIO12 / GPIO13 / GPIO14 / GPIO15 / GPIO2 |
| TFT reset / backlight | not connected / GPIO21 |
| Touch IRQ / MISO / MOSI / CS / SCLK | GPIO36 / GPIO39 / GPIO32 / GPIO33 / GPIO25 |
| Local battery ADC | GPIO34, ADC1_CH6 |
| GPS UART1 RX / TX / PPS | GPIO27 / GPIO18 / GPIO35 at 115200 baud |
| Audio DAC / enable | GPIO26 / GPIO4 |
| Flash profile | ESP32-WROOM-32E, revision >=3, 4 MB, no PSRAM |

`partitions.csv` SHA-256 is
`1acf7d5c0861ef3beb10b927eabd2149b2a76fae1c9ce17c584497ae45e68679`:

```text
nvs       0x9000   0x4000
otadata   0xd000   0x2000
phy_init  0xf000   0x1000
ota_0     0x10000  0x1E0000
ota_1     0x1F0000 0x1E0000
```

## Host Validation

Passed without modifying firmware inputs:

```text
./scripts/run-host-selftests.sh
GPS stream self-test passed
speed dashboard self-test passed
FarDriver protocol self-test passed
A-GNSS helper self-test passed
firmware code self-test passed
```

## Isolated Build

Command:

```bash
./scripts/esp-idf-env.sh -B build/legacy-baseline build
./scripts/esp-idf-env.sh -B build/legacy-baseline size
./scripts/esp-idf-env.sh -B build/legacy-baseline size-components
```

All commands passed. The generated component graph is retained in
`build/legacy-baseline/project_description.json` (SHA-256
`d60cab3b9c894593ccb4047bbed2d59ec71b210b934b7bbd330e7a2bce2200ec`).
Its application closure includes `main`, `esp_bms_idf_runtime`,
`esp_bms_lvgl_ui`, `esp_bms_lvgl_bridge`, `esp_bms_lvgl_contract`,
`esp_bms_audio_feedback`, `esp_fardriver_protocol`, LVGL, Wi-Fi, BLE, HTTP,
NVS, ADC, LCD, and the managed touch/button components.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Application binary | 1519920 | `1ded3e035d5e61f11be7de05f91d2c3932ae5770eab2f89d9d6ff712953348f8` |
| Application map | 10195330 | `30d1da4d109e110366e71cc7bc09bcf488d6582888b257315aa4968dbe9f2dc7` |
| Bootloader | 24736 | `84363015d5d320763b11441790a26ef5fd4d1913e74f476288c76a819440f69e` |
| Partition table | 3072 | `dbbebb5ee220599f4b12fb5c43c01c38b59b2720b5df0d14f7d1b857f6524c92` |
| OTA data | 8192 | `7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f` |

The build reports an application image size of `0x173130` with `0x6ced0`
(23%) free in either 0x1E0000 OTA partition. The size report records 927768 B
flash code, 464316 B flash data, 105767 B IRAM (80.69%), and 60412 B DRAM
(48.49%).

## Production UI Simulator

Both production-source simulator smoke paths passed:

```bash
./scripts/run-lvgl-simulator.sh --headless
./scripts/run-lvgl-simulator.sh --headless --portrait
```

They completed 360 frames at 320x240 and 240x320 respectively, passed the
GPS/controller capability matrix, all three boot animations, Fireblade stress
updates, and the 320 km remaining-range check. The existing root preview is
`preview/legacy-esp32-baseline-20260721.png` (320x240, SHA-256
`570c22ebba55f35c486874ca169af38b35003446bb8b88bb71c6a1119084290e`), with
the simulator BMP source at `preview/legacy-esp32-baseline-20260721.bmp`
(SHA-256 `b0d387dc2d4dc498b9f91b1fe1b7d478a033336373bcbfa0f0a5807eb623447a`).

## RFC2217 Hardware Attempt

The configured TCP endpoint `192.168.2.10:4000` was reachable. Flashing the
isolated build through the required URL failed before the device booted:

```text
Could not open rfc2217://192.168.2.10:4000?ign_set_control, the port is busy or doesn't exist.
Remote does not seem to support RFC2217 or BINARY mode [we-BINARY:True(ACTIVE), we-RFC2217:False(REQUESTED)]
```

No firmware was written. Therefore cold boot, display, touch, ANT BMS, GPS,
Setup AP/Web, and OTA are **unverified**, not passed. The bridge must expose
Espressif RFC2217 negotiation and have no competing client before this task can
collect a hardware baseline.

Retry at `2026-07-21T09:39:32+08:00` confirmed that TCP port 4000 is reachable
and that the captured firmware inputs still match this baseline. Flashing
`build/legacy-baseline` again did not write the device because the server
rejected RFC2217 parameter negotiation for baud rate, data size, parity, and
stop bits. The Windows bridge still needs to run an Espressif-compatible
RFC2217 server with exclusive COM3 ownership.

## Scope Guard

`git diff --check` passed. GitNexus `detect-changes` reports 21 tracked files,
107 symbols, 82 execution flows, and critical risk. Those tracked source and
spec changes existed before this baseline evidence was added; this task added
only files under `.trellis/tasks/07-21-legacy-esp32-compat/` and did not edit a
firmware symbol. The required final compatibility comparison must start with a
fresh GitNexus impact analysis for each symbol it changes.

## Acceptance Status

| Criterion | Status | Evidence |
| --- | --- | --- |
| Baseline identity, config, graph, size/map/image hashes, and tests | PASS | This file and `build/legacy-baseline/` |
| Default build at task-start source state | PASS | Isolated build and size commands above |
| RFC2217 startup/peripheral checks | BLOCKED | Reachable TCP endpoint rejects RFC2217 negotiation |
| Final legacy profile compatibility comparison | PENDING | Requires later migration outputs to compare against this baseline |
| Pre-existing uncommitted files preserved | PASS | No reset/checkout/source edits; only task evidence added |
