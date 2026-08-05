# Research: Classic Bluetooth media controls with BLE HID fallback

- Query: user now wants media controls to use Classic Bluetooth on chips that support it, and fall back to BLE HID on unsupported chips. Determine feasibility and minimal safe architecture in this repo with ESP-IDF 6.0.2.
- Scope: mixed
- Date: 2026-08-04

## Findings

### Short Answer

ESP32 can support Classic Bluetooth media-control experiments under ESP-IDF
6.0.2, but the current product cannot safely add Classic AVRCP beside the
existing NimBLE BLE runtime. ESP-IDF exposes a single Bluetooth host-stack
choice: Bluedroid for Classic/dual-mode or NimBLE for BLE-only. Therefore the
fallback cannot be one firmware image that initializes NimBLE plus Bluedroid
and chooses at runtime. It must be a profile/configuration choice at build time:
ESP32 Classic profile uses Bluedroid; ESP32-S3 and other BLE-only chips keep the
current NimBLE BLE HID profile.

The minimal safe architecture is to keep the already-verified BLE HID path as
the current task deliverable and split Classic Bluetooth into a follow-up spike.
Classic-on-ESP32 becomes safe only as an isolated profile first, then as a
larger Bluedroid BLE migration if it must coexist with BMS/controller BLE.

### Files Found

- `.trellis/tasks/07-29-ble-hid-media-controls/prd.md` - current task scope requires BLE HID Consumer Control and explicitly keeps Classic AVRCP/A2DP out of scope.
- `.trellis/tasks/07-29-ble-hid-media-controls/design.md` - design already states the runtime is the only NimBLE host owner and says Classic should be a later ESP32-only profile.
- `.trellis/tasks/07-29-ble-hid-media-controls/research/android-hid-pairing-failure.md` - records the hardware-verified BLE HID path and warns not to replace it with Classic in this task.
- `firmware/catalog/module/ble-media-hid.env` - BLE HID module requires `BLE`, conflicts with `phone-media`, and uses `legacy-runtime`.
- `firmware/catalog/module/phone-media.env` - existing companion-app phone media module also requires `BLE` and conflicts with `ble-media-hid`.
- `firmware/catalog/mcu/esp32.env` - current ESP32 catalog capabilities are only `BLE,WIFI`; no Classic capability is modeled.
- `firmware/catalog/mcu/esp32s3.env` - current ESP32-S3 catalog capabilities are `BLE,WIFI,PSRAM`; no Classic capability is modeled.
- `start.sh` - configurator validates module `REQUIRES_CAPABILITIES`, `CONFLICTS`, generated feature flags, and selected modules from catalog records.
- `firmware-builds/ble-media-hid-esp32/generated/profile.cmake` - checked-in HID profile enables `ESP_BMS_FEATURE_BLE_MEDIA_HID=1`, disables `ESP_BMS_FEATURE_PHONE_MEDIA=0`, and includes BMS/controller modules.
- `firmware-builds/ble-media-hid-esp32/sdkconfig.defaults` - checked-in HID profile uses NimBLE central/peripheral roles, GATT client/server, bonding, encryption, and three BLE connections.
- `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c` - owns NimBLE init, GAP/GATT service registration, security config, advertising, HID service, and HID action dispatch.
- `components/esp_bms_idf_runtime/include/esp_bms_ble_media_hid.h` - owns Consumer Control usages and the two-byte HID report encoder.
- `components/esp_bms_bms_ble/esp_bms_bms_ble.c` - BMS BLE driver is written directly against NimBLE GAP/GATT APIs.
- `components/esp_bms_controller_ble/esp_bms_controller_ble.c` - controller BLE driver is written directly against NimBLE GAP/GATT APIs.
- `/vol1/1000/toolchains/esp-idf-v6.0.2/components/bt/Kconfig` - ESP-IDF host stack is a single `choice BT_HOST`.
- `/vol1/1000/toolchains/esp-idf-v6.0.2/components/bt/controller/esp32/Kconfig.in` - ESP32 controller mode can be BLE-only, BR/EDR-only, or dual-mode.
- `/vol1/1000/toolchains/esp-idf-v6.0.2/components/soc/esp32/include/soc/soc_caps.h` - ESP32 has both BLE and Classic support macros.
- `/vol1/1000/toolchains/esp-idf-v6.0.2/components/soc/esp32s3/include/soc/soc_caps.h` - ESP32-S3 has BLE macros but no Classic support macro in the Bluetooth capability block.
- `/vol1/1000/toolchains/esp-idf-v6.0.2/components/bt/host/bluedroid/Kconfig.in` - Classic, A2DP, AVRCP, Classic HID, Bluedroid BLE GATTS/GATTC, and Bluedroid task Kconfig symbols.
- `/vol1/1000/toolchains/esp-idf-v6.0.2/components/bt/host/bluedroid/api/include/api/esp_avrc_api.h` - AVRCP pass-through command codes and API constraints.
- `/vol1/1000/toolchains/esp-idf-v6.0.2/examples/bluetooth/bluedroid/classic_bt/avrcp_ct_metadata/` - ESP-IDF AVRCP controller example uses Bluedroid + Classic + A2DP.
- `/vol1/1000/toolchains/esp-idf-v6.0.2/examples/bluetooth/esp_hid_device/` - ESP-IDF HID device example supports Classic HID device over Bluedroid.
- `/vol1/1000/toolchains/esp-idf-v6.0.2/components/esp_hid/` - ESP-IDF HID abstraction can select Classic BT or BLE transport, but Classic transport still depends on Bluedroid.

