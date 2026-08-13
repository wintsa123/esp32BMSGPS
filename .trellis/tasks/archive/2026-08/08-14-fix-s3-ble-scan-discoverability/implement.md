# Implementation Plan

1. 在 `bms_gap_event()` 中保留现有名称解析，扫描激活时始终调用现有 candidate store；有名称传名称，无名称传 `NULL`。
2. 在 `controller_scan_gap_event()` 中做同样的最小恢复，继续由现有 store 处理缓存、去重与 snapshot 投影。
3. 在 `runtime_reset_state()` 中将 `BLUETOOTH_ADVERTISE_REQUESTED` 默认值恢复为 `false`，保留设置 action 和 HID 服务逻辑。
4. 增加最小可运行回归检查，覆盖 ASCII/中文/无名候选都可入表，以及 HID 开启时启动广告请求仍为关闭；优先复用现有 host/simulator smoke，避免建立新的测试框架。
5. 运行格式与静态检查、`./scripts/run-host-selftests.sh`、`python3 -m unittest tests/test_ble_host_bridge.py`、可用的 LVGL simulator headless smoke、S3 profile 构建和 `git diff --check`。
6. 运行 `node .gitnexus/run.cjs detect-changes -r esp32BMSGPS --scope compare --base-ref main`，确认生产影响限于预期 BLE 扫描与 runtime 初始化流程。
7. 使用项目固定 RFC2217 流程烧录匹配 ESP32-S3，验证冷启动不可发现、手动开启/关闭、BMS/Controller 列表和运行稳定性。

## Risk And Rollback

- `runtime_reset_state` 静态风险为 HIGH；若启动或设置行为异常，仅回退广告请求默认值这一行。
- NimBLE 回调静态风险被低估；若硬件扫描异常，分别回退 BMS 或 Controller 的入表调用条件并保留日志定位。
- 不覆盖工作区已有修改；实施与检查只评估本任务新增差异。
