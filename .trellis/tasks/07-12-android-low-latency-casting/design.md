# 设计：Android 低延迟遥控投屏

## 边界与数据流

`Android Cast App → authenticated WebSocket → cast session/parser → LVGL bridge partial RGB565 write → TFT`。

反向流为 `XPT2046/LVGL gesture → cast session input message → Android App → optional accessibility action`。HTTP 仅发现能力和二维码入口，所有动态画面/输入走同一 WebSocket。

## 协议

- 协议版本固定为 v1；所有整数使用网络字节序。
- 每一帧由 `FRAME_BEGIN(sequence, rotation)`、零个或多个 `RGB565_BLOCK(x,y,w,h,pixels)`、`FRAME_END(sequence)` 组成；块 `1..16`，且必须落在 `cast-info` 的逻辑宽高内。
- 设备仅在一帧完成并提交后回 `ACK(sequence)`。客户端收到 ACK 才把该帧当作下一帧差分基线。
- HEARTBEAT 双向发送；超时关闭会话并恢复仪表。输入事件需要会话处于活动状态。
- WebSocket 握手认证使用 HTTP Basic 或 `X-Setup-Password`，与既有 API 一致；不能把密码写进日志、二维码之外的持久化或响应。

## 无 PSRAM 内存策略

服务端只使用固定 `16*16*2 = 512 B` 的接收块缓冲和固定消息头，不保存全帧或差分基线。Android 端承担缩放、分块、差分和帧在途控制；过期采集结果直接丢弃。

## 显示与 UI

桥接层暴露原子区域写入 API，调用方必须先获得 LVGL lock。开始投屏后 UI 层暂停普通仪表的刷新/覆盖；结束调用现有重绘路径。触摸在 cast 会话中优先给导航/远控，非投屏时保留原路径。

主页采用现有横向 tile/page 机制追加第 4 页。TFT 文案用 ASCII（例如 `REMOTE CAST`、`SCAN QR`、`ZH/EN`），二维码生成深链 `fuckingbms://cast/v1?...`，其参数为临时读取的当前 SSID/密码、主机和协议版本。

## Android 设计

Android module 使用 Kotlin、SDK 29+。深链 Activity 解析但不持久化密码；通过 `WifiNetworkSpecifier` 请求用户确认当前热点。开始按钮之后申请 `MediaProjection`，经 `ImageReader` 得到画面，在后台缩放/letterbox 到设备信息报告的尺寸。每块哈希与已 ACK 基线比较；一次只送一个帧序号。可访问性服务是可选能力，未授权则显示仅镜像与本机 Back/Home 说明。

## 兼容性、风险与回滚

未开启 Setup AP 时不启动投屏会话，App 显示打开热点提示。遇到协议错误、socket 关闭、心跳过期或显示写入失败即关闭会话并恢复仪表。回滚只需不注册 cast 路由与页面；不改变 NVS 格式、AP 策略或 BMS 协议。