### Code Patterns

- Current task requirements explicitly say the firmware uses one NimBLE host and
  HID must coexist with BMS/controller BLE (`prd.md:9-13`); acceptance requires
  HID not to break BMS/controller BLE (`prd.md:26-32`). Classic AVRCP/A2DP is
  still listed out of scope (`prd.md:34-37`).
- The design states `esp_bms_idf_runtime` remains the only NimBLE host owner,
  responsible for NVS, `nimble_port_init()`, security, host task, advertising,
  phone connection, and BMS/controller scan arbitration (`design.md:5-9`).
  It also says Classic cannot be directly inserted into the current NimBLE HID
  delivery and should be a follow-up (`design.md:35-40`).
- The module catalog already enforces one phone-media path at a time:
  `ble-media-hid` requires `BLE` and conflicts with `phone-media`
  (`firmware/catalog/module/ble-media-hid.env:1-8`), while `phone-media`
  conflicts back (`firmware/catalog/module/phone-media.env:1-8`).
- The MCU catalog currently cannot express Classic support. ESP32 has only
  `CAPABILITIES=BLE,WIFI` (`firmware/catalog/mcu/esp32.env:1-9`), and ESP32-S3
  has only `CAPABILITIES=BLE,WIFI,PSRAM`
  (`firmware/catalog/mcu/esp32s3.env:1-9`).
- The configurator is already capability-driven. It reads MCU capabilities from
  catalog records (`start.sh:831-833`), rejects modules whose
  `REQUIRES_CAPABILITIES` are not present (`start.sh:631-639`), rejects module
  conflicts (`start.sh:646-649`), and emits generated feature flags such as
  `ESP_BMS_FEATURE_BLE_MEDIA_HID` (`start.sh:1010-1021`,
  `start.sh:1059-1064`, `start.sh:1074-1088`). Existing tests cover BLE
  capability rejection and `phone-media,ble-media-hid` conflict
  (`tests/configurator_selftest.sh:104-139`).
