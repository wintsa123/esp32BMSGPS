# Research: Android HID Pairing Failure

- Query: Android 在 BLE HID Consumer Control 配对弹窗点击“配对”后主动断开，设备端出现 `pairing failed: status=7` 或 disconnect reason=531；审计当前 HOGP 声明、ESP-IDF v6.0.2 NimBLE HID 做法，并给出最高概率根因。
- Scope: mixed
- Date: 2026-08-04

## Findings

### Verified Working Configuration Update - 2026-08-04

用户在刷入全功能 HID profile 后确认媒体控制功能正常。当前可用方案不是无 PIN
Just Works，也不是 Classic Bluetooth AVRCP；它是 BLE HID over GATT，并使用
ESP-IDF/NimBLE 的加密 HID 配对路径。

- Build/flash evidence: full ESP32 HID profile flashed through
  `rfc2217://192.168.2.10:4000?ign_set_control` at 115200 baud; boot log shows
  `App version: a0148d9f-dirty` and `ELF file SHA256: f7d80df36...` in
  `logs/ble-hid/20260804-212911-idf-monitor-hid-security-lvl2.log`.
- Required profile security: `CONFIG_BT_NIMBLE_SM_LVL=2` in
  `config/sdkconfig/sdkconfig.defaults`,
  `firmware-builds/ble-media-hid-esp32/sdkconfig.defaults`, and
  `firmware-builds/ble-media-hid-esp32/sdkconfig`.
- Runtime security parameters: HID build uses `BLE_SM_IO_CAP_DISP_ONLY`,
  `sm_bonding=1`, `sm_mitm=1`, `sm_sc=1`, and `ENC | ID` key distribution.
- Runtime pairing flow: after ACL connect, keep default PHY and schedule
  `ble_gap_security_initiate()` after the local 1000 ms delay. `PASSKEY_ACTION`
  display/input injects fixed PIN `123456`; numeric comparison is accepted by
  firmware.
- Device UI requirement: while `bms_error_text` is `PIN 123456`, the Settings >
  Bluetooth detail page shows `PIN 123456` on the existing `可被发现` row so the
  user sees the PIN from the Bluetooth discovery area.
- GATT compatibility pieces retained: Generic Attribute service init
  (`ble_svc_gatt_init()`), DIS, BAS, PnP ID, External Report Reference, Report
  Reference, encrypted HIDS reads/writes, and encrypted CCCD writes.
- Report format retained: Consumer Control input report is Report ID 1 with a
  2-byte little-endian Usage ID, followed by a 2-byte zero release report.
  Reverting to the older one-byte bitmask is not the validated path.
- Headphone comparison: Bluetooth headphones can change volume/track/playback
  without a visible PIN because they normally use Classic Bluetooth AVRCP after
  audio-profile pairing. This project must remain BLE HID for ESP32-S3
  compatibility, so AVRCP/A2DP is not a valid replacement for this task.

Do not treat earlier “let Android trigger SMP and do not proactively initiate
security” notes in this research file as the final answer; the hardware-verified
working path above supersedes that hypothesis.

### Files Found

- `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c` - 当前 BLE HID GATT 声明、GAP 事件处理、安全参数、广告和主动配对调度都集中在此文件。
- `components/esp_bms_idf_runtime/include/esp_bms_ble_media_hid.h` - Consumer Control report id、release 值和五个 media usage 到 bit 的映射。
- `firmware-builds/ble-media-hid-esp32/sdkconfig.defaults` - HID 构建 profile 的 NimBLE 安全、bond、连接、CCCD 和标准服务裁剪默认值。
- `firmware-builds/ble-media-hid-esp32/sdkconfig` - 当前生成配置，确认实际 profile 中 NimBLE HID/BAS/DIS 内置服务关闭，项目使用手写 GATT 服务。
- `/vol1/1000/toolchains/esp-idf-v6.0.2/examples/bluetooth/esp_hid_device/main/esp_hid_gap.c` - ESP-IDF BLE HID device 示例的 NimBLE GAP、安全参数和 passkey 处理。
- `/vol1/1000/toolchains/esp-idf-v6.0.2/components/esp_hid/src/nimble_hidd.c` - ESP-IDF NimBLE HIDD wrapper 的 HID/DIS/BAS 初始化和 GAP listener。
- `/vol1/1000/toolchains/esp-idf-v6.0.2/components/bt/host/nimble/nimble/nimble/host/services/hid/src/ble_svc_hid.c` - ESP-IDF 随带 NimBLE HID service 对 HID characteristic、descriptor 和安全 flag 的实现。
- `/vol1/1000/toolchains/esp-idf-v6.0.2/components/bt/host/nimble/Kconfig.in` - NimBLE security level、HID/BAS/DIS service 的 Kconfig 默认值。
- `.trellis/tasks/07-29-ble-hid-media-controls/prd.md` - 目标是 Android 系统蓝牙配对为标准 BLE HID Consumer Control，无 companion app。
- `.trellis/tasks/07-29-ble-hid-media-controls/design.md` - 设计约束是不使用 `esp_hid` wrapper 的全局 GAP ownership，改由 runtime 手写 HOGP 服务以兼容中央/外设共存。

