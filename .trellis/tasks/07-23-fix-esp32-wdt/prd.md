# 修复 ESP32 看门狗复位

## Goal

消除 ESP32-WROOM-32E legacy 启动期间的 CPU0 task watchdog 告警，确保首屏和启动动画不会让 CPU0 的 IDLE 任务长期得不到调度。

## Confirmed Facts

- 目标设备是 ESP32-D0WD-V3（`esp32-wroom-32e-legacy`），ESP-IDF v6.0.2，4 MB flash、无 PSRAM，SPI ST7789 显示屏和 XPT2046 触摸，背光 GPIO21。
- 实机启动在 LVGL 初始化约 7 秒后报告 task WDT：受影响任务为 `IDLE0`，当时 CPU0 运行任务为 `esp_timer`；task WDT 超时为 5 秒。
- `main/idf_main.c` 的主循环和启动延时均会调用 `vTaskDelay`，不能仅凭任务名将其认定为唯一饿死源。
- `components/esp_bms_lvgl_ui/bluetoothon.c` 和 `wlanJZ.c` 的自定义 LVGL 字体此前将 `.fallback` 指向字体自身。缺字形查找会形成无终止回退，属于可使 LVGL 定时处理长期占用 CPU 的高概率根因；工作区已有删除这两处自引用的未提交候选修复。
- 背光 GPIO 漏传已由相关 legacy 任务修正，当前生成硬件头含 `.pin_backlight = (gpio_num_t)21`；该问题与 WDT 验证相关，但不是通过关闭 WDT 处理。

## Requirements

1. 自定义字体不得将 fallback 指向自身；保持现有字体字形和非回退显示行为。
2. 保留 task WDT 和 CPU0 IDLE 监测，不得通过增大超时或关闭看门狗掩盖故障。
3. 必须对 legacy profile 进行构建，并通过串口冷启动验证首屏、启动动画和稳定观测窗口。
4. 若移除自引用后仍出现 WDT，必须以最小化、可移除的启动/定时路径诊断数据继续定位，不进行推测性重构。

## Acceptance Criteria

- [x] `bluetoothon` 和 `wlanJZ` 字体定义不再含自引用 fallback。
- [x] 不存在其他同类自定义字体自引用 fallback（同时修复 `hotspoton`）。
- [x] legacy profile 成功构建。
- [x] 冷启动日志显示首屏路径完成，启动后观察窗口内不再报告 `task_wdt`、`IDLE0` 饿死、panic 或重启。
- [x] 完成 2 分钟无交互稳定运行，期间无 `task_wdt`、panic 或重启。
- [x] 仍启用 task WDT 及 CPU0/CPU1 idle 任务检查。

## Out of Scope

- 不修改 task watchdog 超时、关闭 CPU0 idle 检测或关闭 watchdog。
- 不改变显示针脚、分区表、NVS 用户设置或不相关的 UI 页面逻辑。
