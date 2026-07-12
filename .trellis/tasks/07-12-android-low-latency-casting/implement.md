# 实施清单：Android 低延迟遥控投屏

1. 读取固件 HTTP、运行时、LVGL UI/bridge 现状，完成 GitNexus 影响分析；记录直接调用者、流程与风险。
2. 建立独立 cast 协议/会话组件和 host-side 单测：认证、长度/坐标检查、单客户端、ACK/超时/恢复。
3. 在 LVGL bridge 中实现固定块区域写入；在运行时注册 `cast-info`、WebSocket 和状态转移；接入主循环。
4. 追加投屏主页、二维码和投屏触摸导航；渲染预览至 `preview/`。
5. 创建 Android Gradle Kotlin app，实施深链、热点连接、MediaProjection、块差分 WebSocket、Back/Home 与可选无障碍服务；添加单测。
6. 构建固件与 Android APK，运行测试、尺寸检查及 `detect_changes`；按 LAN RFC2217 流程做可用的硬件验证。

## 风险点和检查门

- 不能改写现有用户未提交的 audio/runtime/UI 改动；重叠时先停止并汇报。
- 每次修改函数/方法前先做影响分析；若为 HIGH/CRITICAL，先提示用户。
- 不允许完整 RGB565 帧或依画面尺寸分配的服务器缓存。
- WebSocket API 依 ESP-IDF 版本实际能力实现；若 SDK 缺少二进制/异步收包接口，协议解析仍保持固定上限。

## 验证

```bash
./scripts/esp-idf-env.sh build
./gradlew :app:testDebugUnitTest :app:assembleDebug
git diff --check
```

在提交前运行 GitNexus `detect_changes`（若本环境提供 MCP），并进行热点、断线、退出、旋转、BMS/BLE 并发的真机检查。
