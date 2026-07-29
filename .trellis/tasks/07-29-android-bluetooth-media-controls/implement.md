# 实施清单：安卓蓝牙媒体控制

1. 新增 `phone-media` 编译模块和 `ESP_BMS_FEATURE_PHONE_MEDIA` 生成/传递规则；验证目标无 BLE 时模块选择被拒绝。
2. 在 runtime 中以固定队列注册媒体 GATT 服务、处理安全连接/订阅/状态写入和控制通知；新增最小协议自检或 host 可编译检查。
3. 扩展 dashboard snapshot、LVGL action 与轮播：创建音乐页、长标题处理、四个按钮、动态滚动边界和页面过渡；扩展模拟器断言。
4. 扩展 Android Manifest、Activity 和 Kotlin 服务：权限、BLE lifecycle、通知访问、MediaSession 元数据及命令执行；为协议编码/标题截断添加 Kotlin 单测。
5. 分别构建关闭/开启 `phone-media` 的 profile，构建 Android debug APK 并运行测试；运行模拟器生成并检查 `preview/` 截图。
6. 运行 GitNexus `detect-changes`，构建功能开启固件，按 RFC2217 刷写，检查启动日志、BMS/controller 并行 BLE 以及 Android 媒体控制。失败时保留精确日志并修复。

## 风险与检查点

- `runtime_init_ble_host()` 为 CRITICAL 影响面：先完成 Host 前 GATT 注册的静态/构建检查，再以 BMS、控制器和本机手机三类 BLE 流程做回归。
- 任何文件中出现 `ESP_BMS_FEATURE_PHONE_MEDIA` 时，检查 profile 生成、main、runtime 与 UI 四个定义点，以确保功能真能裁剪。
- 媒体标题是外部输入：长度、协议版本、连接加密状态和 UTF-8 截断边界必须验证；不得记录原始媒体标题或载荷。