### Current Repo HOGP Declaration Audit

结论：按当前源码看，DIS、PnP ID、BAS 和 External Report Reference 已经存在，不是完全缺失状态。剩余可疑点集中在 HID Information flags、Generic Attribute service/Service Changed、以及安全流程。

- HID UUID 声明完整：HID service `0x1812`、HID Information `0x2A4A`、Report Map `0x2A4B`、Control Point `0x2A4C`、Report `0x2A4D`、Protocol Mode `0x2A4E`、External Report Reference `0x2907`、Report Reference `0x2908`、DIS `0x180A`、PnP ID `0x2A50`、BAS `0x180F`、Battery Level `0x2A19` 都已定义，见 `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:349-363`。
- HID Information 当前为 `{0x11, 0x01, 0x00, 0x02}`，即 HID 1.11、country 0、flags 只有 Normally Connectable；ESP-IDF HIDD wrapper 使用 `Remote Wake | Normally Connectable`，即 flags 通常为 `0x03`。当前缺少 Remote Wake bit，见 `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:397` 与 `/vol1/1000/toolchains/esp-idf-v6.0.2/components/esp_hid/src/nimble_hidd.c:99-104`。
- Report Map 是单一 Consumer Control Application Collection，Report ID 1，包含 Next、Previous、Play/Pause、Volume Down、Volume Up 五个 1-bit Input usage 和 3-bit padding，见 `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:398-407`；usage 到 bit 的发送映射见 `components/esp_bms_idf_runtime/include/esp_bms_ble_media_hid.h:6-44`。
- Report Reference descriptor 当前返回 `{report_id=1, type=0x01/Input}`，见 `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:408-410`；descriptor 本身是 `BLE_ATT_F_READ` 明文读，见 `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:616-621`。这与 ESP-IDF NimBLE HID service 的 Report Reference descriptor 明文 `BLE_ATT_F_READ` 一致，见 `/vol1/1000/toolchains/esp-idf-v6.0.2/components/bt/host/nimble/nimble/nimble/host/services/hid/src/ble_svc_hid.c:388-394`。
- Report Map descriptor 当前包含 External Report Reference 指向 BAS UUID `0x180F`，见 `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:412-415` 和 `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:625-630`。这与 ESP-IDF HIDD wrapper 设置 `hparams.external_rpt_ref = BLE_SVC_BAS_UUID16` 的做法一致，见 `/vol1/1000/toolchains/esp-idf-v6.0.2/components/esp_hid/src/nimble_hidd.c:119-122`。
- DIS 已注册为 Primary Service，包含 Manufacturer Name、Model Number、PnP ID，且 PnP ID 为 USB VID `0x16C0`、PID `0x05DF`、version `0x0100`，见 `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:416-424` 和 `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:634-658`。ESP-IDF 示例也使用 VID `0x16C0`、PID `0x05DF`、version `0x0100`，见 `/vol1/1000/toolchains/esp-idf-v6.0.2/examples/bluetooth/esp_hid_device/main/esp_hid_device_main.c:385-399`。
- BAS 已注册为 Primary Service，Battery Level 为静态 `100`，characteristic 只有 read，无 notify/CCCD，见 `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:422`、`components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:556-568`、`components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:659-669`。ESP-IDF BAS service 也至少提供 read；notify 取决于 `CONFIG_BT_NIMBLE_SVC_BAS_BATTERY_LEVEL_NOTIFY`，见 `/vol1/1000/toolchains/esp-idf-v6.0.2/components/bt/host/nimble/nimble/nimble/host/services/bas/src/ble_svc_bas.c:47-60`。
- HID characteristic 安全权限比 `SM_LVL=1` 下的 ESP-IDF `ble_svc_hid.c` 更严格：当前 HID Information、Report Map、Protocol Mode、Input Report 都设置了 `READ_ENC`，Control Point、Protocol Mode 设置 `WRITE_ENC`，Input Report 设置 `BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC`，见 `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:671-706`。
- CCCD 写加密是有效的：NimBLE 定义 `BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC` 为 CCCD Write Encrypted，见 `/vol1/1000/toolchains/esp-idf-v6.0.2/components/bt/host/nimble/nimble/nimble/host/include/host/ble_gatt.h:174-175`；GATTS 会把该 flag 转成 CCCD 的 `BLE_ATT_F_WRITE_ENC`，见 `/vol1/1000/toolchains/esp-idf-v6.0.2/components/bt/host/nimble/nimble/nimble/host/src/ble_gatts.c:366-375`。
- 当前 runtime 只看到 `ble_svc_gap_init()`，未看到 `ble_svc_gatt_init()`；ESP-IDF HIDD wrapper 同时调用 GAP 和 GATT service init，见 `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:4769-4779` 与 `/vol1/1000/toolchains/esp-idf-v6.0.2/components/esp_hid/src/nimble_hidd.c:192-200`。这意味着当前服务表可能缺少 Generic Attribute service / Service Changed characteristic；它更像 GATT 兼容性/缓存风险，而不是直接证明的 SMP 配对失败原因。
- 当前 HID service 没有通过 `ble_gatt_svc_def.includes` include BAS；但本地 ESP-IDF v6.0.2 HIDD wrapper 中 `hid_incl_svc` 结构成员没有发现实际接入 `includes`，`ble_svc_hid.c` 也只把 HID service 建为 Primary Service。因此这不是本地 IDF wrapper 的明确差异，只能作为 HOGP 兼容性排查项，见 `/vol1/1000/toolchains/esp-idf-v6.0.2/components/esp_hid/src/nimble_hidd.c:89-91`、`/vol1/1000/toolchains/esp-idf-v6.0.2/components/esp_hid/src/nimble_hidd.c:648-746`、`/vol1/1000/toolchains/esp-idf-v6.0.2/components/bt/host/nimble/nimble/nimble/host/services/hid/src/ble_svc_hid.c:786`、`/vol1/1000/toolchains/esp-idf-v6.0.2/components/bt/host/nimble/nimble/nimble/host/include/host/ble_gatt.h:1100-1105`。

