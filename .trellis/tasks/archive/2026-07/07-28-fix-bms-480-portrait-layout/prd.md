# 修复 BMS 480 分辨率竖屏布局

## Goal

让 320x480 竖屏下的 BMS 首页采用完整、可读的 480 级竖屏仪表布局，而非当前的 240x320 通用回退卡片。

## Confirmed Facts

- `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:916` 的
  `bms_native_landscape_enabled()` 要求 `width >= height`，因此 320x480 不会进入原生 BMS 布局。
- `create_screen()` 在该条件不满足时会创建 `dashboard_viewport(..., true)`，其设计基准为 240x320，未利用 320x480 的额外高度。
- 根目录 `preview/bms-portrait-refined-320x480.png` 是现存的 320x480 BMS 竖屏视觉参考。

## Requirements

1. 在 320x480 竖屏显示器上，BMS 首页使用独立的竖屏布局，完整利用可用高度，并以 `preview/bms-portrait-refined-320x480.png` 为视觉验收目标。
2. 竖屏布局须保留现有 BMS 动态数据：SOC、总压、电流、单体统计、温度、容量/续航与全部保护状态。
3. 480x320 横屏原生 BMS 仪表及更小分辨率的回退布局不得改变。
4. 新增或更新的 UI 预览图须存放在仓库根目录 `preview/`。

## Acceptance Criteria

- [x] 在 320x480 模拟器截图中，BMS 页面具备清晰的 SOC、实时电气、单体电压、温度、容量/续航和告警/保护分区，且无重叠或截断。
- [x] 同一 BMS 快照下，竖屏页面中的所有动态指标会随快照更新。
- [x] 480x320 横屏 BMS 页面保持原有原生布局。
- [x] 模拟器构建通过，并在 `preview/` 更新 320x480 BMS 截图。

## Out Of Scope

- 不改变 BMS 数据协议、Web UI、设置页或页面切换机制。
