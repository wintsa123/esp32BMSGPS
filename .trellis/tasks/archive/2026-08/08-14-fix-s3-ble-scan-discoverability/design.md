# Design

## Boundaries

- `esp_bms_bms_ble`: 从 NimBLE DISC 事件生成 BMS 候选。
- `esp_bms_controller_ble`: 从 NimBLE DISC 事件生成 Controller 候选。
- `esp_bms_idf_runtime`: 初始化本地手机蓝牙广告的用户意图标志。
- 现有 runtime candidate storage、snapshot、LVGL 列表、simulator bridge 与设置 action 保持不变。

## Candidate Data Flow

```text
NimBLE DISC event
  -> parse optional complete/short local name
  -> copy optional display name
  -> store MAC candidate unconditionally while scan is active
  -> existing per-MAC merge and bounded candidate array
  -> runtime snapshot
  -> existing shared BMS/Controller BLE list
```

名称是否存在只决定 `name` 参数，不再决定候选是否入表。现有 store 函数已经处理无名候选、同 MAC 合并、有名 scan response 补全和容量上限，因此不新增 helper。

## Discoverability Flow

```text
boot -> runtime_reset_state -> advertise requested = false
user enables discoverability -> existing action -> advertise requested = true -> advertise
user disables discoverability -> existing action -> stop advertise
```

BLE media HID 仍可编译和使用，但功能可用性不再隐含用户允许设备在启动时可发现。

## Compatibility And Risk

- 扫描回调恢复所有 MAC 候选会重新显示无名或非中文设备，这是回归前合同，也是 UI 占位设计的用途。
- `runtime_reset_state` 为 HIGH 风险共享初始化函数，但变更只替换一个布尔初始化表达式，不触及其余默认值。
- 不修改 NVS schema；升级后首次启动直接采用默认关闭，用户本次运行内手动开启仍有效。
- 回滚只需恢复三个条件表达式，不涉及数据迁移。
