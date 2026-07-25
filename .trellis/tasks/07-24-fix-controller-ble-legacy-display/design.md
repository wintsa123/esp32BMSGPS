# 设计：BLE 扫描仲裁与旧 ESP32 显示回归

## 边界

`esp_bms_bms_ble` 与 `esp_bms_controller_ble` 继续分别拥有各自协议、候选表和 GAP 回调。NimBLE 扫描是共享底层资源，因此运行时只提供一个轻量的扫描仲裁状态，不迁移协议代码或合并候选数据。

## 扫描交接

1. 新扫描请求写入唯一的待扫描所有者，并清除另一来源的待请求标记。
2. 若 discovery 已运行，调用 `ble_gap_disc_cancel()`；当前所有者的 `BLE_GAP_EVENT_DISC_COMPLETE` 只负责关闭自身 active 状态，再请求运行时启动待扫描所有者。
3. 若 discovery 未运行，立即启动待扫描所有者。
4. host sync 使用同一待扫描所有者决策，避免 BMS/控制器标记同时为真时依赖 if/else 顺序。
5. 启动成功时清除待请求标记并设定 active；启动失败保留该来源的可重试状态，不污染另一来源。

这将扫描启动从周期性 tick 的偶然时序改为 GAP 完成事件上的明确状态转换。BMS 或控制器组件只调用公共仲裁入口，不直接假设自己能拥有 discovery 回调。

## 编译期裁剪

`touch_config` 和 `panel_config` 的声明采用与其驱动调用相同的编译期开关。没有匹配驱动时，函数直接返回 `ESP_ERR_NOT_SUPPORTED`；存在驱动时配置对象仍在该编译单元中可用。

## 旧 ESP32 2.8 英寸配置

保留现有 `ili9341-2p8-spi` catalog 记录，不创建重复板型。补充自动化配置器覆盖，验证旧 ESP32 过滤逻辑和动态描述都不会把它遗漏。该条目继承现有 ILI9341 初始化与 XPT2046 SPI 输入，不改变 GPIO 或生成器合同。

## 构建输出烧录包

ESP-IDF 在构建目录的 `flasher_args.json` 是唯一的烧录文件和偏移来源。新增共享 Python 发布器供 Bash 与 PowerShell 调用：复制该清单引用的各镜像到 `output/<profile>/`，保留应用镜像为 `<profile>.bin`，并以 `0xff` 填充镜像间隙生成 `<profile>-flash.bin`。合并包从 flash offset `0x0` 写入，独立文件地址由同目录 `flash-manifest.json` 声明；因此不会把应用镜像误当成 `0x1000` 的 bootloader。

## 兼容与回滚

- 公开 action、snapshot 布局和固件配置格式不变。
- 回滚只需还原仲裁入口与相关测试；无 NVS 迁移或硬件配置格式变更。
