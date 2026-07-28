# 修复 BMS 类型选择后重启

## Goal

设备在设置页确认任意 BMS 类型后不得触发崩溃或重启，选择结果仍应按现有流程生效。

## Confirmed Facts

- 用户在设置页选择 Daly 后稳定复现，随后补充确认选择任意 BMS 类型都会死机。
- 日志在 `resource action=select-bms-daly phase=begin` 后发生 `LoadProhibited`；`EXCVADDR=0x00000000` 表明存在空指针读取。
- 触发固件的 ELF SHA256 为 `10dece64a...`，日志中应用为 ESP-IDF v6.0.2、ESP32-D0WD-V3、4 MB flash。
- 此问题必须位于各 BMS 类型共用的选择确认、保存或页面资源切换路径，不能只为 Daly 添加分支保护。
- `esp_bms_idf_runtime_apply_action_event()` 将所有 `SELECT_BMS_*` 动作汇入
  `runtime_select_bms_type()`；后者在打印“type selected”前调用已注册的 BMS BLE driver 的
  `stop()`。
- `bms_stop()` 无条件调用 `ble_gap_disc_active()`，随后可能调用
  `ble_gap_disc_cancel()`、`ble_gap_conn_cancel()` 或 `ble_gap_terminate()`。
- 启动日志确认当前无绑定 MAC，因此 NimBLE host 未启动；同一组件的扫描路径只在
  `BLE_HOST_READY` 与 `BLE_HOST_SYNCED` 都为真时调用 GAP API。
- 本地工作区没有与日志 SHA256 对应的 ELF，不能将 PC `0x40130c24` 精确映射到源码行；
  但共享调用链、触发时序与 host 未初始化状态共同确认该未受保护的 GAP 调用是根因。

## Requirements

- R1：定位所有 BMS 类型选择共享的崩溃根因及其调用链。
- R2：以最小改动修复根因；保留现有 BMS 类型选择、持久化和连接流程。
- R3：不得修改与本缺陷无关的现有工作区改动。
- R4：NimBLE host 未初始化或尚未同步时，BMS 停止路径不得调用 NimBLE GAP API；仍必须清理
  BMS runtime 状态与遥测。

## Acceptance Criteria

- [ ] AC1：在设备上连续选择至少两个不同 BMS 类型后，不出现 Guru Meditation、软件复位或界面死机。
- [ ] AC2：所选 BMS 类型仍由现有设置流程保存并在重启后恢复。
- [ ] AC3：目标固件构建通过，且针对该缺陷的最小回归检查通过。
- [ ] AC4：已连接或扫描中的 BMS 更换类型后，仍会取消扫描或断开连接并进入空闲状态。
