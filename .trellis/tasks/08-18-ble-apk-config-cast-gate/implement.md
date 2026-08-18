# 实施计划

1. 盘点固件 HTTP 状态/配置/manifest 处理函数和 BLE 广播定义，运行 GitNexus impact，确定可复用入口。
2. 在固件增加最小 GATT 请求/响应协议、路径白名单、分片/长度校验及自检；保持投屏路径排除。
3. 在 Android 增加设备 BLE 会话与传输适配，让状态、manifest、config 读写可走 BLE。
4. 在 `MainActivity` 增加 `NONE/WIFI/BLE` 状态渲染，BLE 模式开放查看/修改，投屏按钮要求 Wi-Fi 并显示中文提示。
5. 添加协议/状态单测，运行 `idf.py`/CMake 目标与 `./gradlew testDebugUnitTest assembleDebug`。
6. 运行 `detect_changes()`，检查只影响预期符号后完成任务归档。
