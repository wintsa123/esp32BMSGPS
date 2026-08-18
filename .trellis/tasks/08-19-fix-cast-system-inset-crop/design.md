# 设计：Android 投屏系统栏裁剪

## 边界

- `MainActivity` 负责读取窗口系统栏 inset，并在启动 `CastService` 时传递。
- `CastService` 负责保存源显示尺寸和裁剪 inset，在 `consumeImage` 中生成安全裁剪矩形后编码。
- ESP32 固件和 JPEG/WebSocket 协议保持不变。

## 数据流

1. 根视图收到 `WindowInsets`，记录 left/top/right/bottom 像素值。
2. `startService` Intent 增加四个 inset extra；旧调用缺失时默认为 0。
3. `CastService.configureCapture` 保存当前源显示尺寸与 inset。
4. `consumeImage` 将源 inset 按 `image/source` 比例映射到 ImageReader 图像，使用 `Rect` 限制源区域，再绘制到既有目标 bitmap。

## 兼容与取舍

- 不改变 `createVirtualDisplay` 的目标尺寸和已有目标比例算法，降低协议和固件风险。
- inset 读取使用现有原生 View API，兼容 minSdk 29；API 不可用或值非法时回退整屏。
- 裁剪矩形采用边界 clamp，避免刘海、导航模式或旋转期间出现负尺寸。
- 这是会话级参数；旋转触发既有 display listener 重配时重新计算映射，未更新 inset 时仍以最后一个合法值工作。

## 回滚

删除 Intent inset extra 和 `consumeImage` 的 crop rect 即可恢复当前整屏行为；协议无需回滚。
