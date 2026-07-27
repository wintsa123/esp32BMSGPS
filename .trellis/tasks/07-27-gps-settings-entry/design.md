# 设计：GPS 一级设置入口

## Boundary

仅在 `esp_bms_lvgl_ui`、host simulator 和 `preview/` 修改。运行时继续发布既有 snapshot；UI 只消费既有字段。

## UI Shape

设置详情枚举增加 `SETTINGS_DETAIL_GPS`，并增加专属 `settings_gps_view_t`：根页、速度单位列表、速度来源列表。控制器继续使用 `settings_controller_view_t`。

根列表按宏裁剪：

- `ESP_BMS_FEATURE_GPS=1`：显示“GPS”。
- `ESP_BMS_FEATURE_CONTROLLER=1`：显示“控制器”。
- 两者都关闭：都不显示。

GPS 详情用已有列表卡片和行组件渲染状态、定位、本地时间、速度单位；仅双功能构建追加速度来源行。控制器详情删除 GPS-only 兼容分支，只保留控制器功能。

## State And Refresh

`settings_navigate_back` 依据 `SETTINGS_DETAIL_GPS` 与 `settings_gps_view_t` 回到 GPS 根页。GPS 选择器打开时只写入 GPS 子视图状态。快照更新通过一个 GPS 专用比较函数检查模块状态、`GPS_FIX_VALID`、本地时间、速度单位和（双功能时）速度来源；只在 GPS 详情处于可重绘的子视图时调用 GPS 对应渲染函数。

## Compatibility

所有新 UI 条目和 GPS 测试钩子由 `ESP_BMS_FEATURE_GPS` 或 `ESP_BMS_LVGL_UI_SIMULATOR` 保护。没有持久化迁移或动作新增。现有 `SET_SPEED_SOURCE` 动作和离线禁用逻辑直接复用。

## Validation And Rollback

模拟器编译参数显式传入 GPS、控制器功能宏，运行 GPS+控制器、仅 GPS、无 GPS 组合。任何导航或宏裁剪回归可通过回退该 UI、模拟器和预览范围的变更恢复，不影响已保存配置。
