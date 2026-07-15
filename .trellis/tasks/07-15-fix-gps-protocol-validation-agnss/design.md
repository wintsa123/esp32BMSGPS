# Design: GPS 协议校验与 A-GNSS 注入修复

## Boundaries

- `esp_bms_gps_stream.c/.h` 负责 CASBIN 帧边界、校验和和最大受支持帧尺寸。
- `esp_bms_idf_runtime.c` 负责 RMC 语义、A-GNSS class/id/长度白名单、HTTP 到 UART 的提交以及 MON-SEC 状态投影。
- `scripts/push-agnss.py` 负责下载/生成辅助数据、完整预验证和调用设备接口。
- 主机自测验证纯解析逻辑；ESP-IDF 构建和实机日志验证任务、UART 与硬件行为。

## RMC Contract

解析器先确认语句种类，再定位唯一的 `*`。`*` 后必须恰好两个十六进制字符且位于行尾；校验范围仍为 `$` 后到 `*` 前。只有校验和与字段解析均成功时更新 fix、速度和 UTC。

## CASBIN Length Contract

使用一个最小的消息长度校验函数，按 class/id 返回合法性：

| Class/ID | Payload bytes |
| --- | ---: |
| MSG 00 | 20 |
| MSG 01 | 16 |
| MSG 02 | 92 |
| MSG 03 | 16 |
| MSG 04 | 92 |
| MSG 05 | 20 |
| MSG 06 | 16 |
| MSG 07 | 72 |
| MSG 08 | 68 |
| MSG 09 | 20 |
| MSG 0B | 76 |
| MSG 0C | 20 |
| MSG 0D | 16 |
| MSG 0E | 72 |
| MSG 11 | 88 |
| MSG 17 | `16+2*payload[14]` |
| AID 01 | 56 |

白名单的最大实际载荷由 `MSG-IGP` 决定。仍保留 CASBIN 的 4 字节对齐检查，不扩展到未使用的通用 2 KiB 消息。

## HTTP Injection

已确认协议为“一次请求一个 CASBIN 帧”：

`file/server data -> helper validates whole stream -> split frames -> POST one frame -> device validates full frame -> UART write -> success response`

这样设备只需一个最大帧缓冲区，响应语义与 UART 提交一致。脚本在开始发送前先验证全部输入，因此本地文件中后置坏帧不会导致部分发送。设备端仍独立验证，不能信任脚本。

批量单请求方案本期不实施。它需要整体缓冲后验证；在当前 ESP32-WROOM-32E 上，最多 32 KiB 堆分配会提高 HTTP、LVGL、Wi-Fi 和 BLE 并发下的内存风险。未来带 PSRAM 的目标若需要批量吞吐，可增加独立可选路径，但单帧接口继续作为跨型号兼容基线。

## Platform Compatibility

- `esp_bms_gps_stream.c/.h` 不包含 ESP-IDF target 或 PSRAM 条件逻辑，最大帧尺寸只来源于本任务支持的 CASBIN 消息集合。
- HTTP handler 使用一个最大 CASBIN 帧缓冲区，不根据 PSRAM 有无改变请求协议。
- 当前 UART/GPIO 常量仍属于 ESP32-WROOM-32E 板级配置；未来多目标支持应在板级配置任务中迁移这些值，不在协议解析器中增加芯片判断。
- 不提前引入 allocator 接口、PSRAM 工厂或双实现。未来批量接口有实际需求时，再使用 ESP-IDF capability allocator，并保持单帧端点向后兼容。

## Security State

- `0`: unknown
- `1`: clear
- `2` 或 `3`: alert

只在状态组合变化时记录日志。unknown 不得清除既有告警的语义，汇总日志继续输出原始数值。

## Compatibility And Rollback

- 线路、波特率、PPS、10 Hz 和 `0x07` 射频通道掩码保持不变。
- 旧的批量 helper 调用方式若改为单帧请求会发生接口行为变化，但仓库内唯一调用者同步更新。
- 回滚时可独立恢复 HTTP 单帧行为，不影响 RMC、CASBIN 长度和 MON-SEC 修复。
- 后续切换到带 PSRAM 的 ESP32 型号不需要修改 CASBIN parser、消息长度表或 helper 分帧格式。
