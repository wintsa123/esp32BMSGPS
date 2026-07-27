# GPS 一级设置入口

## Goal

在设备设置根页中为已编译的 GPS 提供独立一级入口，使用户可查看 GPS 模块、定位和本地时间状态，并在该页调整现有速度单位和可用的速度来源；控制器设置保持独立。

## Confirmed Facts

- 设置 UI 当前把 GPS 与控制器合并在“速度仪表”中，`settings_show_controller_detail` 同时承担控制器与 GPS-only 构建。
- 运行时快照已提供 `gps_module_state`、`GPS_FIX_VALID`、本地日期/时间、速度单位和速度来源；不需要扩展运行时、NVS、动作协议或 Web API。
- 现有速度来源选择器在 GPS 不可用时将 GPS 选项显示为不可选；功能宏已经可在 UI 中编译期裁剪。
- GitNexus 对 `settings_show_controller_detail` 的上游影响为 CRITICAL：5 个直接调用方、14 条受影响流程、4 个模块。实现必须覆盖返回导航、快照刷新和模拟器流程。

## Requirements

1. 当 `ESP_BMS_FEATURE_GPS=1` 时，设置根页显示一级“GPS”入口；GPS 模块未检测到时入口仍可打开并显示状态。
2. 当 `ESP_BMS_FEATURE_GPS=0` 时，根页和详情页都不得保留 GPS 入口；当仅控制器编译时，保留控制器一级入口及其现有设置。
3. GPS 详情展示 ASCII 状态值：模块状态 `READY`、`CHECK` 或 `OFFLINE`，定位状态 `FIX`，以及快照中的本地 `TIME`。
4. GPS 详情复用速度单位选择器；速度来源选择仅在 GPS 和控制器均编译时显示，且 GPS 离线时 GPS 选项不可选。
5. GPS 的单位/来源二级选择页返回按钮、边缘返回和快照刷新必须回到 GPS 详情；控制器的对应状态不能被复用。
6. 当模块状态、定位状态或本地时间变化时，只在当前 GPS 设置详情重绘；不得影响控制器页或滚轮编辑器状态。
7. 添加仅受 `ESP_BMS_LVGL_UI_SIMULATOR` 保护的 GPS 设置测试钩子，并将模拟器的 GPS/控制器宏改为独立可配置。
8. 在 `preview/` 更新设置首页和 GPS 详情的横、竖屏预览及其生成脚本。

## Out Of Scope

- NMEA 解析、GPS 运行时快照、NVS 格式、动作协议、Web UI 和固件公共接口。
- 新字体或 TFT 中文状态文字。
- 控制器功能和速度数据源策略的语义调整。

## Acceptance Criteria

- [ ] GPS+控制器构建显示独立的“GPS”和控制器入口；GPS 页可打开、单位与来源选择页均能返回 GPS 页，并能在快照更新后保持该页。
- [ ] 仅 GPS 构建显示 GPS 入口，不显示控制器入口或速度来源选择。
- [ ] 无 GPS 构建不显示 GPS 入口，GPS 模拟器钩子明确拒绝打开；仅控制器构建仍保留控制器设置。
- [ ] GPS 状态、FIX 和 TIME 的变化在 GPS 详情可见，GPS 离线时来源选择器中的 GPS 不能提交。
- [ ] 横、竖屏 headless 模拟器和 `./scripts/run-host-selftests.sh` 通过；ESP-IDF 构建通过。
- [ ] 预览图位于根目录 `preview/`，提交前 GitNexus `detect-changes` 仅报告预期 UI、模拟器和预览影响。
