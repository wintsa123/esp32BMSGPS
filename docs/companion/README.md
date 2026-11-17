# 音乐、歌词与本地导航

2026-11-17。本次实现参考 MotoNav 的手机绘制、设备显示思路和暗色青色界面，按本项目 320×480 / 480×320 矩形屏重新布局。源码参考快照：`CharlesXu/MotoNav@3b93facfd244b0477950189b6e26aa913b2ded78`。原项目显示歌名和歌手；这里另外增加可用歌词同步与 LRC 导入。

## 使用

1. 安装仓库根目录的 `两轮智控.apk`，连接设备蓝牙。固件也必须更新到包含本次 `/api/companion` 协议的版本。
2. App 的“功能”页进入“音乐与歌词”或“实时导航”；“设置 → 同步与地图设置”管理后台同步、播放器信息权限与地图凭证。
3. 在系统页面授予播放器信息读取权限，打开音乐播放器。歌名、歌手随媒体会话同步；点击“在设备显示音乐”切换设备页面。现有 BLE HID 媒体控制保持优先，未建立 HID 时可由 App 接收屏幕按钮动作。
4. 高德使用 **Web 端 JS API Key + 安全密钥**；本机构建已配置用户提供的默认值，设置中可以覆盖。天地图也可使用 APK 的构建默认值。导航输入完整地址，或输入高德 GCJ-02 的 `经度,纬度`，授予精确定位后开始。当前统一使用普通骑行规划。
5. 后台同步使用常驻通知。按 Home 后服务继续运行；系统强行停止、撤销权限或厂商省电限制后需重新打开 App。通知“停止”可关闭同步。

Android 没有统一的“当前歌词”标准字段。实现读取播放器明确共享的歌词字段或歌词通知字段；播放器不提供时只显示歌名、歌手，不报错。可为当前歌曲导入 UTF-8 LRC，按歌名、歌手匹配；导入内容只在当前 App 进程保留。未收到手机数据时设备显示安静的占位界面，失联约 45 秒后回收旧画面。

天地图构建默认值读取 `android-cast/local.properties` 的 `companion.tiandituKey`，该文件已忽略，不提交真实 Key。2026-11-17 本机构建已按用户要求使用 YunPu 的 `TIANDITU_TK`，一次模拟安卓原生请求返回 HTTP 200、256×256 PNG；这不是实际手机网络验证。手动设置优先，保存空值可停用瓦片。Key 会随 APK 分发并可被提取，与 YunPu 共用同一凭证的额度。

高德默认凭证读取同一忽略文件的 `companion.amapJsKey` 与 `companion.amapSecurityJsCode`，没有写入 Git；旧的 `amap_key` Web 服务配置不再读取。App 内独立 WebView 加载打包的骑行规划页，通过官方 JSAPI 2.0 的 `AMap.Geocoder`、`AMap.Riding({policy: 0})` 完成地址查询和普通骑行规划；不再拼接 REST 算路请求，也没有电动车、摩托车、汽车模式选择。

规划 WebView 仅加载自身的 `https://bms.local/companion-riding.html` 文档与 HTTPS 高德资源，禁用文件和内容访问、页面跳转及 iframe；请求通过可用互联网网络，避免受投屏 Wi-Fi 进程绑定影响。原生回调校验请求编号、最大响应体、分段数、坐标数和坐标范围；60 秒超时、取消和服务销毁会丢弃结果并释放对应页面。脚本或服务失败后可重试，不保留损坏的页面。若高德 Key 设置了域名白名单，需要允许此本地页面的来源。

当前为个人调试 APK，JS 安全密钥会随包分发，能够被提取。公开分发前应改成自有服务器 `serviceHost` 代理保存安全密钥。

## 字体、数据流与内存

手机通过 `MediaSessionManager` 读取媒体元数据和播放进度，使用 Android 字体在 Canvas 绘制中文，再将画面量化为 16 色并编码为 RLE。单片机仅解码成 LVGL I4 图像，不需要完整中文字库。屏幕媒体按钮仍使用项目原有的极小中文子集字体。

复用原有加密 BLE GATT 请求通道和共享会话；前台 Activity、后台服务共用连接。只发送设备当前音乐/导航页的画面。导航帧不会重叠排队；音乐只有文字或播放状态变化时发送。帧传输期间也轮询页码和按钮，切页后取消过时画面。

全屏 320×480 I4 像素区为 76,800 字节，另有 64 字节调色板。固件只保留一个页面及其接收缓冲；提交完整帧前保留上一帧，避免半帧显示。错误片段不会部分写入有效图像，接收中断后会清理暂存区。真实 BLE 吞吐和瓦片复杂度会影响屏幕刷新速度，尚未完成真机测速。

## 导航与额度

高德只负责地址解析、首次规划和确认偏航后的重新规划。手机 GPS、本地路线匹配、剩余距离与转弯距离计算不请求高德。天地图提供 `vec_w` 底图，瓦片异步下载，缓存最多 160 张；未配置 Key 或下载失败时仍显示路线与指引。

- 使用 `AMap.Riding` 回调中的 `routes[0].rides`、分段 `path`、骑行时间和文字指引，桥接为本地导航路线；坐标格式化到六位小数。
- 地址解析走 `AMap.Geocoder`；直接输入经纬度、或偏航重新规划时已缓存目标坐标，则不重复请求地址解析。
- 高德路线 GCJ-02 坐标转换为 WGS-84，再投影到天地图 Web Mercator 瓦片；手机 GPS 使用 WGS-84。
- 精度超过 40 米时不确认偏航；偏离路线至少 45 米且持续 12 秒才触发判断。自动重新规划间隔至少 120 秒。
- 每个自然日最多尝试 20 次路线请求，失败也计数，首次算路失败不会反复自动请求；地址查询与天地图瓦片各自消耗对应服务额度，**20 次不是全部 API 请求数，也不保证免费**。
- 普通骑行不包含摩托车专用禁限行规则。JSAPI 的实际可用权限、额度及费用以账号控制台为准。

