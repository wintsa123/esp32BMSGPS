# ESP32 LVGL 双核帧率优化

## Goal

在双核 ESP32 目标上启用 LVGL 9.5 软件绘制并行能力，并把 LVGL worker 放在 Core 1，降低 UI 渲染与无线协议栈竞争造成的滑页卡顿。

## Confirmed Facts

- 工程锁定 `lvgl/lvgl 9.5.0` 与 `esp_lvgl_adapter`，桥接层通过适配器维护 LVGL 全局锁。
- 默认配置启用 FreeRTOS，但当前本地 `sdkconfig` 是旧生成文件，仍显示 `LV_OS_NONE`、单绘制单元和 160 MHz，不能作为新构建配置。
- LVGL 适配器默认 worker 不绑核；双核芯片上 Core 0 通常承载 Wi-Fi/BLE。
- ESP32 C3 为单核，不能启用双绘制单元或固定到 Core 1。
- 旧 ESP32 ST7789 使用 40 MHz SPI、无 PSRAM；双缓冲和取消全屏失效已有独立实机诊断开关，不能作为无条件默认值。

## Requirements

- R1: 双核目标的默认 SDK 配置将 `LV_DRAW_SW_DRAW_UNIT_CNT` 设为 2，并保持 `LV_OS_FREERTOS`。
- R2: ESP32-C3 默认配置明确保持一个软件绘制单元。
- R3: LVGL adapter worker 在双核目标固定到 Core 1，在单核目标固定到 Core 0。
- R4: 不改变 LVGL 对象访问模型；所有 `lv_*` UI 调用继续由现有 bridge lock 串行化。
- R5: 保留现有双缓冲、全屏失效 A/B 诊断路径，不在无 PSRAM 旧 ESP32 默认打开它们。

## Acceptance Criteria

- [x] 双核默认配置包含 `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=2`，C3 覆盖为 1。
- [x] 适配器配置在编译期正确选择 worker 核心，不引入 C3 的非法 Core 1 任务。
- [x] ESP32 与 ESP32-S3 profile 构建通过；C3 默认配置生成验证通过（暂无 build-ready C3 板）。
- [x] LVGL bridge profile 构建与 UI 模拟器 headless 自测通过。

## Out Of Scope

- 不重写 `esp_bms_lvgl_ui_update` 或拆分多个 UI 线程。
- 不修改显示协议时钟、不默认启用双缓冲、不删除全屏失效保护。
- 不承诺超出 40 MHz SPI 全屏传输物理上限的帧率。
