# 实施清单：安卓 Wi-Fi 投屏交互与 UI 优化

1. 对 `MainActivity` 及计划修改的连接、能力查询和投屏状态符号运行 GitNexus upstream impact；若为 HIGH/CRITICAL，先暂停并告知用户。
2. 在 `MainActivity` 内建立最小 UI 状态与统一渲染，改为可滚动的单页原生 View 布局，删除 Back/Home 占位控件。
3. 让热点请求和能力查询幂等；统一生命周期恢复、手动重试、权限结果、断线和停止路径。
4. 为 `CastService` 广播稳定状态类型，使 Activity 的投屏中、失败和已停止状态与服务生命周期一致，不改变编码或网络协议。
5. 添加一个聚焦状态到按钮语义/可用性的最小 Kotlin 单元检查；不引入测试框架或 UI 框架。
6. 生成手机 App 操作页预览到 `preview/`，检查小屏横竖屏无溢出或遮挡。
7. 运行 `./gradlew test` 与 `./gradlew assembleDebug`；执行 GitNexus `detect_changes --scope compare --base-ref main`，确认只影响预期 Android 流程。
8. 修复手动连接热点路径：识别当前 Wi-Fi后直接查询设备；删除密码输入、`X-Setup-Password`、Service 密码 extra 和 WebSocket 密码认证；投屏二维码替换为不含凭据的离线 App 深链，同时保留 Setup AP WPA2 和标准 Wi-Fi 二维码。

## 回滚点

- 若结构化服务状态需要侵入投屏协议，则只保留 Activity 本地状态，不扩展协议。
- 若预览环境不可用，保留构建和布局检查结果并明确记录未完成的视觉验证，不伪造预览。
