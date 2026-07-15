# 修复 GPS 协议校验与 A-GNSS 注入

## Goal

让 ATGM336H-6N-74 的 RMC、CASBIN、A-GNSS 和射频安全诊断严格符合中科微协议，避免损坏数据被接受、合法辅助数据被拒绝，以及失败响应与接收器实际状态不一致。

## Background

- 目标硬件是 ATGM336H-6N-74，基于 AT6668，支持 GPS/QZSS/BDS2/BDS3/Galileo/GLONASS、A-GNSS、10 Hz 和抗干扰硬件；UART 默认 115200 baud。
- ESP32 目标是 ESP32-WROOM-32E、4 MB Flash、无 PSRAM。GPS 使用 UART1：RX/GPIO27、TX/GPIO18，PPS 使用 GPIO35。
- 当前硬件不是长期唯一目标；后续可能支持带 PSRAM 的其他 ESP32 系列芯片。协议解析和 HTTP 契约不能依赖当前芯片型号或 PSRAM 缺失这一条件。
- `GPS_SECURITY_JAM_CHANNEL_MASK=0x07` 对应该型号的 1575、1561、1602 MHz 三个射频通道。
- 中科微 R6 CASBIN 协议要求载荷长度为 4 的整数倍；当前白名单中的固定 MSG 载荷为 16 至 92 字节，`MSG-IGP` 为 `16+2*N`，可超过当前 128 字节上限。
- GitNexus 已将 `runtime_feed_gps_byte()` 的上游影响评为 HIGH；修改前仍需针对每个实际编辑符号重新运行 impact。

## Requirements

### R1. 严格验证 RMC 校验和

- `runtime_parse_rmc()` 必须要求语句以恰好一个 `*HH` 校验和结尾。
- 缺失校验和、校验和位数错误、非十六进制字符或校验和后存在额外字符时返回解析错误。
- 非 RMC 语句继续忽略，不计入 RMC 解析错误。

Evidence: `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:1220` 当前只在遇到 `*` 时校验，缺失 `*HH` 仍可进入发布路径。

### R2. 按消息类型验证 CASBIN 载荷

- 保留现有 A-GNSS class/id 白名单，但增加协议规定的载荷长度验证。
- 固定长度消息必须精确匹配 R6 长度。
- `AID-INI` 必须为 56 字节。
- `MSG-IGP` 必须满足 `16+2*igpNum`、内部 `igpNum` 字段一致、总长度为 4 的整数倍，并允许协议内超过 128 字节的合法帧。
- C 固件与 `scripts/push-agnss.py` 必须使用同一组长度契约，不能一端接受而另一端拒绝。

Evidence: `components/esp_bms_idf_runtime/include/esp_bms_gps_stream.h:12` 和 `scripts/push-agnss.py:13` 当前限制为 128 字节；`runtime_gps_agnss_message_allowed()` 仅检查 class/id。

### R3. 消除失败响应后的隐式部分注入

- `/api/gps/agnss` 不能在返回“请求被拒绝”时隐瞒已经写入接收器的帧。
- 接口收敛为单个 CASBIN 帧一次请求：先完整接收并验证一帧，再写 UART；脚本预验证整个文件后逐帧发送。
- 单帧接口作为所有 ESP32 型号的兼容基线，不因目标是否带 PSRAM 而改变请求语义。
- 当前实现不为批量原子校验常驻或临时分配 32 KiB 缓冲区；未来若确有批量吞吐需求，可在不改变解析器和单帧接口的前提下增加可选能力。

Evidence: `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:3221` 当前逐帧写 UART，后续无效帧仍会令整个请求返回 400。

### R4. 正确区分射频安全状态

- `spoof=0` 或 `jam=0` 必须记录为 unknown，不能记录为 clear。
- 仅状态值 1 表示未检测到欺骗或干扰；值 2/3 表示告警等级。
- 状态变化日志继续限频，60 秒汇总保持可诊断。

Evidence: `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:5464` 当前把所有小于 2 的状态记录为 clear。

### R5. 保持现有运行时边界

- GPS UART 继续独立于当前 LVGL 页面持续轮询。
- PPS ISR 仍只更新计数，不执行日志、分配或 UART 操作。
- A-GNSS 注入期间继续避免周期安全查询插入注入序列。
- 不改变 Setup AP、BMS、控制器、投屏或 OTA 行为。

### R6. 保持多 ESP32 型号可移植性

- CASBIN 编解码、消息长度验证和 helper 分帧逻辑保持纯 C/Python，不依赖 ESP32-WROOM-32E、Xtensa 或 PSRAM API。
- 固定缓冲区尺寸由受支持的 CASBIN 协议上限决定，不由当前设备剩余堆大小决定。
- 不新增 `CONFIG_IDF_TARGET_ESP32` 等仅服务当前芯片的条件分支；目标相关 UART/GPIO 配置继续留在现有硬件配置边界。
- 本任务不要求现在实现 PSRAM 批量缓冲，但不得阻碍未来通过独立可选路径使用 PSRAM。

## Acceptance Criteria

- [ ] 缺失、截断或带尾随字符的 RMC 校验和全部被拒绝；合法 GP/GN/GA/GL/BD RMC 继续通过。
- [ ] 所有白名单固定长度 CASBIN 消息只接受协议规定长度，畸形短帧不能返回注入成功。
- [ ] 合法且大于 128 字节的 `MSG-IGP` 可通过脚本和固件验证；不超过协议与设备内存边界。
- [ ] 一个失败的 HTTP 请求不会被报告为“完全未注入”却已经静默写入部分帧。
- [ ] MON-SEC 的 0/1/2/3 状态分别呈现 unknown/clear/alert/alert 语义。
- [ ] `tests/gps_stream_selftest.c` 和 A-GNSS helper 自测覆盖上述边界并通过严格警告编译。
- [ ] `./scripts/esp-idf-env.sh build` 通过，Flash/OTA 空间不发生不可接受回退。
- [ ] 通过 RFC2217 刷写 ATGM336H-6N-74 实机，验证 10 Hz RMC、A-GNSS 注入、CFG-JSM、MON-SEC 和 PPS 日志。
- [ ] GitNexus `detect-changes` 只显示预期 GPS/runtime/脚本/测试符号与执行流。
- [ ] 纯 GPS/CASBIN 主机自测不依赖 ESP-IDF target 宏或 PSRAM，并能在标准 C11 编译器下运行。

## Out Of Scope

- 轨迹存储、地图、GPX/KML、TF 卡或新的 GPS UI。
- 新增第三方 NMEA/CASBIN 库。
- 保存接收器配置到 Flash 或改变卫星系统选择。
- 为通用 CASBIN 协议实现全部小于 2 KiB 的消息；只支持当前 A-GNSS 白名单和安全诊断消息。
- 在本任务中增加 PSRAM 专用批量 A-GNSS API，或为尚未接入的 ESP32-S3/C3/C6 等目标建立完整板级配置。
