# 修复模拟器鼠标轮播手势

## Goal

让交互式 LVGL 模拟器支持鼠标拖动轮播和黑场跟手，同时保留无头原生手势测试。

## Confirmed Facts

- `simulator/main.c` 默认打开 GPS 页面并选用 Fireblade 风格，因此窗口初始只显示本田仪表。
- SDL 已创建 `lv_sdl_mouse_create()` 输入设备，但模拟器以
  `native_gestures_supported=true` 初始化 UI；该模式会禁用 `s_ui.pages` 的
  LVGL 原生滚动。
- 当前 SDL 事件桥只处理窗口关闭与键盘按键，没有将鼠标拖动转换成原生手势。
- 无头矩阵显式调用 `esp_bms_lvgl_ui_simulator_native_gesture_smoke()`，它要求原生手势模式保持开启。

## Requirements

- R1: 非无头的 SDL 窗口以 LVGL 指针滚动模式运行（`native_gestures_supported=false`），使现有鼠标输入可直接驱动横向轮播及黑场跟手效果。
- R2: `--headless` 保持设备端原生手势模式（`native_gestures_supported=true`），继续覆盖原生手势分发与既有功能矩阵。
- R3: 交互式默认页面改为 BMS；仪表风格默认值不变，用户向右拖动即可看到 Fireblade 仪表。
- R4: 终端帮助明确鼠标拖动与键盘页面选择的用途。

## Acceptance Criteria

- [ ] AC1: 非无头启动后，鼠标横向拖动可在 BMS、仪表、投屏页之间切换；拖动期间使用生产轮播的黑色标题占位页。
- [x] AC2: 非无头默认首屏为 BMS，拖动后可进入 Fireblade 仪表；键盘 `1` 至 `4` 仍可选择页面。
- [x] AC3: 横竖屏 `--headless` 模拟器均通过，包含原生手势与黑场轮播回归。
- [x] AC4: `git diff --check` 和 GitNexus `detect-changes` 只报告模拟器预期流程。

## Out Of Scope

- 修改设备端的原生触摸手势、轮播阈值或黑场实现。
- 为鼠标事件新增一套重复的原生手势识别器。