- The checked-in full HID profile enables BLE HID and keeps BMS/controller in
  the same build: `ESP_BMS_SELECTED_MODULES` includes `ble-media-hid,bms,controller`
  (`firmware-builds/ble-media-hid-esp32/generated/profile.cmake:1-3`),
  `ESP_BMS_FEATURE_BLE_MEDIA_HID=1` and `ESP_BMS_FEATURE_PHONE_MEDIA=0`
  (`firmware-builds/ble-media-hid-esp32/generated/profile.cmake:13-19`).
- That same HID profile is NimBLE-only at host level and enables both central
  and peripheral roles: `CONFIG_BT_NIMBLE_ROLE_CENTRAL=y`,
  `CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y`, `CONFIG_BT_NIMBLE_GATT_CLIENT=y`,
  `CONFIG_BT_NIMBLE_GATT_SERVER=y`, `CONFIG_BT_NIMBLE_SM_LVL=2`, and
  `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=3`
  (`firmware-builds/ble-media-hid-esp32/sdkconfig.defaults:59-77`).
- Runtime Bluetooth includes NimBLE headers, not Bluedroid headers
  (`components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:22-46`). It calls
  `nimble_port_init()`, initializes GAP/GATT services, sets GAP name/appearance,
  registers phone media or HID GATT service, sets `ble_hs_cfg`, creates the HID
  worker, then starts the NimBLE FreeRTOS host task
  (`components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:4796-4903`).
- The current HID implementation is already a manual NimBLE HOGP service:
  HID UUIDs, appearance `0x03C0`, passkey, encryption flags, HID Information,
  and report map live in `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:350-438`;
  DIS/BAS/HID service table is registered in
  `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:649-755`.
- HID media actions are already separated from LVGL local volume state: UI
  actions enqueue HID usages for previous, next, volume down/up, and play/pause
  (`components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:5738-5775`). The report
  encoder maps the five Consumer Usages to a two-byte little-endian report
  (`components/esp_bms_idf_runtime/include/esp_bms_ble_media_hid.h:6-39`).
- BMS/controller BLE are not behind a portable host abstraction. BMS includes
  NimBLE headers (`components/esp_bms_bms_ble/esp_bms_bms_ble.c:6-15`) and
  explicitly handles NimBLE's single discovery callback handoff
  (`components/esp_bms_bms_ble/esp_bms_bms_ble.c:1083-1107`). Controller BLE
  also includes NimBLE headers (`components/esp_bms_controller_ble/esp_bms_controller_ble.c:7-17`)
  and has the reciprocal discovery handoff
  (`components/esp_bms_controller_ble/esp_bms_controller_ble.c:649-672`).

### ESP-IDF 6.0.2 Bluetooth Feasibility

- ESP32 Classic support is real in ESP-IDF 6.0.2: ESP32 SoC caps define
  `SOC_BLE_SUPPORTED` and `SOC_BT_CLASSIC_SUPPORTED`
  (`/vol1/1000/toolchains/esp-idf-v6.0.2/components/soc/esp32/include/soc/soc_caps.h:391-398`).
- ESP32-S3 does not expose Classic support in its Bluetooth capability block:
  it defines BLE/BLE 5.0 related macros but no `SOC_BT_CLASSIC_SUPPORTED`
  (`/vol1/1000/toolchains/esp-idf-v6.0.2/components/soc/esp32s3/include/soc/soc_caps.h:522-527`).
  Since `BT_CLASSIC_ENABLED` depends on `SOC_BT_CLASSIC_SUPPORTED`, Classic is
  not available on ESP32-S3 under this IDF (`components/bt/host/bluedroid/Kconfig.in:44-47`).
- Bluedroid + NimBLE coexist is not an acceptable in-app architecture. ESP-IDF
  has one `choice BT_HOST`: `BT_BLUEDROID_ENABLED` is "Bluedroid - Dual-mode"
  for Classic/dual-mode use cases; `BT_NIMBLE_ENABLED` is "NimBLE - BLE only"
  (`/vol1/1000/toolchains/esp-idf-v6.0.2/components/bt/Kconfig:9-35`). A
  profile using Classic must unset NimBLE and select Bluedroid.
