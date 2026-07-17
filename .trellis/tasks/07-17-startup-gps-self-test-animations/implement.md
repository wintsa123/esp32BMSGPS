# Implementation Plan

## 1. Contracts and runtime state

- [ ] 在 `esp_bms_lvgl_ui.h` 增加 GPS 模块状态、启动动画枚举、snapshot 字段、新 action 和 boot API。
- [ ] 在 runtime state 中初始化探测状态，并实现统一的 GPS 状态转换、查询和启动探测 finalize API。
- [ ] 用有效 NMEA checksum 或 CASBIN 完整帧标记模块可用；UART 初始化失败/启动超时标记不可用，保留后续恢复。
- [ ] 更新速度来源派生与 action 双层校验，不改变速度单位换算和滤波。
- [ ] A-GNSS 接口按模块能力返回 503。

## 2. Persistence and startup orchestration

- [ ] 增加 `boot_anim` optional NVS 读取、校验、保存和恢复默认行为。
- [ ] 把显示设置读取提前到 display 初始化前，确保旋转、亮度和启动风格首次呈现即正确。
- [ ] 在主循环前运行两套动画共用的 3 秒 GPS 探测循环，并启动已配置的 BMS/控制器 BLE。
- [ ] 动画结束时提交真实 snapshot 并进入电池首页。

## 3. LVGL behavior

- [ ] 实现未来科技电量充能 overlay，所有动态文本使用持久缓冲和 ASCII。
- [ ] 实现基于现有 S1000RR/Fireblade 页的扫表更新，不复制仪表绘制代码。
- [ ] 在系统设置增加启动动画选择页并发出 committed numeric action。
- [ ] GPS 不可用时禁用速度来源行；区分 `GPS OFF` 与未定位 `GPS --`。
- [ ] 根据 `GPS AVAILABLE || CONTROLLER ONLINE` 隐藏/显示速度页并压缩轮播索引；处理当前页退出、投屏页位置和稳定数据源。

## 4. Simulator and tests

- [ ] 扩展 simulator snapshot/action/命令以覆盖 GPS 能力和 boot style。
- [ ] 增加纯 C 自测覆盖模块状态转换与 GPS/控制器来源选择，或把可测试策略提取为无硬件 helper。
- [ ] 运行现有 GPS、速度仪表、FarDriver 等自测。
- [ ] 运行横屏和竖屏 headless simulator；生成/更新的截图只放在仓库根 `preview/`。
- [ ] 运行 ESP-IDF build，检查 RAM/Flash 增量和无新警告。

## 5. Review and hardware validation

- [ ] 运行 `gitnexus detect-changes --scope compare --base-ref main` 并核对受影响流程。
- [ ] 使用项目 RFC2217 流程刷写硬件并监控日志。
- [ ] 有 GPS：确认 `AVAILABLE`、无 fix 不误判、两套扫表主题可完成。
- [ ] 无 GPS：确认 `UNAVAILABLE`、设置禁用、速度页不渲染、投屏页无空白间隔。
- [ ] 控制器上线/离线：确认速度页动态加入/退出。

## Validation Commands

```bash
./scripts/run-host-selftests.sh
./scripts/run-lvgl-simulator.sh --headless
./scripts/run-lvgl-simulator.sh --headless --portrait
idf.py build
gitnexus detect-changes --scope compare --base-ref main --repo esp32BMSGPS
```

## Rollback Points

1. 完成 runtime capability contract 后先跑 host selftests；失败则不进入 UI 修改。
2. 完成 carousel 压缩后先跑双方向 simulator；出现手势回归则回退索引压缩而保留 GPS 状态。
3. firmware build 通过后再刷写；硬件动画异常可只移除 boot orchestration 调用，不影响 GPS 降级状态。
