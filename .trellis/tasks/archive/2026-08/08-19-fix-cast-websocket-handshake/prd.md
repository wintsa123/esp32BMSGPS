# 修复投屏 WebSocket 握手阻塞

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
# 修复投屏 WebSocket 握手阻塞

## 目标

修复 ESP32 `/cast` WebSocket handler 在 HTTP Upgrade 握手请求阶段提前读取 WebSocket 数据帧，导致 Android 与 ESP32 互相等待、握手超时的问题。

## 范围

- ESP32 投屏 WebSocket handler 的握手分支。
- 不改变现有投屏帧协议、JPEG 编解码、ACK 格式或 Android 端行为。

## 验收标准

- 初始 `HTTP_GET` Upgrade 请求不调用 `httpd_ws_recv_frame()`，handler 正常返回 `ESP_OK`。
- 后续 WebSocket 请求仍执行现有 binary frame、heartbeat、JPEG 和 ACK 流程。
- 固件组件可通过格式化/编译检查。
- 不修改用户已有的 `AGENTS.md`、`CLAUDE.md` 改动。
