# 优化轮播切换卡顿

## Goal

恢复 BMS 首页与速度仪表页之间的轮播拖动和吸附流畅度，避免默认配置在每个滚动事件都强制重绘整张已拉伸的仪表视口。

## Confirmed Facts

- 用户反馈 BMS 显示页与本田火刃仪表页在拉伸后，轮播切换明显卡顿。
- `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:9526` 在每个 `LV_EVENT_SCROLL` 调用 `invalidate_dashboard_viewport()`。
- 当 `CONFIG_ESP_BMS_LVGL_UI_DRAG_FULL_INVALIDATE=y` 时，该函数在 `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:9082` 使整个仪表根视图失效；默认 ESP32 与 ESP32-S3 配置均开启该选项。
- BMS 和 Fireblade 都通过 `dashboard_viewport()` 以固定 320x240 或 240x320 布局再作 `transform_scale` 适配非原生面板尺寸（`components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:779`）。全屏失效会放大这类页面的每帧绘制成本。
- Fireblade 已跳过这条整屏失效路径，但仍保留缩放绘制；因此本任务不把“拉伸”本身当作未经验证的根因，也不重写页面布局。
- 已有诊断脚本和 `dragdiag-no-full-invalidate` 覆盖层可比较整屏失效与 LVGL 原生局部失效路径。
- GitNexus upstream impact：`page_scroll_event_cb` 为 LOW（0 个直接调用者、0 条受影响流程）；`invalidate_dashboard_viewport` 和 `dashboard_viewport` 均为 CRITICAL，分别覆盖 8 和 11 条流程。本任务不修改这两个高风险函数。

## Requirements

- R1: 普通 ESP32、ESP32-S3 与桌面模拟器默认关闭 `ESP_BMS_LVGL_UI_DRAG_FULL_INVALIDATE`，使轮播拖动使用 LVGL 原生局部失效。
- R2: 保留完整 A/B 诊断能力：默认诊断镜像使用局部失效，并提供显式全屏失效覆盖层供回归比较；既有 `--no-full-invalidate` 命令保持兼容。
- R3: 不修改 BMS/Fireblade 的页面结构、缩放策略、页面映射、手势阈值、快照延迟或双缓冲策略。
- R4: 在目标真机上验证 BMS <-> Fireblade 轮播的慢拖、快速甩动、松手吸附和程序化切页均无残影、分块、触摸退化、panic 或 WDT。
- R5: 经典 ESP32 的原生横屏 320x240 S1000RR 速度仪表使用 Flash 中的 RGB565 静态底图，减少拖动期间的矢量绘制；动态速度、状态和文本保持 LVGL 实时绘制。

## Acceptance Criteria

- [x] AC1: 普通与 ESP32-S3 默认配置、Kconfig 默认值及模拟器配置均不再启用 `CONFIG_ESP_BMS_LVGL_UI_DRAG_FULL_INVALIDATE`。
- [x] AC2: 诊断脚本默认构建局部失效镜像；显式全屏失效与兼容的 `--no-full-invalidate` 两种调用都得到正确的配置覆盖层。
- [x] AC3: 构建并运行模拟器无头冒烟测试，覆盖 BMS、Fireblade、横屏和竖屏基本 UI 路径。
- [ ] AC4: 目标硬件上的局部失效镜像反复切换 BMS <-> Fireblade 时无视觉残影或分块，且主观卡顿较当前默认镜像改善。
- [x] AC5: `git diff --check`、相关配置/脚本检查、ESP-IDF 目标构建和 GitNexus `detect_changes` 通过。
- [x] AC6: 经典 ESP32 构建仅增加一张 320x240 RGB565（153,600 B）静态底图；竖屏、ESP32-S3 和模拟器仍使用原有动态绘制路径。

## Out Of Scope

- 将固定坐标仪表重写为原生响应式布局，或消除现有 BMS/Fireblade 的视觉缩放。
- 修改 I80/SPI 时钟、像素格式、LVGL buffer 高度、PSRAM 双缓冲、触摸参数或轮播手势算法。
- 修改 BMS、GPS、控制器、投屏和设置业务逻辑。
- 为其他仪表风格、竖屏或 ESP32-S3 生成额外静态图片。

## Implementation Approval

- 用户于 2026-07-27 审核并批准按本 PRD 实施。

## Notes

- Keep `prd.md` focused on requirements, constraints, and acceptance criteria.
- Lightweight tasks can remain PRD-only.
- For complex tasks, add `design.md` for technical design and `implement.md` for execution planning before `task.py start`.
- The ESP32-S3 image was flashed through RFC2217. The bridge rejected monitor
  negotiation afterward, so AC4 remains a manual on-device visual check.