- If an ESP32 Classic profile still needs BLE central/peripheral behavior, the
  controller must be dual-mode, not BLE-only. ESP32 controller Kconfig offers
  `CONFIG_BTDM_CTRL_MODE_BLE_ONLY`, `CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY`, and
  `CONFIG_BTDM_CTRL_MODE_BTDM`
  (`/vol1/1000/toolchains/esp-idf-v6.0.2/components/bt/controller/esp32/Kconfig.in:1-15`).
- Bluedroid has its own BLE GATTS/GATTC paths: `CONFIG_BT_BLE_ENABLED`,
  `CONFIG_BT_GATTS_ENABLE`, and `CONFIG_BT_GATTC_ENABLE`
  (`/vol1/1000/toolchains/esp-idf-v6.0.2/components/bt/host/bluedroid/Kconfig.in:273-410`).
  Using them would mean porting the repo's NimBLE `ble_gap_*`, `ble_gattc_*`,
  `ble_gatts_*`, and `ble_hs_cfg` usage to Bluedroid APIs.
- AVRCP can send media pass-through key commands after an AVRCP controller
  connection. ESP-IDF exposes `ESP_AVRC_PT_CMD_VOL_UP`, `VOL_DOWN`, `PLAY`,
  `PAUSE`, `FORWARD`, and `BACKWARD`
  (`/vol1/1000/toolchains/esp-idf-v6.0.2/components/bt/host/bluedroid/api/include/api/esp_avrc_api.h:51-110`),
  press/release states
  (`/vol1/1000/toolchains/esp-idf-v6.0.2/components/bt/host/bluedroid/api/include/api/esp_avrc_api.h:131-135`),
  and `esp_avrc_ct_send_passthrough_cmd()`
  (`/vol1/1000/toolchains/esp-idf-v6.0.2/components/bt/host/bluedroid/api/include/api/esp_avrc_api.h:621-637`).
- AVRCP without A2DP/HFP is not supported by ESP-IDF Bluedroid. The Kconfig
  makes `BT_AVRCP_ENABLED` depend on `BT_A2DP_ENABLE` and documents AVRCP/A2DP
  coupling (`/vol1/1000/toolchains/esp-idf-v6.0.2/components/bt/host/bluedroid/Kconfig.in:84-107`).
  The API docs for both CT and TG say AVRC cannot work independently and should
  be used along with A2DP
  (`/vol1/1000/toolchains/esp-idf-v6.0.2/components/bt/host/bluedroid/api/include/api/esp_avrc_api.h:512-515`,
  `/vol1/1000/toolchains/esp-idf-v6.0.2/components/bt/host/bluedroid/api/include/api/esp_avrc_api.h:668-670`).
- The ESP-IDF AVRCP controller example confirms this coupling: it sets
  `CONFIG_BT_BLUEDROID_ENABLED=y`, `CONFIG_BT_CLASSIC_ENABLED=y`, and
  `CONFIG_BT_A2DP_ENABLE=y`
  (`/vol1/1000/toolchains/esp-idf-v6.0.2/examples/bluetooth/bluedroid/classic_bt/avrcp_ct_metadata/sdkconfig.defaults:1-8`),
  initializes AVRCP CT/TG, then initializes A2DP sink
  (`/vol1/1000/toolchains/esp-idf-v6.0.2/examples/bluetooth/bluedroid/classic_bt/avrcp_ct_metadata/main/main.c:228-239`).
  The example can disable actual audio data transfer with
  `EXAMPLE_A2DP_SINK_STREAM_ENABLE=n`, but that still leaves an A2DP profile in
  the product and may affect Android audio routing
  (`/vol1/1000/toolchains/esp-idf-v6.0.2/examples/bluetooth/bluedroid/classic_bt/avrcp_ct_metadata/main/Kconfig.projbuild:8-21`).
