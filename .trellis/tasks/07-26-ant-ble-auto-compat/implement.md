# 实施计划：ANT BLE 自动协议兼容

1. 使用 GitNexus 对将修改的协议解析与 BLE 状态机符号执行上游影响分析；读取嵌入式层规范和现有 BMS 自检接线。
2. 扩展 ANT 纯 C 协议模块，保留新版解码行为，增加旧版固定 140 字节流组帧、校验、只读请求和遥测映射。
3. 新增 `tests/ant_bms_protocol_selftest.c`，使用新旧上游黄金帧覆盖请求、字段、分包、粘包和损坏帧。
4. 在 BLE 传输层增加仅内存保存的 ANT 协议识别状态：订阅后新版探测、受限超时后旧版探测、校验成功后锁定并按该协议轮询；连接重置时清理状态。
5. 接入主机自检脚本，执行格式化、主机自检和 ESP-IDF 构建；完成 GitNexus 变更影响检查。除非用户明确要求，不烧录硬件。

## 风险与回滚

- `esp_bms_bms_ble.c` 是所有品牌共享的 BLE 状态机；修改仅限 `bms_type=ant` 分支，其他品牌逻辑和 UUID 不变。
- 新旧协议共享 UUID，探测错误会导致无法更新数据而非错误数据，因此只以完整校验帧锁定。
- 需要保留现有新版轮询帧与字段偏移不变；回滚点是移除识别分支和旧解析器，恢复单一新版路径。

## 验收命令

```bash
./scripts/run-host-selftests.sh
./scripts/esp-idf-env.sh build
node .gitnexus/run.cjs detect-changes -r esp32BMSGPS
```
