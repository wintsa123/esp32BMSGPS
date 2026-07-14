# 实施清单

## 1. 契约与算法

- [ ] 在 LVGL 公共头增加 action、snapshot 字段、默认/上限常量并更新尺寸断言。
- [ ] 在 `esp_bms_speed_dashboard` 增加双阶段纯 C helper。
- [ ] 扩展 `tests/speed_dashboard_selftest.c` 覆盖算法边界和单位独立性。

## 2. Runtime 与 NVS

- [ ] 在 reset/default、load、save、restore defaults 路径接入 `preset_range_km=100`。
- [ ] 缺少新 NVS key 时兼容旧设备并安排持久化。
- [ ] 在 `runtime_update_snapshot_speed()` 用公制电耗和 BMS/SOC 有效状态计算 snapshot 结果。
- [ ] 在 action dispatcher/runtime apply 路径处理绝对预设里程并复用现有保存请求。

## 3. 正式 LVGL UI

- [ ] 将已确认的 BMS header/status/SOC/battery/range 布局迁入创建与旋转函数。
- [ ] 在 `set_dashboard()` 更新剩余里程文本和 BMS/GPS 条件显隐。
- [ ] 在 BMS 设置详情增加预设里程行和四位 roller 编辑页。
- [ ] 同步速度单位三级页、固定导航、控制器卡片和“控制器连接”文案。
- [ ] 调整一级设置横屏 52px 行高和标题/副标题整体居中。

## 4. 模拟器与字体

- [ ] 更新模拟器默认快照、命令和 action 处理以覆盖新契约。
- [ ] 移除 `range_preview.c`/`simulator_zh_13.c` 的构建与调用路径。
- [ ] 从正式 UI 中文字重新生成 `settings_zh_10/13/16.c`。

## 5. 验证

- [ ] 编译并运行 speed dashboard、GPS stream、FarDriver 主机自测。
- [ ] 运行横竖屏 headless 模拟器并保存/检查 `preview/` 图片。
- [ ] 检查 BMS/GPS 失效、SOC fallback、实测切换、roller 保存/取消/返回。
- [ ] 运行字体缺字检查和 `git diff --check`。
- [ ] 运行 `./scripts/esp-idf-env.sh build`。
- [ ] 运行 GitNexus `detect-changes -r esp32BMSGPS --scope compare --base-ref main`。
- [ ] 通过 RFC2217 刷写并监控启动、NVS 和 panic/WDT 日志。

## Rollback Points

- 完成第 2 节后先跑纯 C 自测，失败时不进入 UI 修改。
- 完成第 3 节后先跑模拟器，布局不稳定时不生成字体或刷写。
- ESP-IDF 构建或 flash 失败时保留上一个已验证阶段，不改动硬件桥配置。