### Current Repo Pairing And GAP Behavior

- HID build 在连接成功时总是把 `bluetooth_pair_initiate_at_us` 设置为当前时间 + 1000 ms，见 `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:4339-4347`；延迟常量为 `LOCAL_BLUETOOTH_PAIR_INITIATE_DELAY_MS 1000U`，见 `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:72-73`。
- 后续 tick 中，只要未标记 `BLUETOOTH_CONNECTED` 且 handle 仍有效，就主动调用 `ble_gap_security_initiate()`；成功或 `BLE_HS_EALREADY` 都会打印 `pairing started`，见 `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:5241-5263`。
- ENC_CHANGE 成功才把 `BLUETOOTH_CONNECTED` 和 `ble_media_hid_connected` 置 true；ENC_CHANGE 失败时打印 `pairing failed: ... status=%d find_rc=%d`，并调用 `ble_gap_terminate(..., BLE_ERR_REM_USER_CONN_TERM)`，见 `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:4440-4483`。
- DISCONNECT 会清除 conn handle、pair deadline、pair initiate 时间和 HID snapshot，见 `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:4409-4428`。
- 当前 HID build 的 security 参数是 `sm_bonding=1`、`sm_io_cap=BLE_SM_IO_CAP_DISP_YES_NO`、`sm_mitm=1`、`sm_sc=1`、双方 key distribution 都是 `ENC | ID`，并启用 `ble_store_config_init()`，见 `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:4812-4826`。
- PASSKEY_ACTION 中，NUMCMP 被设备端自动接受；DISP 和 INPUT 都注入固定 passkey `123456`，并把屏幕状态设为 `PIN 123456`，见 `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:4517-4548`。这在安全语义上相当于设备声明自己有 Yes/No 确认能力，但没有真实的本机确认动作。
- 广播包含 HID service UUID、appearance `0x03C0` 和完整名称，见 `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:4608-4630`；advertising 参数为 undirected connectable + general discoverable，见 `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:4652-4663`。

