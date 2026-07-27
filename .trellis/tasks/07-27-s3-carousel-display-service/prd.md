# S3 轮播性能与显示任务重构

## Goal

将 `esp32s3-n16r8-st7796u-gt1151` 上 BMS 与 Honda Fireblade 仪表间的横向轮播，
从缩放的大型 LVGL 对象树重绘，改为原生 480x320 布局、可复用静态 RGB565 底图和
单一显示服务调度。目标是慢拖、甩动与吸附的 P95 帧时间不超过 16.7 ms，同时保持
当前仪表字段、交互和全屏视觉意图。

## Confirmed Facts

- 目标硬件固定为 ESP32-S3、16 MB Flash、8 MB PSRAM、16 位 I80 ST7796U 和 GT1151，
  目录定义在 `firmware/catalog/board/esp32s3-n16r8-st7796u-gt1151.env`。
- 当前 ST7796U 目录的像素时钟为 10 MHz，逻辑横屏为 480x320；S3 默认 LVGL 双缓冲
  为 480x40 两块。
- `esp_bms_lvgl_bridge_init()` 初始化 `esp_lv_adapter`，而
  `esp_bms_lvgl_bridge_start()` 启动其独立 LVGL worker；应用层目前通过 adapter 锁，
  从 `app_main` 直接调用 bridge、UI 和 LVGL 相关操作。
- `esp_bms_lvgl_ui_update()` 由 `app_main` 和模拟器直接调用。BMS 页面和 Fireblade
  页面均使用 `dashboard_viewport()` 的运行时 `transform_scale`。
- 现有 UI 已在拖动/吸附期间合并最新遥测快照，并在完成后刷新；新服务须保留该行为。
- 工作区已有其它任务的未提交改动，尤其是局部失效诊断与 S3 设置字号；本任务不得
  覆盖或回退它们。

## User Decisions

- 用户要求彻底重构而非仅关闭全屏失效或微调缓冲，并接受较高改动风险；发生问题时继续
  修复，而不是退回到旧架构。
- 20 MHz I80 时钟只适用于该 S3/ST7796U 目录。出现花屏、触摸异常或冷启动失败时，
  保留渲染优化并将时钟回退至 10 MHz。

## Requirements

- R1: BMS 和 Fireblade 不再经由 `dashboard_viewport()` 对 320x240/240x320 进行运行时
  缩放；横屏为原生 480x320 布局，竖屏继续使用其原生页面尺寸与旋转重建路径。
- R2: 两个仪表的静态部分各建立一块固定 480x320 RGB565 PSRAM 底图（约 307 KiB/块），
  初始化或重建时生成一次。运行时仅由 LVGL 更新动态数值、指针和状态覆盖层。
- R3: S3 使用两个 480x120 RGB565 绘制缓冲，所有绘制缓冲与仪表底图只在初始化或显示
  重建时分配并复用；手势与遥测更新路径不得申请或释放内存。
- R4: 新增显示服务，作为 firmware 侧唯一的 LVGL 所有者。它初始化 bridge 和 UI、
  串行调用 `lv_timer_handler()`，且不启动 `esp_lv_adapter` 自带 worker。
- R5: 应用层仅通过显示服务的 start、publish snapshot、submit command 和 take action
  event 接口与显示交互；运行时与 `app_main` 不得再直接调用 `lv_*`、UI 函数或 LVGL 锁。
- R6: 快照使用容量为 1 的静态覆盖队列；UI 动作使用有界静态队列。亮度、旋转、触摸校准、
  页面切换和投屏块写入均由显示服务串行执行。
- R7: 记录一次手势的 RENDER、FLUSH、DMA 等待、失效面积、队列延迟、PSRAM/DMA 最小
  空闲内存，用于区分渲染瓶颈与面板带宽瓶颈。
- R8: S3/ST7796U 目录像素时钟提高到 20 MHz，且其他显示目录保持原值。

## Acceptance Criteria

- [ ] AC1: S3 配置自测断言双 480x120 缓冲、20 MHz 仅作用于 ST7796U 目录、BMS 和
  Fireblade 不再设置 transform scale、两个静态缓存容量正确。
- [ ] AC2: 模拟器覆盖原生 480x320 BMS/Fireblade 横向拖动、吸附、旋转重建和高频快照；
  断言无对象泄漏、无运行时堆增长，且拖动结束后显示最新快照。
- [ ] AC3: `app_main` 与运行时不直接调用 UI/bridge/LVGL 锁，adapter worker 未启动，
  所有显示命令经显示服务处理。
- [ ] AC4: 构建 S3 profile，日志可输出手势性能统计；在真机慢拖与快速甩动各 20 次时，
  P95 帧时间不超过 16.7 ms，且无残影、分块、WDT、panic 或触摸退化。
- [ ] AC5: 20 MHz 下至少 3 次冷启动、全区域触摸和长时轮播通过；失败时记录证据、回退
  10 MHz 并保留其它优化。
- [ ] AC6: `git diff --check`、配置自测、模拟器、S3 ESP-IDF 构建和 GitNexus
  `detect-changes` 通过。

## Out Of Scope

- 不修改 BMS、GPS、控制器、投屏协议或设置业务含义；只改变它们到显示层的线程边界。
- 不提高其它 I80/SPI 显示目录的像素时钟。

## Implementation Approval

用户提供了本任务的重构方案，并在 2026-07-27 明确授权“新建一个，并且实现”，随后确认
接受彻底重构的较高风险。该授权覆盖本 PRD、设计和实施清单。
