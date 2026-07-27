# 优化轮播切页黑场过渡

## Goal

将 BMS 首页、速度仪表和投屏页的横向轮播从“完整页面跟手移动”改为“黑场中的页面标题跟手交接”，降低拖动期间复杂控件的绘制压力，并保持既有的切页方向、阈值和松手吸附结果。

## Confirmed Facts

- 主轮播由 `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:10624` 的 `page_scroll_event_cb()` 驱动，当前会在拖动时移动包含复杂仪表控件的 `s_ui.pages` 子页面。
- 上一轮优化已默认关闭拖动期间的全屏仪表失效；这次不修改该配置、SPI/I80 参数、LVGL 缓冲或仪表布局。
- 页面容器和 BMS、仪表、投屏页在 `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:10753` 的 `create_screen()` 中创建。
- GitNexus 影响分析：`page_scroll_event_cb()` 为 LOW（0 个直接调用方、0 条流程）；`create_screen()` 为 CRITICAL（2 个直接调用方、10 条流程、4 个模块）。

## Requirements

- R1: 轮播开始后，屏幕显示不透明纯黑过渡层；原页面及相邻页面的复杂控件在拖动期间不参与绘制。
- R2: 过渡层只显示当前页和相邻目标页的居中标题，并按当前拖动距离反向/正向跟手移动；边界无法切换时只保留当前标题。
- R3: 松手吸附、切页阈值、页面顺序和原生手势/API 的既有目标页语义保持不变；目标页稳定后恢复完整页面绘制并应用延迟的数据快照。
- R4: 屏幕重建、进入设置或其他提前结束的轮播路径不得留下隐藏页面或黑色过渡层。
- R5: 不新增依赖、配置项、位图截图缓存或额外帧缓冲。

## Acceptance Criteria

- [x] AC1: 常规触摸横拖时，复杂页面内容在拖动期间被黑场遮住，两个标题随手势位置交接。
- [x] AC2: 取消拖动恢复原页；超过既有阈值的拖动恢复对应目标页；首尾边界不出现空白目标标题。
- [x] AC3: 原生手势和 `esp_bms_lvgl_ui_set_page(..., true)` 保持正确的页面切换与最终完整渲染。
- [x] AC4: 无头 LVGL 模拟器全量特性矩阵通过，新增的过渡态检查覆盖显示、标题移动和结束后的恢复。
- [x] AC5: `git diff --check`、目标 ESP-IDF 构建和 GitNexus `detect_changes` 通过。

## Out Of Scope

- 重新实现原始触摸输入、轮播滚动算法、页面结构或页面截图缓存。
- 更改显示总线速度、像素格式、缓冲区数量、触摸阈值或动态仪表绘制策略。

## Implementation Approval

- 用户于 2026-07-28 要求“创建 Trellis 任务，并执行”。

## Notes

- Keep `prd.md` focused on requirements, constraints, and acceptance criteria.
- Lightweight tasks can remain PRD-only.
- For complex tasks, add `design.md` for technical design and `implement.md` for execution planning before `task.py start`.
- 2026-07-28: `./scripts/run-lvgl-simulator.sh --headless` passed its full
  matrix, including native carousel stress (424 deferred snapshots); a
  profile-isolated ESP32 build produced
  `/tmp/esp32-bms-gps-carousel-output.avS1e4/esp32all/esp32all-flash.bin`.
- 2026-07-28: RFC2217 bridge `192.168.2.10:4000` timed out, so this task has
  no hardware-drag visual confirmation yet.