### ESP-IDF v6.0.2 NimBLE HID Reference Behavior

- ESP-IDF `esp_hid_device` 的 NimBLE example 在 `Kconfig.projbuild` 中选择 media/keyboard/mouse role 时会 `select BT_NIMBLE_HID_SERVICE`，见 `/vol1/1000/toolchains/esp-idf-v6.0.2/examples/bluetooth/esp_hid_device/main/Kconfig.projbuild:10-31`；NimBLE HID service Kconfig 默认关闭，BAS/DIS 默认开启，见 `/vol1/1000/toolchains/esp-idf-v6.0.2/components/bt/host/nimble/Kconfig.in:900-935`。
- 示例 NimBLE GAP 安全参数：`sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY`、`sm_bonding = 1`、`sm_mitm = 1`、`sm_sc = 1`、双方 key distribution 为 `ID | ENC`，见 `/vol1/1000/toolchains/esp-idf-v6.0.2/examples/bluetooth/esp_hid_device/main/esp_hid_gap.c:795-801`。
- 示例 PASSKEY_ACTION 支持 DISP、NUMCMP、OOB、INPUT；DISP/INPUT 使用固定 `123456`，NUMCMP 自动接受，见 `/vol1/1000/toolchains/esp-idf-v6.0.2/examples/bluetooth/esp_hid_device/main/esp_hid_gap.c:893-925`。
- 示例 ENC_CHANGE 成功后启动 HID task；失败时只打印 `encryption failed; waiting for disconnect/retry`，未主动 terminate，见 `/vol1/1000/toolchains/esp-idf-v6.0.2/examples/bluetooth/esp_hid_device/main/esp_hid_gap.c:855-865`。
- 在本地 ESP-IDF v6.0.2 的 BLE HID device 示例与 `nimble_hidd.c` 中，没有发现 HID peripheral 路径主动调用 `ble_gap_security_initiate()`；搜索命中只在 HID host `nimble_hidh.c:861`。这说明 IDF 的 HID 外设参考路径更倾向于让主机/中心设备或加密属性访问触发安全过程。
- ESP-IDF HIDD wrapper 包含 BAS、HID、DIS service 头文件，见 `/vol1/1000/toolchains/esp-idf-v6.0.2/components/esp_hid/src/nimble_hidd.c:26-28`。
- ESP-IDF HIDD wrapper 的 HID Information flags 为 Remote Wake + Normally Connectable，见 `/vol1/1000/toolchains/esp-idf-v6.0.2/components/esp_hid/src/nimble_hidd.c:99-104`。
- ESP-IDF HIDD wrapper 对 Report Map 设置 External Report Reference 指向 BAS UUID `0x180F`，见 `/vol1/1000/toolchains/esp-idf-v6.0.2/components/esp_hid/src/nimble_hidd.c:119-122`。
- ESP-IDF HIDD wrapper 初始化 DIS，并设置 7-byte PnP ID、Manufacturer、Serial，见 `/vol1/1000/toolchains/esp-idf-v6.0.2/components/esp_hid/src/nimble_hidd.c:169-189`。
- ESP-IDF HIDD wrapper 初始化 GAP、GATT、SPS、BAS、DIS，然后添加 HID DB 并调用 `ble_svc_hid_init()`，见 `/vol1/1000/toolchains/esp-idf-v6.0.2/components/esp_hid/src/nimble_hidd.c:192-210`。
- ESP-IDF HIDD wrapper 有 BAS battery update 路径 `ble_svc_bas_battery_level_set(level)`，见 `/vol1/1000/toolchains/esp-idf-v6.0.2/components/esp_hid/src/nimble_hidd.c:376-390`。
- ESP-IDF HIDD wrapper 明确注释 HOGP 要求访问 HID report characteristic 前链路已加密，但 wrapper 自身不强制，要求通过 `BLE_GATT_CHR_F_READ_ENC` / `WRITE_ENC` 或应用层 `ble_hs_cfg.sm_*` 实现，见 `/vol1/1000/toolchains/esp-idf-v6.0.2/components/esp_hid/src/nimble_hidd.c:572-576`。
- ESP-IDF NimBLE HID service 在 `BT_NIMBLE_SM_LVL=1` 时不会自动给 HID characteristic 添加 `READ_ENC` / `WRITE_ENC`；只有 `SM_LVL=2` 才加 encrypted，`SM_LVL=3` 才加 authenticated + encrypted，见 `/vol1/1000/toolchains/esp-idf-v6.0.2/components/bt/host/nimble/nimble/nimble/host/services/hid/src/ble_svc_hid.c:180-201`、`:331-360`、`:429-436`、`:451-468`、`:477-494`。
- 当前 profile `CONFIG_BT_NIMBLE_SM_LVL=1`，见 `firmware-builds/ble-media-hid-esp32/sdkconfig.defaults:65-70` 和 `firmware-builds/ble-media-hid-esp32/sdkconfig:868-874`。因此如果后续改用 ESP-IDF `ble_svc_hid.c`，需要同步调整 SM_LVL 或补安全 flags；当前手写服务已手动补了 encrypted flags。
- 当前 profile 禁用了内置 `CONFIG_BT_NIMBLE_HID_SERVICE`、`CONFIG_BT_NIMBLE_BAS_SERVICE`、`CONFIG_BT_NIMBLE_DIS_SERVICE`，见 `firmware-builds/ble-media-hid-esp32/sdkconfig.defaults:83-96` 和 `firmware-builds/ble-media-hid-esp32/sdkconfig:979-981`。这不是当前服务缺失的证据，因为 runtime 已手写注册 DIS/BAS/HID。

