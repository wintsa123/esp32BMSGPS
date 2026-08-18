# 修复安卓投屏顶部状态栏黑边

## Goal

移除 Android 全屏投屏画面中的状态栏/导航栏空白区域，让 ESP32 目标屏幕显示手机应用内容而不是顶部黑边。

## Background

- `MainActivity.kt:1660-1663` 使用 MediaProjection 默认显示器采集，系统栏会包含在源图像中。
- `CastService.kt:193-197` 当前把整张 `ImageReader` 图像直接缩放到目标尺寸，没有裁剪系统 inset。
- 该黑边不是 `FLAG_SECURE` 绕过问题；本任务只处理已授权采集结果中的像素裁剪，不改变 Android 投屏授权或录屏提示行为。

## Requirements

1. 投屏启动时取得当前 Activity 的系统栏 inset，并传给 `CastService`。
2. Android 端编码前裁剪顶部、底部以及必要的左右系统 inset，再将有效内容缩放到设备声明的目标尺寸。
3. 兼容当前最低 Android 版本和 Android 14 的默认显示器投屏流程；不得移除用户授权。
4. 旋转或窗口尺寸变化时不得因裁剪矩形越界、零尺寸或旧帧复用导致投屏失败；无有效 inset 时回退为整屏采集。
5. 保持现有 JPEG 协议、目标分辨率、帧率、ACK 和固件端接口不变。

## Acceptance Criteria

- [ ] 使用带状态栏/导航栏的 Android 手机投屏时，ESP32 屏幕顶部不再出现对应的黑色空行，应用有效内容铺满目标画面。
- [ ] 横屏、竖屏至少各验证一次；画面无越界、拉伸异常或崩溃。
- [ ] 没有可用 inset 或裁剪参数异常时仍能正常投屏，行为等同当前整屏模式。
- [ ] 现有 `CastProtocolTest`、`CastCapabilitiesTest` 及新增裁剪纯函数测试通过。
- [ ] Android 项目构建通过，固件投屏协议代码无需修改。

## Out of Scope

- 绕过 MediaProjection 用户授权、录屏指示器或 Android 安全策略。
- 改为应用窗口投屏、修改投屏协议或调整 ESP32 解码/显示链路。

## Open Questions

无。MVP 采用 Android 端传递 inset、编码前裁剪的方案。
