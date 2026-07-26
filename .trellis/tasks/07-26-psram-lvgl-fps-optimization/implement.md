# 实施计划

1. 将 LVGL bridge 的双缓冲 Kconfig 默认值设为 `y if SPIRAM`，并让 S3 默认配置明确启用。
2. 调整 `esp_bms_lvgl_bridge_init`：分别判断一块与两块 PSRAM buffer 的可用性，向 adapter 传递实际可用的双缓冲状态，并扩展启动日志。
3. 在现有配置器自测中断言 S3 与无 PSRAM 默认值，避免配置回归。
4. 运行配置器自测、S3 profile 构建、差异空白检查和 GitNexus 变更检测；尝试使用项目 RFC2217 流程完成匹配硬件的启动/拖动验证。

## 风险与验证

- 修改符号 `esp_bms_lvgl_bridge_init` 的 GitNexus 上游影响为低：1 个直接调用方 `app_main`，1 个启动流程。
- 仅改 adapter profile 参数，不改面板传输、LVGL 对象访问或 UI 绘制代码。
- 若 S3 编译或启动失败，先恢复单缓冲 S3 默认值；无 PSRAM 诊断覆盖不应受影响。

## 验证记录

- S3 隔离 profile 构建完成，最终 `sdkconfig` 同时启用 `CONFIG_SPIRAM=y` 和
  `CONFIG_ESP_BMS_LVGL_BRIDGE_DOUBLE_BUFFER=y`；生成镜像为 0x175520。
- 已通过 RFC2217 烧录匹配的 ESP32-S3，启动日志确认 8 MB PSRAM，且 bridge
  输出 `requested_double=yes active_double=yes psram=yes`。
- 当前工作树重新构建的 S3 镜像也已通过 RFC2217 写入。esptool 识别
  `ac:a7:04:f3:9b:68` 为内置 8 MB PSRAM 的 ESP32-S3，应用分区起始 4 KiB
  已读回并与本地镜像一致，随后完成硬复位。
- 旧 ESP32 隔离 profile 在 `CONFIG_SPIRAM` 关闭且显式双缓冲开启时构建完成，
  证明诊断分支仍可编译；镜像为 1,522,035 B，DRAM 使用 49.48%。
- `git diff --check`、相关 shell 语法检查和完整
  `tests/configurator_selftest.sh` 通过。自测的交互取消输入已同步到当前菜单：
  默认选择触摸、模块、仪表和 profile 名称后以 `n` 取消，且仍断言不生成配置。
