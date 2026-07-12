# Design

## Boundaries

- `esp_bms_lvgl_ui`：roller 导航与选中提示、BMS 电流的用户侧符号、控制器成功 toast 收尾。
- `esp_bms_idf_runtime`：控制器 BLE 数据通道的订阅和通知解析；保留页面节流仅用于主动 gather。

## UI Contracts

- roller 的滚动手势会保留现有左右返回手势，但对其滚动容器禁用导航标题隐藏，防止滚轮的纵向拖动改变导航可见性。
- 给 roller 的 `LV_PART_SELECTED` 添加上、下边框作为固定选择窗口；不增加覆盖对象、堆分配或新的状态。
- `set_dashboard()` 用局部 `display_current_deci_amps = -snapshot->current_deci_amps` 进行格式化和 charging 判定。最小有符号值采用安全的饱和处理，避免 `int16_t` 取反溢出。
- 快速连接 toast 在 controller online 从 false 变 true 时和 BMS 一样收尾。online 仍由有效连接句柄投影，符合“已连接”的用户预期。

## Controller BLE Data Flow

```text
GAP connect -> conn_handle / online snapshot / audio event
           -> GATT discovery -> CCCD subscribe (always enabled once discovered)
           -> notify frame parsing (independent of active UI page)
           -> cached controller snapshot -> controller carousel labels

stable UI page -> active_data_source -> only controls 2 s gather keepalive
```

这样既让被动通知持续缓存，又不把主动 gather 从控制器页扩展到所有页面。

## Compatibility and Rollback

- Snapshot 字段、action、NVS 键和协议解析均不改变。
- 不引入新字体字符；定位线使用现有色彩常量。
- 运行时改动集中在订阅/解析条件，若有 BLE 回归可独立回滚，不影响控制器设置和 dashboard 布局。
