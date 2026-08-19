# 定位投屏真实连接失败

## Goal

TBD.

## Requirements

- TBD

## Acceptance Criteria

- [ ] TBD

## Notes

- Keep `prd.md` focused on requirements, constraints, and acceptance criteria.
- Lightweight tasks can remain PRD-only.
- For complex tasks, add `design.md` for technical design and `implement.md` for execution planning before `task.py start`.
# 定位投屏真实连接失败

## 目标

让投屏失败报告反映首次真实错误，并补足 Android 与 ESP32 投屏链路的最小日志，定位握手完成后的首帧、心跳或 ACK 失败。

## 范围

- Android `CastService` 的握手响应、发送、ACK 和重试错误日志。
- ESP32 `/cast` WebSocket 数据帧接收错误日志。
- 保留 ESP-IDF 的 `ws_post_handshake_cb` 握手生命周期，不在 HTTP Upgrade 阶段读取帧。

## 验收标准

- 首次失败原因不会被后续重试覆盖。
- Android 日志能区分握手响应、首帧发送、ACK 超时/不匹配和心跳发送。
- ESP32 日志能记录收到的 WebSocket 帧类型/长度及拒绝原因。
- Android 单元测试、ESP32 profile 构建通过，并完成安装/烧录验证。