官方接口契约核对来源（2026-11-17）：

```text
https://lbs.amap.com/api/javascript-api-v2/guide/services/navigation
https://lbs.amap.com/demo/javascript-api-v2/example/riding-route/riding-custom
```

这是本地路线跟随，不包含实时路况导航引擎、语音播报或交通规则的本地数据库。GPS 超过 15 秒没有新定位时提示等待可靠定位。Android 35 模拟器已用真实凭证完成高德地址解析、普通骑行算路及天地图瓦片读取；尚未完成实体手机、设备 BLE 和道路导航验证。

## 协议

现有 HTTP / BLE JSON API 均支持：

```text
GET /api/companion
  version, boot_id, action_id, action, page, width, height,
  music_height, encoding="rle4", chunk_bytes=128

POST /api/companion
  {op:"select", slot:0|1}
  {op:"begin", slot, frame, width, height}
  {op:"chunk", slot, frame, offset, data:"十六进制"}
  {op:"commit", slot, frame}
  {op:"ping"|"clear", slot}
  {op:"ack", slot:0, frame:action_id}
```

`slot=0` 音乐图像占屏幕顶部，底部保留原生媒体按钮；`slot=1` 导航全屏。RLE 字节低 4 位为调色板索引；高 4 位 0～14 表示连续 1～15 个像素，高 4 位 15 后跟小端 uint16 长度，表示连续 16～65535 个像素。分块可在长游程中间截断；解码器验证偏移、长度、总像素数后才提交。

按钮队列最多 8 条、10 秒失效；动作 1～5 分别是上一曲、播放暂停、下一曲、音量减、音量加。App 按 `boot_id:action_id` 去重后确认，避免重试导致重复切歌。

## 验证记录

- Android debug APK 构建成功；46 项 JVM 单元测试通过，其中 8 项覆盖歌词时间轴、坐标、路线跟随、JSAPI 桥接路线契约、可信资源地址、投影和编码。
- `node tests/companion_riding_check.cjs` 通过：模拟官方插件验证骑行分段、地址解析、缓存目标、坐标边界、取消、过期回调、错误凭证脱敏与加载失败重试。
- Android 35 的真实 JSAPI 联调通过：对“北京南站”进行地址解析后得到 137 个路线点、11 个分段，读取 256×256 天地图瓦片并生成实际导航帧；取消回调只触发一次。数据见 `evidence/riding-validation.json`，日志见 `evidence/android-riding-instrumentation.log`，画面见根目录 `preview/companion-navigation-jsapi-riding.png`。复跑此在线检查会使用服务额度：`adb shell am instrument -w -r -e check riding com.fuckingbms.cast.test/com.fuckingbms.cast.CompanionInstrumentedChecks`。
- Android 35 模拟器中创建真实 `MediaSession`，验证中文歌名/歌手、带时间戳歌词、四种帧尺寸以及按 Home 后前台服务继续运行。日志见 `evidence/android-instrumentation.log`。
- 将 Android 实际编码产物输入 LVGL 模拟器，320×480 / 480×320 的音乐和导航显示通过。图像在仓库根目录 `preview/companion-*.png`，导航演示没有在线瓦片。
- C 解码器测试使用 ASan / UBSan；现有 UI/GPS/遥控协议 9 项 Python 检查通过；Web typecheck/build 与入口桥接、320/390 像素布局、普通浏览器提示检查通过。
- ESP-IDF 6.0.2，`esp32s3-n16r8-st7796-ft6336` profile 编译通过，产物在 `build-companion-clean/`。为避免修复工作污染现有依赖目录，在 `/vol1/1000/tmp/companion-idf-source` 建立隔离工程，指向仓库组件并使用组件管理器验证依赖；锁文件保存在 `evidence/firmware-dependencies.lock`。
- 已按项目约定尝试 `rfc2217://192.168.2.10:4000?ign_set_control`、115200 波特刷写。TCP 探测与刷写均超时，未建立连接；设备未刷入。原始错误见 `evidence/flash.log`。
- GitNexus 已执行符号影响检查与完整结构化变更分析，历史记录见 `evidence/`。2026-11-18 刷新两个仓库索引后重新分析全部改动，前端 252 个符号、47 条流程，评级 CRITICAL；主仓库 125 个符号、未解析出受影响流程，不能据此认为固件不受影响。两份结构化结果均未截断；UNKNOWN 已结合实际调用点与协议回归检查。本批代码整理至各自 main 分支；没有发布线上网页或完成硬件刷写。局域网连接测试见 `../app-controls/lan-test.md`。

复现核心检查：

```bash
npm --prefix vercel run typecheck
RUN_TESTS=1 ./scripts/build-android-cast.sh
python3 -m pytest -q tests/test_dashboard_ui_contract.py tests/test_gps_status_api.py tests/test_remote_controls.py
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Icomponents/esp_bms_lvgl_ui/include components/esp_bms_lvgl_ui/esp_bms_companion_codec.c tests/test_companion_codec.c -o /tmp/companion-codec-check
/tmp/companion-codec-check
# Vite 服务运行于 8787 时：
node tests/dashboard_companion_check.cjs
```
