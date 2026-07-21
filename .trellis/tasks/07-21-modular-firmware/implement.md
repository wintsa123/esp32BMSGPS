# 实施清单

1. 建立核心模块契约和 profile 驱动的组件/注册表生成；保持 legacy 路径构建。
2. 将 GPS UART、PPS、解析、诊断和启动探测迁移至 `esp_bms_gps`；GPS off 时
   从 `main` 组件闭包、archive、map 和 ELF 中消失。
3. 将 BMS BLE 迁移至 `esp_bms_bms_ble`，再将控制器 BLE 迁移至
   `esp_bms_controller_ble`；分别验证 enabled/disabled。
4. 抽取 Wi-Fi 与 Setup AP/HTTP 基础设施到 `esp_bms_network`，将 OTA 与投屏
   路由分别迁至 `esp_bms_ota` 和 `esp_bms_cast`，同时处理嵌入 Web 资源。
5. 使 TFT/Web/API 设置贡献走注册表，移除 runtime 中所有已经迁移的代码。
6. 将 catalog `COMPONENTS` 换为真实闭包，增加模块 on/off 构建和 map/symbol/
   resource 断言。

每步：先运行 GitNexus upstream impact，随后执行针对性自检和 ESP-IDF build；
完成时执行全量自检、默认 legacy build、`git diff --check`、GitNexus
`detect-changes` 和适用的 RFC2217 刷写。
