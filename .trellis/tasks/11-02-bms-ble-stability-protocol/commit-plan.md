# 提交计划（用户已确认）

用户已明确要求“合并到main，推送到github”。保持以下两批提交及文件范围；提交使用显式路径限制，保留无关改动与暂存内容，然后推送 origin/main。README已由其他提交59106d37进入main，本任务不重复修改。

## 1. fix: 改善 BMS BLE 恢复并补齐 ANT 与 EM02 协议

包含产品实现、对应测试、执行入口、协议文档和契约：

- `.trellis/spec/backend/bms-link-and-telemetry.md`
- `.trellis/spec/backend/index.md`
- `components/esp_bms_bms_ble/esp_bms_bms_ble.c`
- `components/esp_bms_bms_ble/protocols/ant/esp_bms_ant_protocol.c`
- `components/esp_bms_bms_ble/protocols/ant/esp_bms_ant_protocol.h`
- `components/esp_bms_bms_ble/protocols/bluebabe/esp_bms_bluebabe_protocol.c`
- `components/esp_bms_bms_ble/protocols/em02/esp_bms_em02_protocol.c`
- `components/esp_bms_bms_ble/protocols/em02/esp_bms_em02_protocol.h`
- `components/esp_bms_bms_ble/protocols/esp_bms_bms_protocol.h`
- `components/esp_bms_bms_ble/protocols/jk/esp_bms_jk_protocol.c`
- `components/esp_bms_bms_ble/protocols/mingtang/esp_bms_mingtang_protocol.c`
- `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c`
- `components/esp_bms_idf_runtime/include/esp_bms_idf_runtime.h`
- `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui_internal.h`
- `components/esp_bms_lvgl_ui/include/esp_bms_lvgl_ui.h`
- `docs/protocols/cross-verification.md`
- `docs/protocols/motexa-baseline.md`
- `main/web/index.html`
- `scripts/run-host-selftests.sh`
- `tests/ant_bms_protocol_selftest.c`
- `tests/bluebabe_bms_protocol_selftest.c`
- `tests/em02_bms_protocol_selftest.c`
- `tests/jk_bms_protocol_selftest.c`
- `tests/mingtang_bms_protocol_selftest.c`
- `tests/test_bms_ble_recovery.py`
- `tests/test_bms_config_runtime.py`
- `tests/test_bms_telemetry_projection.py`

## 2. docs: 记录 BMS 稳定性改进与验证结果

仅本任务目录的计划、研究、上下文和验证记录：

- `.trellis/tasks/11-02-bms-ble-stability-protocol/check.jsonl`
- `.trellis/tasks/11-02-bms-ble-stability-protocol/commit-plan.md`
- `.trellis/tasks/11-02-bms-ble-stability-protocol/design.md`
- `.trellis/tasks/11-02-bms-ble-stability-protocol/implement.jsonl`
- `.trellis/tasks/11-02-bms-ble-stability-protocol/implement.md`
- `.trellis/tasks/11-02-bms-ble-stability-protocol/prd.md`
- `.trellis/tasks/11-02-bms-ble-stability-protocol/research/check-report.md`
- `.trellis/tasks/11-02-bms-ble-stability-protocol/research/findings.md`
- `.trellis/tasks/11-02-bms-ble-stability-protocol/research/firmware-build.log`
- `.trellis/tasks/11-02-bms-ble-stability-protocol/research/firmware-flash.log`
- `.trellis/tasks/11-02-bms-ble-stability-protocol/research/graph-changes-full.json`
- `.trellis/tasks/11-02-bms-ble-stability-protocol/research/graph-changes-full.log`
- `.trellis/tasks/11-02-bms-ble-stability-protocol/research/graph-changes.log`
- `.trellis/tasks/11-02-bms-ble-stability-protocol/research/host-selftests-final.log`
- `.trellis/tasks/11-02-bms-ble-stability-protocol/research/host-selftests.log`
- `.trellis/tasks/11-02-bms-ble-stability-protocol/research/impact-json-u8.log`
- `.trellis/tasks/11-02-bms-ble-stability-protocol/research/impact-main.log`
- `.trellis/tasks/11-02-bms-ble-stability-protocol/research/transport-callers.txt`
- `.trellis/tasks/11-02-bms-ble-stability-protocol/research/transport-impact.jsonl`
- `.trellis/tasks/11-02-bms-ble-stability-protocol/research/validation-final.md`
- `.trellis/tasks/11-02-bms-ble-stability-protocol/task.json`

## 排除：已有或其他并行工作

这些路径不纳入上述任何提交，不修改其暂存状态：

- `.trellis/spec/backend/hardware-build-flash.md`
- `.trellis/workspace/wintsa/index.md`
- `.trellis/workspace/wintsa/journal-1.md`
- `.reasonix/tasks/50e9f0e3d7419484aee3459ee7991420--bash-6/`
- `.reasonix/tasks/50e9f0e3d7419484aee3459ee7991420--bash-7/`
- `.reasonix/tasks/988b0c9040d7a037ced9259490f1fe66--bash-1/`
- `.reasonix/tasks/988b0c9040d7a037ced9259490f1fe66--bash-2/`
- `.reasonix/tasks/988b0c9040d7a037ced9259490f1fe66--bash-3/`
- `.reasonix/tasks/988b0c9040d7a037ced9259490f1fe66--bash-4/`
- `.reasonix/tasks/988b0c9040d7a037ced9259490f1fe66--bash-5/`
- `.trellis/tasks/archive/2026-10/`
- `broad/pcb-revisions/`

任务暂不归档；真机验证受TCP端点超时阻塞。后续恢复硬件连接时再继续设备验证。