- Classic HID device is an alternative Classic media-control transport that
  avoids A2DP/AVRCP audio-routing risk, but it still requires Bluedroid. ESP-IDF
  `esp_hid_device` example enables `CONFIG_BT_BLUEDROID_ENABLED=y`,
  `CONFIG_BT_CLASSIC_ENABLED=y`, `CONFIG_BT_BLE_ENABLED=y`,
  `CONFIG_BT_HID_ENABLED=y`, and `CONFIG_BT_HID_DEVICE_ENABLED=y`
  (`/vol1/1000/toolchains/esp-idf-v6.0.2/examples/bluetooth/esp_hid_device/sdkconfig.defaults:1-10`).
  `esp_hid` exposes `ESP_HID_TRANSPORT_BT` and `ESP_HID_TRANSPORT_BLE`
  (`/vol1/1000/toolchains/esp-idf-v6.0.2/components/esp_hid/include/esp_hid_common.h:101-107`),
  and `esp_hidd_dev_init()` dispatches Classic BT transport only behind
  `CONFIG_BT_HID_DEVICE_ENABLED`
  (`/vol1/1000/toolchains/esp-idf-v6.0.2/components/esp_hid/src/esp_hidd.c:21-49`).
  The Classic HID Kconfig symbols are `CONFIG_BT_HID_ENABLED`,
  `CONFIG_BT_HID_HOST_ENABLED`, and `CONFIG_BT_HID_DEVICE_ENABLED`
  (`/vol1/1000/toolchains/esp-idf-v6.0.2/components/bt/host/bluedroid/Kconfig.in:215-240`).

### Minimal Safe Architecture

- Keep `ble-media-hid` as the current universal fallback. It is already aligned
  with ESP32-S3 and the current NimBLE central/peripheral architecture.
- Add Classic support only as a follow-up build-profile split, not as a second
  host initialized in the same firmware. The configurator should choose the
  transport based on MCU capabilities at profile generation time.
- Keep the first Classic profile manual-only: the existing Bluetooth action
  controls discoverability, the existing music page sends the five media usages,
  and no timer/demo task may send a usage every 2 seconds.
- Add a catalog capability such as `BT_CLASSIC` or `CLASSIC_BT` to
  `firmware/catalog/mcu/esp32.env`; do not add it to
  `firmware/catalog/mcu/esp32s3.env`.
- Add a new module such as `classic-media-avrcp` or `classic-media-hid` under
  `firmware/catalog/module/`. It should require `BT_CLASSIC` and conflict with
  `ble-media-hid` and `phone-media`. For an isolated first spike, it should also
  avoid BMS/controller coexistence unless the profile explicitly ports BLE to
  Bluedroid.
- Add generated feature flags in `start.sh`, for example
  `ESP_BMS_FEATURE_CLASSIC_MEDIA_AVRCP` or
  `ESP_BMS_FEATURE_CLASSIC_MEDIA_HID`, plus configurator selftests covering
  ESP32 accepted, ESP32-S3 rejected, and conflict with `ble-media-hid` /
  `phone-media`.
- Add separate `sdkconfig.defaults` / profile generation behavior for Classic
  ESP32:
  - AVRCP path: `CONFIG_BT_ENABLED=y`, `CONFIG_BT_BLUEDROID_ENABLED=y`,
    `CONFIG_BT_CLASSIC_ENABLED=y`, `CONFIG_BT_A2DP_ENABLE=y`, and either
    `CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY=y` for an isolated Classic-only spike or
    `CONFIG_BTDM_CTRL_MODE_BTDM=y` if Bluedroid BLE is also required.
  - Classic HID path: `CONFIG_BT_ENABLED=y`, `CONFIG_BT_BLUEDROID_ENABLED=y`,
    `CONFIG_BT_CLASSIC_ENABLED=y`, `CONFIG_BT_HID_ENABLED=y`,
    `CONFIG_BT_HID_DEVICE_ENABLED=y`, and the same BR/EDR-only vs dual-mode
    controller choice.
  - Both Classic paths must not set `CONFIG_BT_NIMBLE_ENABLED=y`.
