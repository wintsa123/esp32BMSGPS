# 实施清单

## 1. 契约与算法

- [x] 在 LVGL 公共头增加 action、snapshot 字段、默认/上限常量并更新尺寸断言。
- [x] 在 `esp_bms_speed_dashboard` 增加双阶段纯 C helper。
- [x] 扩展 `tests/speed_dashboard_selftest.c` 覆盖算法边界和单位独立性。

## 2. Runtime 与 NVS

- [x] 在 reset/default、load、save、restore defaults 路径接入 `preset_range_km=100`。
- [x] 缺少新 NVS key 时兼容旧设备并安排持久化。
- [x] 在 `runtime_update_snapshot_speed()` 用公制电耗和 BMS/SOC 有效状态计算 snapshot 结果。
- [x] 在 action dispatcher/runtime apply 路径处理绝对预设里程并复用现有保存请求。

## 3. 正式 LVGL UI

- [x] 将已确认的 BMS header/status/SOC/battery/range 布局迁入创建与旋转函数。
- [x] 在 `set_dashboard()` 更新剩余里程文本和 BMS/GPS 条件显隐。
- [x] 在 BMS 设置详情增加预设里程行和四位 roller 编辑页。
- [x] 同步速度单位三级页、固定导航、控制器卡片和“控制器连接”文案。
- [x] 调整一级设置横屏 52px 行高和标题/副标题整体居中。

## 4. 模拟器与字体

- [x] 更新模拟器默认快照、命令和 action 处理以覆盖新契约。
- [x] 移除 `range_preview.c`/`simulator_zh_13.c` 的构建与调用路径。
- [x] 从正式 UI 中文字重新生成 `settings_zh_10/13/16.c`。

## 5. 验证

- [x] 编译并运行 speed dashboard、GPS stream、FarDriver 主机自测。
- [x] 运行横竖屏 headless 模拟器并保存/检查 `preview/` 图片。
- [x] 检查 BMS/GPS 失效、SOC fallback、实测切换、roller 保存/取消/返回。
- [x] 运行字体缺字检查和 `git diff --check`。
- [x] 运行 `./scripts/esp-idf-env.sh build`。
- [x] 运行 GitNexus `detect-changes`；`main` 与目录同名时使用明确基线引用。
- [x] 通过 RFC2217 刷写并监控启动、NVS 和 panic/WDT 日志。

## 验证记录

- 主机三项自测、横竖屏 headless 与正式 UI 截图均通过；三套字体均为 0 缺字。
- ESP-IDF 5.5.4 构建与 RFC2217 刷写成功，60 秒 GPS 汇总为 `fix=1`、`parse_errors=0`。
- 只读 NVS 转储确认 `esp_bms:preset_rng = 100`；监控窗口内无 panic/WDT。
- 当前真机 BMS 扫描候选为 0，未能执行真实 BMS 断连/重连交互；模拟器已覆盖 BMS/GPS 显隐切换。

## Rollback Points

- 完成第 2 节后先跑纯 C 自测，失败时不进入 UI 修改。
- 完成第 3 节后先跑模拟器，布局不稳定时不生成字体或刷写。
- ESP-IDF 构建或 flash 失败时保留上一个已验证阶段，不改动硬件桥配置。
