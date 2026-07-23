# 启动日志与背光配置分析

## 实机证据

- 设备：ESP32-D0WD-V3 rev 3.1，MAC `20:e7:c8:5f:ab:a4`，COM3。
- 当前应用从 OTA 分区 `0x10000` 成功加载；应用为 ESP-IDF v6.0.2。
- 日志确认 display bus、LVGL adapter 与触摸已启动，但约 7 秒后连续报告
  `task_wdt`，CPU0 运行 `esp_timer` 且 `IDLE0` 未及时执行。

## 背光根因证据

- `firmware/catalog/board/esp32-wroom-32e-legacy.env:15` 声明 `TFT_BACKLIGHT=21`。
- `firmware-builds/esp32-wroom-32e-legacy/firmware.env:18-21` 没有
  `GPIO_TFT_BACKLIGHT`。
- `firmware-builds/esp32-wroom-32e-legacy/generated/esp_bms_profile_hardware.h:22`
  至 `:31` 生成 `.pin_backlight = GPIO_NUM_NC`。
- `components/esp_bms_lvgl_bridge/esp_bms_lvgl_bridge.c:545` 至 `:550`、`:580` 至
  `:584`、`:598` 至 `:604` 对 NC 背光全部成功返回而不写 GPIO。
- `scripts/generate-hardware-config.py:154` 将背光视为可选；普通 SPI 角色收集遗漏该字段，
  位于 `start.sh:631` 至 `:634` 和 `start.ps1:539` 至 `:540`。

## 结论

黑屏的直接原因是 profile 未把 catalog 中的 GPIO 21 传给显示桥接层。看门狗告警是必须在修复后
继续验证的独立启动时序风险，不能通过关闭看门狗掩盖。
