# 全工程 RAM 与 Flash 优化执行计划

## 基线与证据

- [ ] 将 `idf.py size`、`size-components`、`size-files` 的文本/JSON 输出保存至任务目录；记录总体基线：Flash Code 948,988 B、Flash Data 270,984 B、IRAM 101,827 B、DRAM 56,172 B、镜像 1,343,715 B。
- [ ] 从 map JSON 导出主要 archive/file 排名；当前已知大户包括 LVGL 核心（250,039 B Flash）、项目 LVGL UI（83,065 B Flash、10,241 B 静态 RAM）和 net80211（144,769 B Flash、9,413 B 静态 RAM）。
- [ ] 记录设备启动、热点、两路中央 BLE、手机外设 BLE 同时运行时的 heap 与任务栈水位，作为动态基线。

## 配置优化（逐组验证）

- [ ] 在修改前用 GitNexus impact 分析相关配置读取/初始化符号；确认每一项无产品路径依赖。
- [ ] 删除 Wi-Fi STA/企业/WPA3/SAE/OWE 配置，保留 SoftAP、WPA2-PSK 与单客户端约束；重新配置并验证热点和 HTTP。
- [ ] 删除未使用 NimBLE 标准服务与 DTM 测试，保留 GAP、中央/外设、扫描/广播、GATT client/server 和 3 连接配额；验证 BMS、控制器和手机三路 BLE。
- [ ] 启用 Newlib nano 格式化，并回归全部 JSON/HTTP、UI 和日志格式化路径。
- [ ] 裁剪未使用 LVGL 软件颜色格式，保留 RGB565、swapped RGB565、ARGB8888、canvas 与 QR code；检查所有屏幕刷新及自定义绘制。
- [ ] 每组完成后执行 `idf.py size` 并记录与上组、基线的变化；若收益为负或功能受损则回滚该组。

## 运行时与代码优化

- [ ] 为应用任务增加受控的 heap 碎片与栈高水位观测；使用已有 heap 日志位置或集中诊断函数，避免重复输出和高频开销。
- [ ] 检查 HTTP 大栈数组、BLE 回调缓冲、LVGL 动态字符串/画布与静态 UI 状态；只处理有可测收益的项，并对每个拟改符号执行 GitNexus impact。
- [ ] 基于水位数据评估 NimBLE host、HTTP、LVGL draw、FreeRTOS timer、lwIP 和 Wi-Fi 栈/缓冲区。仅在压力测试后收缩，并保留安全余量。
- [ ] 对资源常量和重复字符串执行 map 驱动审计；只做不改变 UI 文案、显示或资产生命周期的 const/去重改动。

## 质量门禁

- [ ] 构建：`idf.py build`、`idf.py size`、`idf.py size-components`、`idf.py size-files`。
- [ ] 测试：运行现有协议自测及相关项目测试；对改动组件进行编译和静态检查。
- [ ] 真机：使用固定 LAN RFC2217 流程刷写和监控，验证热点、HTTP、BMS、控制器、手机 BLE 三路并发及至少一次长时内存观测。
- [ ] 完成后运行 Trellis quality check，GitNexus `detect_changes()`，确认变更仅覆盖预期配置、观测和已审核的优化符号。
- [ ] 若学习到新的内存预算或配置约束，更新项目 spec；提交前不覆盖用户已有未提交改动。

## 风险与回滚点

| 变更组 | 主要风险 | 回滚点 |
| --- | --- | --- |
| Wi-Fi 裁剪 | 热点认证或 HTTP 不可用 | 恢复对应 Wi-Fi Kconfig 项并重新配置 |
| NimBLE 裁剪 | 三路连接或重连失败 | 恢复对应 NimBLE 服务/内存项 |
| Newlib nano | 格式化输出截断或不兼容 | 关闭 `CONFIG_NEWLIB_NANO_FORMAT` |
| LVGL 颜色格式裁剪 | canvas/QR/刷新异常 | 恢复所裁剪颜色格式 |
| 栈和缓冲收缩 | 压力下溢出、丢包、碎片 | 恢复前一组数值并保留观测 |
