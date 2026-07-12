# 全工程 RAM 与 Flash 优化设计

## 范围与不变量

- 硬件为无 PSRAM 的 ESP32 Rev.3；双 OTA 槽和 `otadata` 保持原样。
- Wi-Fi 仅运行 WPA2-PSK Setup AP，最多一台客户端；不支持 STA、企业 Wi-Fi、WPA3/SAE/OWE。
- BLE 同时维持 BMS、控制器和手机三路连接：保留 NimBLE 中央、外设、观察者、广播者、GATT 客户端/服务端和 3 个连接控制器配额。
- GAP 继续提供蓝牙名称与广播；移除未初始化、未被产品使用的可选标准服务。
- 不使用 `CONFIG_ESP_SYSTEM_ALLOW_IRAM_OVERFLOW`，也不以关闭断言、堆栈检查或错误处理来制造表面收益。

## 优化边界

### 1. 配置裁剪

以 `sdkconfig.defaults` 为真源，并在重新配置后核对生成的 `sdkconfig`。

- 关闭 Wi-Fi STA 专属能力：企业 Wi-Fi、WPA3 SAE/SAE-PK、SoftAP SAE、OWE STA 及不再需要的 STA 省电选项。
- 关闭 NimBLE 的 PROX、ANS、CTS、HTP、IPSS、TPS、IAS、LLS、SPS、HR、BAS、DIS 与 DTM 测试；GAP 保留。
- 启用 Newlib nano 格式化前先以全仓 `%f/%e/%g/%a` 检索确认；当前未发现浮点格式化。启用后以 HTTP JSON、日志和 UI 格式化回归确认。
- LVGL 保留 RGB565、RGB565 swapped 与代码明确使用的 ARGB8888 canvas；关闭其余未使用的软件绘制颜色格式。保持 QR code/canvas 和显示 SPI 刷新可用。
- Wi-Fi/NimBLE 缓冲区、LVGL 绘制线程栈和 24 KiB layer buffer 不预先缩小：先通过运行时水位/压力测试得到证据，再以小步修改和回归验证决定。

### 2. 运行时内存观测

统一输出内部 8-bit heap 的：当前空闲、历史最低空闲、最大连续块，以及碎片指标 `1 - largest/free`（free 为零时跳过）。PSRAM 仅作为可选诊断项，不作为预算来源。

对可获得句柄的应用任务保存或查询 `TaskHandle_t`，周期性报告 `uxTaskGetStackHighWaterMark()`；单位明确为 words，并换算字节。NimBLE host 任务和 LVGL/系统任务只在能可靠获得句柄时报告，不能可靠观测时不猜测其栈余量。

日志需限频或在状态变化时输出，避免高频诊断本身造成串口阻塞或影响堆栈。

### 3. 代码与资源审计

- 优先处理 map 文件显示的实际大户，而不是凭经验迁移数据。
- 仅将真正只读的数据改为 `const`；不改变 LVGL 对可变画布、对象或缓冲区的所有权。
- 动态分配只在长期保留、大小不定或频繁分配点确有碎片证据时改为复用/池化；本轮不进行无证据的大范围重构。
- 局部大数组在修改前结合调用频率、任务栈水位和生命周期决定：优先缩小、复用或静态化；避免以静态化将问题转移到 DRAM。

## 验证与回滚

- 每组配置变更单独构建，比较 `.map` 的总体、archive 和 file 报告；出现 BLE/Wi-Fi/LVGL 功能回归即回滚该组。
- 设备验证覆盖：启动、Setup AP/HTTP 页面、BMS 连接、控制器连接、手机 BLE 连接、三路并发与长时运行观察。
- 配置改动可通过恢复对应 `sdkconfig.defaults` 项并重新配置回滚；代码观测改动独立可回滚，不改变业务状态机。
