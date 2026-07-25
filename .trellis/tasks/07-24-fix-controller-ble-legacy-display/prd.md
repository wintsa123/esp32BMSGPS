# 修复控制器蓝牙与旧 ESP32 显示兼容

## Goal

让 BMS 与 FarDriver 控制器在同一固件中可靠复用唯一的 NimBLE 扫描器，使任一设置页的扫描都能展示自己的候选列表；同时恢复旧 ESP32 的 2.8 英寸 ILI9341 可选板型，并消除当前配置矩阵产生的编译告警。

## Confirmed Facts

- NimBLE 的发现扫描只有一个全局回调。BMS 与控制器组件各自注册回调，并在正在扫描时取消对方扫描以交接所有权：`esp_bms_bms_ble.c:833` 与 `esp_bms_controller_ble.c:593`。
- 交接请求仅用 `BMS_SCAN_REQUESTED` / `CONTROLLER_SCAN_REQUESTED` 标记延后启动；扫描完成回调没有确定性地启动已等待的另一方。运行时 tick 顺序为 BMS 后控制器：`esp_bms_idf_runtime.c:3582`。
- 控制器列表从 `START_CONTROLLER_BIND` action 进入，BMS 列表从 `START_BMS_BIND` action 进入；两者的候选数据和 snapshot 字段已分离。
- `firmware/catalog/display/ili9341-2p8-spi.env` 仍声明了 2.8 英寸 ILI9341 SPI 屏，且支持 ESP32；现有旧 ESP32 配置器回归没有覆盖它。
- 当构建档案没有启用任一 I2C 触摸驱动或 I80 面板驱动时，`esp_bms_lvgl_bridge.c:884` 的 `touch_config` 和 `:1024` 的 `panel_config` 仍会被声明，导致 `-Wunused-variable` 告警。
- 配置器本地构建此前只将应用镜像复制到 `output/<profile>/<profile>.bin`，随后删除 ESP-IDF 构建目录，导致在线烧录缺少同次构建的 bootloader、分区表和 OTA 初始数据；该应用镜像若误写入 `0x1000` 会破坏 bootloader。

## Requirements

### BLE 扫描复用

- BMS 与控制器必须共享同一个 NimBLE 扫描资源，绝不同时注册或运行两个 discovery 扫描。
- 用户从 BMS 或控制器蓝牙列表发起扫描时，该请求必须成为唯一待执行扫描；如另一方正在扫描，先取消当前扫描，再在其完成事件后启动请求方扫描。
- 扫描所有权交接不得依赖 tick 调度先后或竞争时序；取消、完成、host sync 与连接中的状态都必须保持一致。
- BMS 与控制器候选列表、绑定 MAC/名称、状态栏和连接行为继续彼此隔离；扫描控制器时不得把 BMS 候选写入控制器列表，反之亦然。
- 已绑定设备的自动连接和本次“仅扫描选择设备”流程均保持可用。

### 旧 ESP32 显示选项

- 在 `esp32-wroom-32e-legacy` 的交互式配置流程中，`ili9341-2p8-spi` 必须作为可选择显示项出现，并以 `ILI9341 2.8 英寸，240 x 320，SPI` 描述。
- 命令行配置和验证必须接受该板型与 `xpt2046-spi` 输入组合，生成的硬件配置保留 240x320、ILI9341 和既有旧 ESP32 GPIO/分区约束。

### 编译告警

- I2C 触摸驱动全部被裁剪时不得声明未使用的 `touch_config`。
- I80 面板驱动全部被裁剪时不得声明未使用的 `panel_config`。
- 启用任一对应驱动的档案仍必须将相同配置传给驱动构造函数。

### 完整烧录包

- Bash 与 PowerShell 的本地构建都必须从 ESP-IDF 的 `flasher_args.json` 发布完整文件集到 `output/<profile>/`。
- 输出保留 `<profile>.bin` 作为 OTA/应用镜像，并生成 `<profile>-flash.bin` 作为从 `0x0` 写入的完整烧录镜像，以及包含各独立文件地址的 `flash-manifest.json`。

## Acceptance Criteria

- [ ] BMS 扫描进行中切换到控制器扫描后，BMS 扫描停止且控制器扫描在完成回调后启动，并填充 `controller_scan_candidates`。
- [ ] 控制器扫描进行中切换到 BMS 扫描后，控制器扫描停止且 BMS 扫描在完成回调后启动，并填充 BMS 候选列表。
- [ ] 连续重复发起任一来源扫描、在 host sync 前发起扫描，以及扫描期间绑定候选设备，不遗留两个 active/requested 标记或错误回调所有权。
- [ ] `ili9341-2p8-spi` 通过旧 ESP32 + XPT2046 的配置/验证回归，并在交互选项描述中显示 2.8 英寸规格。
- [ ] 受影响 ESP-IDF 配置矩阵构建不再报告这两处 `-Wunused-variable` 告警；启用 I2C 触摸和 I80 面板的构建继续通过。
- [ ] `test1` 等本地构建输出包含 bootloader、分区表、OTA 初始数据、应用镜像和从 `0x0` 写入的合并镜像；合并镜像内各文件字节位于清单声明的偏移。
- [ ] 相关自检、配置器测试、差异检查和目标构建通过，且不覆盖用户已有未提交修改。

## Out of Scope

- 同时扫描并同时连接 BMS 与控制器以外的多个 BLE 外设。
- 扩展新的 BMS 或控制器通信协议。
- 更改旧 ESP32 的 GPIO、分区表或用户已保存的配置。
