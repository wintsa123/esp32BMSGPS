# 设计：安卓蓝牙媒体控制

## 架构与边界

`音乐页触控 -> LVGL action event -> runtime 主循环 -> NimBLE notify -> Android BLE 前台服务 -> MediaSession/AudioManager`。

反向路径为 `Android NotificationListenerService / MediaController.Callback -> BLE GATT write -> runtime 命令队列 -> dashboard snapshot -> 音乐页标题与状态`。

不新建第二个 NimBLE Host、不修改 BMS 或控制器 GATT 客户端；媒体服务只在 `ESP_BMS_FEATURE_PHONE_MEDIA=1` 时注册在已经由 runtime 管理的本机外设连接上。

## BLE 协议

使用单个私有 128-bit 服务 UUID，广播服务 UUID 以便 Android 扫描过滤。服务包含两条特征：

- `command`：设备到手机，Notify；一个字节，`1=previous`、`2=next`、`3=volume down`、`4=volume up`。
- `state`：手机到设备，Write；`[version=1, flags, UTF-8 title...]`。`flags bit0` 表示 Android 已取得通知访问且服务已就绪，`bit1` 表示存在活动播放会话，`bit2` 表示正在播放。

标题载荷固定上限为 96 bytes。Android 连接后请求 MTU 128；若协商更小，按实际 MTU 和 UTF-8 边界截断。固件验证版本、长度、加密连接和命令范围；GATT 回调只复制到固定 FreeRTOS 队列，runtime tick 负责更新 snapshot，避免直接跨任务改 UI 数据。

## 固件设计

- `phone-media` 目录项在配置器中要求 BLE。`start.sh` 在生成的 profile CMake 中输出 `ESP_BMS_FEATURE_PHONE_MEDIA`；`main`、runtime 和 LVGL UI 将该定义传递到各自编译单元。
- runtime 在 `runtime_init_ble_host()` 中完成 GAP 初始化后注册媒体 GATT 服务，在蓝牙 GAP 的 connect/encryption/disconnect 状态迁移中维护媒体连接状态。注册保持在 NimBLE Host 启动前，且仅增加服务定义，不改变已有扫描、配对、Coded PHY 或 BMS/controller 状态机。
- runtime 保存 `media_title[97]`、媒体 flags 和连接/订阅状态；收到状态包后复制标题并使 `esp_bms_idf_runtime_tick()` 返回 changed。触控 action 在加密连接且 CCCD 已订阅时发送通知；任何失败只更新可观察状态/日志，不影响本地音量。
- dashboard snapshot 添加媒体状态与标题字段。LVGL 页面使用已有横向 `pages`、卡片过渡和 `pending_event` 模式，追加 `ESP_BMS_LVGL_PAGE_MUSIC`。四个固定尺寸触控按钮只排队新的媒体 action；标题设置宽度和滚动长文本模式以避免溢出。
- `ESP_BMS_FEATURE_PHONE_MEDIA=0` 时编译掉服务、页面、页面过渡和动作处理；现有页面滚动范围必须与当前功能配置严格一致。

## Android 设计

- 新增 `MediaControlService` 前台服务，拥有 BLE 扫描/连接、GATT 订阅、元数据上报和重连生命周期。它不依赖 Setup AP 或投屏服务。
- `NotificationListenerService` 用 `MediaSessionManager.getActiveSessions()` 选择活动会话并注册 `MediaController.Callback`。媒体标题优先 `METADATA_KEY_TITLE`，空值时显示播放器包名或空状态。
- 收到 `command` 通知后，服务调用 `transportControls.skipToPrevious()`/`skipToNext()`，音量由 `AudioManager.adjustStreamVolume(STREAM_MUSIC, ADJUST_LOWER/RAISE, 0)` 执行。
- Activity 保留现有投屏入口，并增加媒体连接/停止、蓝牙权限与通知访问设置入口；状态文本明确区分“蓝牙已连接”和“通知访问已授权”。

## 兼容性与回滚

- 仅 Android 10+。Android 12+ 使用运行时蓝牙权限，Android 13+ 的前台服务声明按 target SDK 35 要求更新。
- 没有 `phone-media` 模块、BLE 不可用、未配对、未订阅、无活动会话或未授权时，固件不发控制指令，页面保留安全的离线提示。
- 回滚只需从 profile 移除 `phone-media`；无 NVS schema 变更、无 BMS 控制器协议变更，已安装 App 可以保留但不会发现服务。
