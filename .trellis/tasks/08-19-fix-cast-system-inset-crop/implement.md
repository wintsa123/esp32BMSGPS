# 实施计划：Android 投屏系统栏裁剪

1. 读取 `trellis-before-dev` 指南并检查 Android 包规范。
2. 在 `MainActivity` 增加窗口 inset 状态，在启动投屏 Service 时传递四边值。
3. 在 `CastService` 增加 inset 解析、源尺寸保存和安全 crop rect 计算。
4. 让 JPEG 编码使用裁剪后的 `Rect`，保留当前 bitmap 复用、帧节流和 ACK 发送逻辑。
5. 为 crop rect 纯函数添加最小单元测试，覆盖零 inset、顶部/底部 inset、越界 inset 和旋转尺寸。
6. 运行 `./gradlew test` 与 `./gradlew assembleDebug`，再执行针对 Android 投屏文件的 `git diff` 检查。
7. 进入实现前运行 GitNexus impact；完成后运行 `detect_changes()`，确认只影响 Android capture 流程和对应测试。
