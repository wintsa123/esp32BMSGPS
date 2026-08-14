# Implementation Plan

1. 对 `controller_chr_cb` 运行 GitNexus upstream impact 并报告风险。
2. 增加最小 source contract 检查，锁定 FFE1 UUID、双能力门槛和同句柄收发语义。
3. 在 FFE0 profile 特征发现中加入 FFE1 单特征分支；保留现有 NUS 与 FFEC/FFEF 分支。
4. 运行目标测试、完整 ESP-IDF build 和 `git diff --check`。
5. 运行 GitNexus compare change detection，核对实际影响范围。
6. 通过固定 RFC2217 端点烧录并监控，验证 FFE1、CCCD、subscription、首个遥测帧、
   “连接...”收敛以及无 panic/watchdog。
7. 运行 Trellis quality check、更新必要规范并提交本任务相关改动，不覆盖用户既有改动。

## Validation Commands

```bash
python3 -m unittest tests.test_controller_ble_source_contract
./scripts/esp-idf-env.sh build
git diff --check
node .gitnexus/run.cjs detect-changes --scope compare --base-ref main
./scripts/esp-idf-env.sh -p "rfc2217://192.168.2.10:4000?ign_set_control" -b 115200 flash
./scripts/esp-idf-env.sh -p "rfc2217://192.168.2.10:4000?ign_set_control" -b 115200 monitor
```
