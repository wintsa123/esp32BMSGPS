# Implementation Plan

1. 读取 backend/frontend 相关规范与三份 research，复核用户未提交 diff；不得覆盖 snapshot、cycle capacity、advertising default 或 UI 改动。
2. 对实际将修改的每个函数/方法运行 GitNexus upstream `impact`；已知 `settings_bms_ble_refresh_rows` 与 `runtime_project_controller_snapshot` 为 CRITICAL，先报告影响范围并保留针对性回归门禁。
3. 先补最小可运行检查：名称全输入归一化/JSON 转义、0/6/7/12 分页与绝对索引、Controller discovering/subscribed/首个有效帧/超时/disconnect 投影、NUS 与 FFE0 profile 命令合同。
4. 将候选上限扩为 12，更新 snapshot ABI assertion和依赖该上限的静态缓存；不改变候选结构或动态分配。
5. 在共享 BLE 页面增加单一 more-page 状态、`More devices` 入口、分页渲染/点击映射及两级返回；BMS/Controller 继续复用同一实现。
6. 修正三条名称归一化路径，保留按 MAC 名称合并；给 BMS HTTP candidates 增加有界 JSON 转义和失败路径检查。
7. 在 Controller BLE 中恢复 NUS 128 位 profile 与五字节只读轮询，保留 FFE0 profile/open/keepalive 作为回退；补齐 service/characteristic/CCCD/subscribe/disconnect 低频日志。
8. 将 Controller online snapshot 改为协议 ready 语义：订阅成功只开始命令轮询和首帧超时，首个实际更新仪表遥测有效位的 FarDriver 帧后才进入 ONLINE、播放提示并展开专属设置；仅参数块不算 ready，超时主动断开并记录 profile/阶段，不修改 LVGL 可见性规则。
9. 运行格式/静态检查、`./scripts/run-host-selftests.sh`、可用的目标检查和 `./scripts/esp-idf-env.sh build`；若 `simulator/` 仍缺失，记录 headless simulator 不可运行而不顺带重建。
10. 运行 `git diff --check` 和 `node .gitnexus/run.cjs detect_changes --scope compare --base-ref main`，核对只影响计划内符号/流程。
11. 关闭其他串口客户端，通过 `rfc2217://192.168.2.10:4000?ign_set_control`、115200 完成一次 flash 与 monitor；检查启动、扫描、候选名称、profile 选择、订阅、遥测、断连原因、heap/最大块及无 panic/watchdog。
12. 由独立 Trellis check 代理复核规范、数据流、测试和用户改动保留情况；修复发现后重复相关门禁。

## Validation Commands

```bash
./scripts/run-host-selftests.sh
./scripts/esp-idf-env.sh build
git diff --check
node .gitnexus/run.cjs detect_changes --scope compare --base-ref main
./scripts/esp-idf-env.sh -p "rfc2217://192.168.2.10:4000?ign_set_control" -b 115200 flash
./scripts/esp-idf-env.sh -p "rfc2217://192.168.2.10:4000?ign_set_control" -b 115200 monitor
```

## Rollback Points

- 分页/容量与 Controller profile/ready 分两组提交前 diff 检查，任一组失败可单独撤销本任务对应改动，不触碰用户原有修改。
- 不执行 NVS erase、分区变更或破坏性 Git 操作。