- If Classic media must coexist with BMS/controller BLE on ESP32, the repo needs
  a larger Bluetooth host abstraction or explicit Bluedroid implementation of
  the current BMS/controller BLE flows. That includes replacing NimBLE
  `ble_gap_disc`, GATT discovery, CCCD subscription, notify handling, local
  peripheral advertising, security/bonding, snapshot updates, and scan handoff
  with Bluedroid GAP/GATTC/GATTS equivalents.

### Related Specs

- `.trellis/spec/backend/hardware-build-flash.md` records ESP-IDF `>=6.0.2, <6.1.0`,
  the project `v6.0.2` install path, profile-driven hardware assignments, and
  `sdkconfig.defaults` as the source of target constraints.
- `.trellis/spec/backend/quality-guidelines.md` requires firmware validation
  through `./scripts/esp-idf-env.sh build` and GitNexus change detection before
  commit, but this research-only turn did not edit firmware.
- `.trellis/spec/guides/code-reuse-thinking-guide.md` supports adding a small
  transport boundary only if it prevents duplicated media action logic; it does
  not justify duplicating BMS/controller BLE code without a deliberate follow-up
  migration.
- `AGENTS.md` says ESP32-S3 display/UI language constraints and preview path
  rules, but this research does not require UI preview changes.

### Risks And Recommended Split

- Risk to the verified BLE HID profile is high if Classic is folded into the
  current task. The verified path depends on NimBLE security, bonding, custom
  HID GATT, runtime-owned advertising, and BMS/controller scan arbitration. A
  host-stack swap touches all of those at once.
- Bluedroid dual-mode is memory and scheduling risk on the legacy ESP32 profile
  with 4 MB flash and no PSRAM. Bluedroid has its own BTC/BTU task stacks
  (`components/bt/host/bluedroid/Kconfig.in:1-35`), and dual-mode controller
  BR/EDR connections add static DRAM costs
  (`components/bt/controller/esp32/Kconfig.in:140-164`).
- AVRCP-on-phone behavior is product risk even if it compiles. ESP-IDF couples
  AVRCP to A2DP, so Android may route media audio to the ESP32 or expose it as
  an audio sink. Disabling the example's audio stream callback is not the same
  as "no A2DP profile".
- Current task should remain BLE HID only: preserve the working ESP32/ESP32-S3
  path, document Classic as follow-up, and avoid changing host ownership.
- Follow-up 1 should be an isolated ESP32 Classic spike, with no BMS/controller
  coexistence requirement, comparing silent A2DP+AVRCP CT against Classic HID
  Device for Android media-key behavior and audio-routing side effects.
- Follow-up 2 should proceed only if the spike is acceptable: either keep
  Classic as a mutually exclusive ESP32 media-control-only profile or perform a
  Bluedroid BLE migration for BMS/controller/phone peripheral behavior.

## Caveats / Not Found

- No source or spec files were edited; only this research file was written.
- I did not run builds, flash hardware, or pair an Android phone during this
  research-only turn.
- GitNexus MCP tools were not exposed in this agent environment. The local
  GitNexus CLI reported the index up to date at commit `bc6b65a` and was used
  only as a locator; final evidence above comes from checked-in files and the
  local ESP-IDF 6.0.2 source tree.
- I did not find ESP-IDF support for standalone AVRCP without A2DP in Bluedroid;
  the local Kconfig and API comments point the other way.
- Classic HID Device may satisfy the user-visible "media keys over Classic"
  goal with lower audio-routing risk than AVRCP, but it is still Bluedroid and
  still incompatible with the current NimBLE runtime without a separate profile
  or a Bluedroid BLE migration.
