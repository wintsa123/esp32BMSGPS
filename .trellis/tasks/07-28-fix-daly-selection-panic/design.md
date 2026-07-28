# 设计：BMS 类型选择后的未初始化 NimBLE 停止路径

## Boundary

修改仅限 `components/esp_bms_bms_ble/esp_bms_bms_ble.c` 的 `bms_stop()`。
调用者 `runtime_select_bms_type()`、BMS 类型枚举、NVS 保存和协议实现均不变。

## Data Flow

`SELECT_BMS_*` -> `runtime_select_bms_type()` -> BMS driver `bms_stop()`。

无绑定 MAC 时，BLE driver 已注册但 NimBLE host 未启动。当前 `bms_stop()` 在清理 runtime
状态前访问 GAP 全局状态，导致空指针读取。修复后，只有 host 已 ready 且 synced 时才执行
发现取消、连接取消和断开连接；状态、遥测及 UI 信息的本地清理始终执行。

## Compatibility

- 未启动 BLE：选择任意 BMS 类型只更新选择并清理本地 BMS 状态，不访问 NimBLE。
- 已启动 BLE：保持既有发现取消、连接取消与连接终止行为。
- 已绑定 MAC：`runtime_select_bms_type()` 在停止完成后仍按既有逻辑重新启动已绑定 BMS。

## Rollback

单文件条件保护。若已启动 BLE 的取消流程回归，删除该保护即可恢复原有行为。
