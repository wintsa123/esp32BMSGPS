# Implementation Plan

1. 对 `esp_bms_jbd_feed` 和直接消费者执行 GitNexus upstream impact。
2. 对照上游 `99cf7c1` 修正普通帧与认证帧的长度、校验和重同步处理。
3. 扩展 `tests/jbd_bms_protocol_selftest.c`，覆盖分片、认证帧、未知读取帧、坏长度、坏 CRC 和尾随多帧。
4. 运行目标自测、全套 host selftests、严格编译告警检查和 `git diff --check`。
5. 运行 GitNexus `detect-changes`，更新必要规范并提交归档。

## Rollback

仅协议解析器与测试，不涉及持久化格式或 UI，可单提交回退。
