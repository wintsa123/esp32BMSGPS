# Implementation Plan: GPS 协议校验与 A-GNSS 注入修复

1. 在编辑前加载 `trellis-before-dev`，读取 backend firmware 规范，并对计划修改的每个函数执行 GitNexus upstream impact；HIGH/CRITICAL 风险先报告。
2. 修改 `runtime_parse_rmc()`，要求唯一且位于行尾的 `*HH`；补充缺失、截断、尾随字符和合法校验和测试。
3. 将 CASBIN 最大载荷调整为当前白名单的协议上限，并在 C 与 Python 中实现一致的 class/id/长度验证，包括 `MSG-IGP` 内部长度字段。
4. 将 `/api/gps/agnss` 改为单帧完整验证后写 UART，同时让 helper 预验证整个流并逐帧 POST。
5. 修正 MON-SEC 日志状态机，分别处理 unknown、clear 和 alert，保持限频与原始数值汇总。
6. 检查新增 parser/validator 不引用 ESP-IDF target 或 PSRAM API；固定缓冲区只按支持的 CASBIN 最大帧定义。
7. 扩展最小自测：CASBIN 最大合法 IGP、错误固定长度、IGP 长度不一致、连续帧、坏校验和和 HTTP/helper 分帧行为。
8. 运行：
   - `cc -std=c11 -Wall -Wextra -Wpedantic -Werror ... gps_stream_selftest.c`
   - `python3 scripts/push-agnss.py --self-test`
   - `git diff --check`
   - `./scripts/esp-idf-env.sh build`
9. 加载 `esp32-lan-rfc2217-flash`，通过固定 RFC2217 地址刷写并监视 ATGM336H-6N-74：确认 RMC 10 Hz、PPS、A-GNSS、CFG-JSM/MON-SEC 以及无 GPS 时的降级行为。
10. 运行 `trellis-check` 和 `node .gitnexus/run.cjs detect-changes -r esp32BMSGPS`，核对只影响预期 GPS 执行流；实施完成后再进入 finish/commit 流程。

## Risk And Rollback Points

- `runtime_feed_gps_byte()` upstream risk 已知为 HIGH；混流回归会影响主 runtime tick，必须保留 NMEA+CASBIN 连续流测试。
- 增大 CASBIN 帧结构会增加 runtime 静态内存和 HTTP/main task 栈占用；构建后检查 map/size，实机观察最小堆。
- HTTP 行为变化必须与仓库内 helper 同步提交；任一端单独回滚都会导致注入失败。
- 不为未来 PSRAM 型号提前增加第二套批量实现；单帧协议保持稳定，批量能力在有实际目标和性能数据后独立增加。
- 不执行 `task.py start`，直到用户审阅最终规划并明确批准开始实施。
