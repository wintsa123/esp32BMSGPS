# Implementation

## Ordered Checklist

1. 读取 backend/frontend 规范、跨层与复用思考指南；记录工作区基线和相关未提交 diff。
2. 使用 GitNexus `query/context` 理解协议、snapshot、runtime NVS/action、BMS BLE 页面和控制器设置流程。
3. 对每个拟修改的函数/方法运行 upstream `impact`，向用户报告 blast radius；HIGH/CRITICAL 在编辑前明确警告。
4. 先扩展 `tests/fardriver_protocol_selftest.c`，锁定 `D2/D3/D4`、周长、传动比、精确车速、fallback、CRC 和边界失败行为。
5. 修改 `esp_fardriver_protocol` 状态合同与派生逻辑，使协议自检通过。
6. 扩展 `esp_bms_lvgl_ui.h` snapshot/action 合同，追加 action 数值并补绝对值/来源字段；在 contract 中要求 roller。
7. 扩展 runtime NVS 键、兼容加载/保存、restore defaults、snapshot 投影、控制器参数同步去重与新 action 处理。
8. 将 BMS BLE 页面参数化为 BMS/Controller 数据源，复用列表、刷新、确认、稳定候选缓存与返回逻辑，移除控制器内嵌候选列表。
9. 将控制器设置实现为 ROOT/BLE_LIST/TIRE_EDIT/RATIO_EDIT 状态；实现 2/6 行、离线 disabled、控制器同步只读、三个规格 roller、一个 ratio roller、确认一次提交与取消丢弃。
10. 启用 `CONFIG_LV_USE_ROLLER`，补齐中文字体字符并删除所有 `[settings-diag]` 日志。
11. 运行针对性协议自检、字库缺字审计和相关静态检查；修复发现的问题。
12. 运行 `trellis-check` 全量质量门、`git diff --check` 和完整 ESP-IDF build。
13. 运行 GitNexus `detect_changes --scope compare --base-ref refs/heads/main`，确认只影响预期符号/执行流。
14. 使用固定 RFC2217 桥刷写并监控启动、控制器扫描/连接、参数同步与重启持久化；记录设备不可提供的真实控制器步骤。
15. 根据实现中形成的可执行约定更新 `.trellis/spec/`，完成 Trellis 收尾；不自动提交用户现有工作区改动。

## Validation Commands

```bash
cc -std=c11 -Wall -Wextra -Werror \
  -Icomponents/esp_fardriver_protocol/include \
  tests/fardriver_protocol_selftest.c \
  components/esp_fardriver_protocol/esp_fardriver_protocol.c \
  -lm -o /tmp/fardriver_protocol_selftest
/tmp/fardriver_protocol_selftest

rg -n "\[settings-diag\]" components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c
git diff --check
./scripts/esp-idf-env.sh build
node .gitnexus/run.cjs detect_changes --scope compare --base-ref refs/heads/main
```

RFC2217 验证按项目 `esp32-lan-rfc2217-flash` skill 的命令执行。

## Review Gates

- 协议自检通过后才进入 runtime/NVS。
- runtime load/save/default/snapshot/action 完成并审查后才进入 UI。
- UI 不得复制第二套 BLE 页面渲染器，也不得在 snapshot 高频路径重建对象树。
- 发现 HIGH/CRITICAL blast radius 或工作区 hunk 重叠时，先报告再编辑。
- 完整 build 和 GitNexus compare 均通过后才允许刷写。

## Rollback Points

- 协议与自检作为第一独立回滚点。
- runtime 新 NVS 键保持可选，回滚时旧键仍可启动。
- UI BLE adapter 与 roller 子状态集中，出现回归时可单独回退，不影响已有控制器首页。

## Verification Results

- Host FarDriver self-test passes with `-Wall -Wextra -Werror -lm`.
- Font audit reports zero missing Han glyphs in `settings_zh_10/13/16.c`; no
  `[settings-diag]` logs remain.
- `git diff --check` and the complete ESP-IDF 5.5.4 build pass. Final image is
  `0x147c50`; the smallest app partition has `0x983b0` bytes (32%) free.
- GitNexus compare against `refs/heads/main` maps 23 dirty workspace files,
  124 symbols, and 56 flows at CRITICAL risk; the scope includes pre-existing
  controller dashboard/settings work as well as this task. Cycle check is
  clean (`0` cycles).
- RFC2217 flash to `100.118.146.11:4000` succeeds with all hashes verified;
  target MAC is `20:e7:c8:5f:ab:a4`. Boot loads old NVS, initializes LVGL and
  touch, leaves about 126 KB idle heap after display settings, and shows no
  panic/watchdog/reset during an additional 10-second monitor window.
- Not exercised remotely: physical TFT taps/edge-back gestures, a real
  FarDriver advertisement/connection, parameter synchronization, and reboot
  persistence after confirming new roller values. Keep these as hardware
  interaction acceptance items.
