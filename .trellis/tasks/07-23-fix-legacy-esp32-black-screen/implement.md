# 实施计划

1. 读取 Bash、PowerShell 配置器的普通 SPI GPIO 角色收集代码及现有 selftest fixture。
2. 对待修改符号进行影响分析，确认 profile 生成、硬件头生成和 configurator selftest 的调用面。
3. 在两个配置器中有条件地收集 `TFT_BACKLIGHT`，并添加 legacy 生成断言。
4. 运行 configurator selftest，重新生成 `esp32-wroom-32e-legacy` profile，并构建固件。
5. 烧录 bootloader、分区表、OTA 数据和应用镜像；采集 15 秒 COM3 冷启动日志。
6. 检查首屏初始化完成日志、背光 GPIO 配置和看门狗状态；任何失败均返回对应步骤修正。
