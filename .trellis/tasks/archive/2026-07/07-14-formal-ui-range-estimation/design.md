# 技术设计

## Architecture

数据流保持单向：

`NVS/default -> runtime snapshot -> pure C range helper -> snapshot result -> LVGL render`

设置流保持现有动作队列：

`four rollers -> action event numeric_delta -> idf_main dispatcher -> runtime apply -> NVS save -> snapshot/UI refresh`

模拟器使用相同的 snapshot、helper 和 action event，不再在 LVGL 对象树完成后注入覆盖控件。

## Contracts

### Pure C helper

- 在 `esp_bms_speed_dashboard.h/.c` 增加剩余里程计算入口。
- 输入同时包含预设里程、SOC 有效性、实测电耗及 BMS 能量输入有效性。
- 优先使用正的公制实测电耗；否则回退 SOC 估算；两阶段都不可用时返回无效。
- 使用 64 位中间值、整数半分母四舍五入，并在输出处钳制 `9999`。

### Snapshot

- `preset_range_km`: 当前持久化配置，范围 `0..9999`。
- `remaining_range_km`: runtime 计算后的显示值，范围 `0..9999`。
- `remaining_range_valid`: 仅表示算法是否有可显示结果；可见性仍由 UI 同时检查 BMS 在线和 GPS fix。

### Action

- `ESP_BMS_LVGL_ACTION_SET_PRESET_RANGE` 使用 `numeric_delta` 作为绝对值。
- `ESP_BMS_LVGL_ACTION_EVENT_FLAG_NUMERIC_DELTA_VALID` 作为载荷有效标志。
- runtime 在入口校验 `0..9999`，更新快照并触发现有显示设置保存路径。

### Persistence

- 在 namespace `esp_bms` 中增加短 NVS key，类型 `u16`。
- 缺键是兼容升级路径，不使整组显示设置加载失败；填入 100 并在现有保存流程中持久化。
- 其他读取错误仍按当前错误策略返回。

## LVGL Design

- 复用现有 label/card/row/roller/button helper；不引入新组件层。
- BMS 页面对象指针加入现有 `s_ui` 状态，创建和旋转函数共同维护位置、尺寸、字体和隐藏状态。
- 四位编辑器复用控制器轮胎/速比编辑器的固定导航和 roller 样式；只保留四个数字和一个确认动作。
- 返回路径只销毁编辑页，不写 snapshot；确认路径排队 action 后返回 BMS 详情页。
- 中文 label 使用 settings 字体，数值与 `km` 可继续使用 ASCII 字体。

## Compatibility

- 新固件读取旧 NVS 时使用 100 km；不擦除其他设置。
- `0000` 不被当作缺省或无效。
- 平均电耗 snapshot 字段保留当前按速度单位投影的显示用途；range helper 使用 runtime 直接获取的公制值，避免 mph 换算污染。
- 结构体新增字段后更新编译期尺寸断言，所有生产者初始化新增字段。

## Risk And Containment

- `runtime_update_snapshot_speed`: CRITICAL，21 个上游符号/8 条流程。仅追加公制电耗获取和 range helper 调用。
- `settings_show_bms_detail`: CRITICAL，20 个上游符号/8 条流程。仅增加一行和三级编辑页入口。
- `set_dashboard`: HIGH，8 个上游符号/4 条流程。仅更新新增 label 文本/显隐及 SOC 格式。
- 不修改 GPS motion filter、trip sample、BLE GAP/GATT 或 controller fallback 逻辑。

## Rollback

- UI 可单独回退新增对象和设置行；snapshot/action/NVS 字段不会影响旧显示路径。
- 算法可回退为 `remaining_range_valid=false` 而不影响其他 telemetry。
- 刷写失败时保留构建产物和完整 esptool 错误，不修改桥接端配置。
