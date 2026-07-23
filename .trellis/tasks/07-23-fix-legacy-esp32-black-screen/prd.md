# 修复旧 ESP32 启动黑屏

## Goal

让 `esp32-wroom-32e-legacy` profile 生成正确的 TFT 背光 GPIO 配置，使已烧录的
ESP32-WROOM-32E 在启动后能点亮首帧，并且不再出现启动阶段 CPU0 IDLE 看门狗告警。

## Confirmed Facts

- 2026-07-23 的实机日志确认 ESP32-D0WD-V3 从 `0x10000` 成功加载
  `esp32_bms_gps_idf`，显示面板、LVGL 和触摸均完成初始化。
- 同一日志在 LVGL 初始化后约 7 秒报告 `task_wdt`，受影响任务为 `IDLE0`，运行任务为
  `esp_timer`；启动代码尚未打印 `display path initialized`。
- 板卡目录 `firmware/catalog/board/esp32-wroom-32e-legacy.env` 声明
  `TFT_BACKLIGHT=21`。
- 现有 legacy profile 生成文件遗漏 `GPIO_TFT_BACKLIGHT`，生成硬件头将
  `.pin_backlight` 设置为 `GPIO_NUM_NC`。
- `esp_bms_lvgl_bridge` 对 `GPIO_NUM_NC` 将背光配置、亮度设置和最终点亮全部作为成功的
  空操作处理；这会让程序继续运行但 TFT 保持黑屏。

## Requirements

1. 配置器在普通 SPI TFT profile 中，若板卡目录声明 `TFT_BACKLIGHT`，必须将其写入 profile
   环境文件，并令生成硬件头的 `.pin_backlight` 使用该 GPIO。
2. 未声明 `TFT_BACKLIGHT` 的板卡必须继续生成 `GPIO_NUM_NC`，不得引入硬编码回退 GPIO。
3. Bash 与 PowerShell 配置器必须采用一致的 GPIO 角色收集规则。
4. 配置器自测必须覆盖 legacy profile 的环境文件和生成硬件头，并防止该字段再次遗漏。
5. 修复后的 legacy firmware 必须能构建、烧录到 COM3，并在启动日志中完成首屏初始化；
   15 秒观测窗口内不得再出现 `task_wdt`。

## Acceptance Criteria

- [ ] legacy profile 包含 `GPIO_TFT_BACKLIGHT=21`。
- [ ] legacy 生成头包含 `.pin_backlight = (gpio_num_t)21`。
- [ ] 未配置背光的 profile 仍可生成且保持 `.pin_backlight = GPIO_NUM_NC`。
- [ ] `tests/configurator_selftest.sh` 通过。
- [ ] legacy ESP-IDF 构建成功，固件烧录哈希校验成功。
- [ ] COM3 冷启动日志显示首屏路径已完成，且 15 秒内无 CPU0 `task_wdt`。

## Out of Scope

- 不修改显示桥接层以硬编码 GPIO 21。
- 不改变已声明的屏幕型号、SPI 引脚、分区表或 NVS 用户设置。