### Error-Code Interpretation

- `BLE_HS_ENOTCONN` 的值是 `7`，语义是 not connected，见 `/vol1/1000/toolchains/esp-idf-v6.0.2/components/bt/host/nimble/nimble/nimble/host/include/host/ble_hs.h:84-90`。
- NimBLE HCI error `BLE_ERR_REM_USER_CONN_TERM` 是 `0x13`，见 `/vol1/1000/toolchains/esp-idf-v6.0.2/components/bt/host/nimble/nimble/nimble/include/nimble/ble.h:210-216`；NimBLE 错误字符串把它解释为 Remote User Terminated Connection，见 `/vol1/1000/toolchains/esp-idf-v6.0.2/components/bt/host/nimble/nimble/nimble/host/src/ble_hs_hci.c:149-168`。
- disconnect reason `531` 等于 `0x0213`，低 8 位 `0x13` 与 Remote User Terminated Connection 对齐。结合用户现象“Android 弹窗点击配对后主动断开”，这更像 Android 先断开，设备端随后在 SMP/ENC_CHANGE 或本地 terminate 路径上看到 ENOTCONN，而不是设备本地首先主动拒绝连接。

### Highest-Probability Root Causes And Minimal Modifications

1. **最高概率：HID 外设主动 `ble_gap_security_initiate()` 的时机与 Android 系统配对流程竞争。**
   证据：当前 HID 连接 1 秒后必定主动安全请求，并把成功/ALREADY 统一打印为 `pairing started`；IDF BLE HID 外设参考路径没有主动 security initiate，而是让中心设备/加密属性访问驱动配对。用户日志中的 `pairing started` 后 `status=7` 或 reason 531，正符合“Android 点击配对后先断开，本机后续 SMP 状态回调/安全请求看到连接已不存在”的形态。
   最小修改建议：HID feature 下先禁用或显著延后 `bluetooth_pair_initiate_at_us` 的主动触发，依赖 Android 读取 `READ_ENC` 的 HID Information/Report Map/Input Report 或写加密 CCCD 触发配对；同时 ENC_CHANGE 失败若 `status == BLE_HS_ENOTCONN` 或 `ble_gap_conn_find()` 已失败，不再主动 `ble_gap_terminate()`，只清状态并恢复广告。先只改这个点，复测一次配对。

