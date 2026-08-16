# 设计：Android 低延迟镜像投屏

## 边界与数据流

`Android MediaProjection -> 手机端旋转/拉伸/RGB565 -> WebSocket v2 -> cast session -> display service dummy-draw -> I80 TFT`。

设备端只接收已转换的最大 `64x64` 块，不保存完整帧、不做缩放。MVP 协议只含画面、心跳和 ACK，不传反向单指输入或其他控制消息。普通 LVGL UI 在投屏期间没有渲染或输入所有权。

## 会话状态与资源所有权

`IDLE -> PREPARE -> ACTIVE -> RESTORE -> IDLE` 由固件主循环拥有，HTTP/WebSocket 处理器只请求状态转移。

- `PREPARE`：显示服务等待在途 LVGL DMA，释放 LVGL 根页面、页面内容、静态缓存和 UI 定时器，启用 `esp_lv_adapter_set_dummy_draw()`；随后才标记会话可接收像素。
- `ACTIVE`：显示服务只处理 cast 控制和 RGB565 块，不调用普通 LVGL 更新、`lv_timer_handler()`、轮播或 UI action。主循环只维护投屏心跳和必要系统服务；当前 cast profile 停止控制器 BLE 与 GPS，退出时按原 profile 重启。
- `RESTORE`：先恢复保存的显示方向并重建 LVGL UI，再关闭 dummy-draw 以请求完整首帧刷新，随后恢复控制器 BLE、GPS 和普通 runtime/module tick。

显示驱动、Setup AP、HTTP/WebSocket、主循环与看门狗不在暂停集合中。协议错误仍关闭连接，避免无主显示会话。

## 显示、颜色与内存

显示桥把投屏块交给现有 `esp_lv_adapter_dummy_draw_blit(..., true)`，该调用复用适配器已有的颜色完成 ISR，并在返回前确认 DMA 已完成。桥接层只有固定最大 `8192 B` 的字节序转换缓冲；wire RGB565 固定为大端，桥按当前 LVGL 颜色格式转换为适配器输入格式。

这样 WebSocket 的静态接收数组在提交返回后可安全复用，I80 面板使用与正常 LVGL 刷新相同的字节序路径。不会引入全屏 RGB565 缓冲、帧队列或设备端差分基线。

## 分辨率与旋转协议

协议版本升为 v2。`GET /api/cast/info` 返回当前 profile 的两个可用逻辑方向及尺寸，而非假定 `320x240`。Android 依据手机实时方向选择宽大于高或高大于宽的设备选项；`FRAME_BEGIN` 携带目标设备旋转值。

接收首个目标方向帧时，显示服务先切换面板和触摸逻辑分辨率，再验证块坐标。Android 将捕获源图像旋转并拉伸到选中的完整目标尺寸，重建 `FrameEncoder` 并发送完整首帧；后续继续用 ACK 基线差分。每次方向改变均在帧边界重新开始，避免混合尺寸或坐标。

临时投屏方向不写入 NVS，也不覆盖用户原来的 `display_rotation`；`RESTORE` 总是恢复该值。

## 组件职责

- `esp_bms_idf_runtime`：协议 v2、会话就绪/关闭、真实分辨率查询、心跳和帧边界方向校验。
- `esp_bms_display_service`：串行化 `ENTER_CAST`、`SET_CAST_ROTATION`、RGB565 和 `EXIT_CAST` 命令；投屏时屏蔽普通 UI 工作。
- `esp_bms_lvgl_bridge`：dummy-draw 开关、固定块字节序转换、同步 DMA 提交和真实逻辑分辨率查询。
- `esp_bms_lvgl_ui`：可逆的投屏暂停/恢复，释放并重建对象树而不重启显示驱动。
- `idf_main`/module registry：依据会话边沿暂停和恢复 controller/GPS；GPS 恢复复用现有初始化路径重建 UART/PPS，不能假定存在未提供的 start API；避免在 `ACTIVE` 中执行普通 runtime/module tick。
- Android `CastService`/`FrameEncoder`：手机方向检测、转换/拉伸、v2 capability 解析、帧边界重建和 ACK 控制。

## 兼容性、风险与回滚

v1 App 与 v2 设备互相拒绝，防止旧客户端发送错误方向语义。当前 `cast-s3-build` 是主要验证 profile；其它面板通过 bridge 的实际逻辑分辨率和颜色格式自动适配。

风险在于暂停服务后恢复时 BLE/GPS 会重新连接或重新探测，因此恢复流程必须幂等并记录失败日志。回滚只需禁用 cast 路由/模块；不修改 NVS、AP 策略、持久显示方向或 BMS 数据格式。
