# Implementation Plan

1. 确认目标 APK（优先 `两轮智控.apk`/对应 `.yy-apk-analysis`）及可复现构建或重打包工具链；记录包名、版本、入口 Activity 和资源来源。
2. 在 APK 代码中定位底部导航/BMS 文案、BMS 扫描入口、控制器扫描入口、蓝牙连接服务和 EventBus 数据事件；先建立静态 smoke 检查。
3. 把扫描候选、绑定 MAC、连接状态按 `BMS`/`CONTROLLER` 分离；补齐刷新、取消、重复扫描、选择确认和断连清理。
4. 将导航文案改为“仪表”，保持 BMS 原功能路径可达。
5. 新增 APK 控制器仪表页面：速度/挡位主区、功率或电流/RPM/控制器温度/电机温度辅助区、连接与空数据状态；接入现有控制器 BLE 数据事件。
6. 设计并实现 MAC 下发 ACK 合同；若需要固件，增加最小命令解析、格式校验、BMS/Controller 独立 NVS key 和回读确认。
7. 执行静态检查、可用的 Android 构建/重打包、APK 安装启动验证；无 Android 构建链时记录阻断并至少验证 Java 语法/资源引用和协议自检。
8. 执行固件 `idf.py build`（仅当固件有改动）、GitNexus `detect_changes()`、`git diff --check`，确认没有修改 TFT UI。

## Rollback Points

- 先回滚 APK 导航/列表分离，不影响控制器仪表页面。
- 再回滚 MAC 下发协议；设备端保留旧 NVS 值。
- 最后回滚仪表页面接入，不回退已确认的列表 source 隔离。

## Validation

- 静态：source 字符串、候选数组、绑定 key、GATT UUID、资源 ID 均按 source 成对出现。
- 行为：BMS/Controller 扫描交替、选择各自 MAC、断连、重启回读；仪表真实帧/无效帧/断连三态。
- 工程：可用 Android 构建或重打包命令、APK 安装启动、固件构建（如改动）、`git diff --check`、GitNexus 变更检测。