2. **第二概率：当前 `DISP_YES_NO + MITM + SC` 声明了设备有本机确认能力，但实际流程自动确认/固定 PIN，可能与 Android 的配对 UX 路径不匹配。**
   证据：当前 HID build 设置 `sm_io_cap=BLE_SM_IO_CAP_DISP_YES_NO`、`sm_mitm=1`、`sm_sc=1`；NUMCMP 被自动接受，DISP/INPUT 注入固定 `123456`。ESP-IDF HID 示例使用的是 `BLE_SM_IO_CAP_DISP_ONLY + MITM + SC`，并明确提示用户在 peer 端输入 `123456`。当前产品 TFT 只有 ASCII，显示 `PIN 123456` 可行，但没有真实 Yes/No 确认输入路径。
   最小修改建议：在 root cause 1 复测仍失败时，把 HID security 参数先改成与 ESP-IDF 示例一致的 `BLE_SM_IO_CAP_DISP_ONLY`、`sm_mitm=1`、`sm_sc=1`，保留 key distribution；只处理/展示固定 `123456` 的 passkey display。若目标是无 PIN 的“只点配对”，则作为另一个单独实验改成 `NO_IO + sm_mitm=0 + sm_sc=1`，不要和主动配对时机修改混在同一次测试里。

3. **第三概率：HOGP/GATT 兼容性仍有小偏差，但已不是 DIS/BAS 完全缺失。**
   证据：当前已手写 DIS、PnP ID、BAS、External Report Reference、Report Reference、加密 HID reads/writes 和加密 CCCD；因此“缺 DIS/PnP ID/BAS/Report Reference/Report Map 权限/CCCD 安全权限”不是当前源码的主要缺口。仍偏离 IDF reference 的点包括 HID Information flags 缺 Remote Wake bit，以及 runtime 没有看到 `ble_svc_gatt_init()`，而 IDF wrapper 会注册 Generic Attribute service。
   最小修改建议：把 HID Information flags 从 `0x02` 调整为 `0x03`（Remote Wake | Normally Connectable）；在手写服务注册路径中补 `ble_svc_gatt_init()`，保持 `ble_svc_gap_init()` 之后、业务服务注册之前；保留当前 DIS/BAS/External Report Reference 和 encrypted HID flags。BAS notify/CCCD、HID includes BAS、Serial Number 可以作为后续兼容性增强，不建议第一轮就扩大修改面。

### Related Specs

- `.trellis/tasks/07-29-ble-hid-media-controls/prd.md:3-31` 要求 Android 系统蓝牙完成标准 BLE HID Consumer Control 配对、重连和五个媒体动作，无 companion app。
- `.trellis/tasks/07-29-ble-hid-media-controls/design.md:3-10` 规定 runtime 继续拥有唯一 NimBLE Host，并手写 HOGP 服务；`.trellis/tasks/07-29-ble-hid-media-controls/design.md:38-42` 说明不直接使用 `esp_hid` wrapper 的原因是中央/外设共存和全局 GAP listener 风险。
- `.trellis/tasks/07-29-ble-hid-media-controls/implement.md:5-19` 把 Android 真机配对和 ESP-IDF 6.0.2 构建列为最终验证；本次研究按用户要求未刷机、未改源码。

## Caveats / Not Found

- 未进行 Android HCI snoop / air trace，也未刷机复测；根因排序基于当前源码、ESP-IDF v6.0.2 本地参考实现和用户给出的日志形态。
- GitNexus CLI 状态显示索引相对当前 commit 已 stale；本研究仅把 GitNexus 查询当作定位辅助，最终证据均来自当前工作区文件和本地 ESP-IDF 文件。
- 未查到当前 runtime 调用 `ble_svc_gatt_init()`；如果其他平台初始化层隐式注册 Generic Attribute service，需要用运行时 GATT discovery 或最终 build map 再确认。
- 当前 `sdkconfig` 关闭内置 NimBLE HID/BAS/DIS service，但源码手写注册了对应服务；不能仅根据 Kconfig unset 判断 Android 看到的服务缺失。
- `status=7` 与 reason 531 的解释能证明“连接不存在/远端断开”的方向，但不能单独证明 Android 断开的具体策略原因；需要 Android 侧 Bluetooth log 或 HCI snoop 才能区分 SMP 参数拒绝、GATT profile 校验失败、bond/cache 问题或厂商策略。
