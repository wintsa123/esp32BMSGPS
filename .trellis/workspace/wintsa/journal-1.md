# Journal - wintsa (Part 1)

> AI development session journal
> Started: 2026-07-04

---



## Session 1: ESP32 GPS TFT BMS firmware wrap-up

**Date**: 2026-07-05
**Task**: ESP32 GPS TFT BMS firmware wrap-up
**Branch**: `main`

### Summary

Added TFT bring-up diagnostics, hardened local HTTP/Web auth and PNA behavior, generated GitNexus project guidance, and validated host plus ESP target builds.

### Main Changes

(Add details)

### Git Commits

| Hash | Message |
|------|---------|
| `7427ad4` | (see git log) |
| `7b72cf0` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 2: Fix TFT rotation touch language regression

**Date**: 2026-07-05
**Task**: Fix TFT rotation touch language regression
**Branch**: `main`

### Summary

Stabilized TFT landscape boot, added XPT2046 touch fallback/diagnostics, preserved ASCII language labels, and validated host plus ESP builds.

### Main Changes

(Add details)

### Git Commits

| Hash | Message |
|------|---------|
| `6311856` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 3: Fix TFT Chinese UI and touch diagnostics

**Date**: 2026-07-05
**Task**: Fix TFT Chinese UI and touch diagnostics
**Branch**: `main`

### Summary

Added a minimal TFT Chinese bitmap path, switched static settings labels to Chinese, restored IRQ-first XPT2046 tap reads, and added low-rate raw touch diagnostics.

### Main Changes

(Add details)

### Git Commits

| Hash | Message |
|------|---------|
| `e246bd4` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 4: Full Source C Memory And Bit Optimization

**Date**: 2026-07-09
**Task**: Full Source C Memory And Bit Optimization
**Branch**: `main`

### Summary

Optimized ESP-IDF runtime/UI C state storage with explicit flag masks, verified build and RFC2217 flash, archived the completed optimization task.

### Main Changes

(Add details)

### Git Commits

| Hash | Message |
|------|---------|
| `ccb444c` | (see git log) |
| `80ee6bc` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 5: Complete system settings and touch calibration

**Date**: 2026-07-11
**Task**: Complete system settings and touch calibration
**Branch**: `main`

### Summary

Completed System settings subpages and persistent four-point touch calibration; verified previews, calibration math, ESP-IDF build, GitNexus scope, RFC2217 flash, boot-time NVS load, and user-confirmed hardware behavior.

### Main Changes

(Add details)

### Git Commits

| Hash | Message |
|------|---------|
| `15bdc23` | (see git log) |
| `54d09f0` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 6: Unify settings list card style

**Date**: 2026-07-11
**Task**: Unify settings list card style
**Branch**: `main`

### Summary

Unified root and secondary TFT settings lists into inset gray cards on black backgrounds, centered row content and affordances, built and flashed the firmware, and verified clean startup.

### Main Changes

(Add details)

### Git Commits

| Hash | Message |
|------|---------|
| `8a33b12` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 7: Quick Panel Slide Lock

**Date**: 2026-07-11
**Task**: Quick Panel Slide Lock
**Branch**: `main`

### Summary

Added a primitive-drawn quick-panel lock icon, full-screen interaction guard, frozen carousel while locked, tap-to-show large slide unlock control, 3-second timeout, rebuild-safe timer lifecycle, previews, build and RFC2217 hardware validation.

### Main Changes

(Add details)

### Git Commits

| Hash | Message |
|------|---------|
| `112c04d` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 8: 统一速度仪表 V4 与速度来源选择

**Date**: 2026-07-13
**Task**: 统一速度仪表 V4 与速度来源选择
**Branch**: `main`

### Summary

完成统一速度仪表横竖屏布局、中文温度缩写、默认挡位、BMS 离线隐藏电池与电耗、完整 LVGL 预览、ESP-IDF 构建与 RFC2217 真机验证；GPS 室外定位成功且 PPS 稳定。

### Main Changes

- Detailed change bullets were not supplied; see the summary above.

### Git Commits

| Hash | Message |
|------|---------|
| `6de4c66` | (see git log) |
| `ee02e28` | (see git log) |

### Testing

- Validation was not recorded for this session.

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 9: 优化 S1000RR 速度色带曲线

**Date**: 2026-07-14
**Task**: 优化 S1000RR 速度色带曲线
**Branch**: `main`

### Summary

将速度色带贝塞尔等价路径采样由 32 提高到 48 段，使用 4 px 切线重叠消除宽线尖缝；完成 13 状态 LVGL 预览、主机测试、ESP-IDF 构建、GitNexus 检测及 RFC2217 刷写启动验证。任务保留现场目视和拖动耗时确认。

### Main Changes

- Detailed change bullets were not supplied; see the summary above.

### Git Commits

| Hash | Message |
|------|---------|
| `8092157` | (see git log) |

### Testing

- Validation was not recorded for this session.

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 10: 正式 UI 与剩余里程

**Date**: 2026-07-14
**Task**: 正式 UI 与剩余里程
**Branch**: `main`

### Summary

迁入正式 BMS/设置 UI，新增预设里程持久化与双阶段剩余里程算法；完成字体、模拟器、主机测试、ESP-IDF 构建、RFC2217 刷写和 NVS 验证。

### Main Changes

- Detailed change bullets were not supplied; see the summary above.

### Git Commits

| Hash | Message |
|------|---------|
| `e4f01f2` | (see git log) |
| `e67b356` | (see git log) |

### Testing

- Validation was not recorded for this session.

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 11: 功能组件拆分与中文定制化编译脚本

**Date**: 2026-07-21
**Task**: 功能组件拆分与中文定制化编译脚本
**Branch**: `main`

### Summary

实现每次无参数启动的中英文语言选择与 --lang 覆盖；补齐 Bash、PowerShell、CMD 入口及包装编译脚本本地化，增加无效语言重试回归。完成 network/OTA on-off 三组 ESP-IDF 构建和 ELF 闭包验证；发现 app_update 为 ESP-IDF 基础依赖，OTA-off 不含 BMS OTA 实现符号。RFC2217 刷写因远端拒绝参数协商未写入设备。

### Main Changes

- Detailed change bullets were not supplied; see the summary above.

### Git Commits

| Hash | Message |
|------|---------|
| `c64a4c2` | (see git log) |

### Testing

- Validation was not recorded for this session.

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 12: 修复 ESP32 启动期 WDT

**Date**: 2026-07-23
**Task**: 修复 ESP32 启动期 WDT
**Branch**: `main`

### Summary

移除 bluetoothon、wlanJZ、hotspoton 三个 LVGL 图标字体的自引用 fallback，避免缺字形查找在 esp_timer 中形成无限回退并饿死 CPU0 IDLE0。legacy profile 构建、配置器/主机自测、LVGL headless smoke 均通过；RFC2217 刷写校验成功，冷启动约 2.7 秒完成显示路径，140 秒监控窗口无 task_wdt、panic 或重启。

### Main Changes

- Detailed change bullets were not supplied; see the summary above.

### Git Commits

| Hash | Message |
|------|---------|
| `d4fca3d` | (see git log) |

### Testing

- Validation was not recorded for this session.

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 13: 速度来源编译裁剪

**Date**: 2026-07-25
**Task**: 速度来源编译裁剪
**Branch**: `main`

### Summary

速度来源三级页按 GPS/控制器 feature 裁剪，GPS-only 保持可进入三级页；确认 ASCII 固件版本链路为 profile 到状态 API，未发现字符库问题。主机与配置器自测通过，ESP-IDF profile 构建未产生镜像，未刷写真机。

### Main Changes

- Detailed change bullets were not supplied; see the summary above.

### Git Commits

| Hash | Message |
|------|---------|
| `c0d87db` | (see git log) |

### Testing

- Validation was not recorded for this session.

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 14: ESP32 LVGL dual-core frame-rate optimization

**Date**: 2026-07-25
**Task**: ESP32 LVGL dual-core frame-rate optimization
**Branch**: `main`

### Summary

Enabled two LVGL software draw units on dual-core targets, pinned the adapter worker to Core 1, preserved the C3 single-core override, and validated ESP32/S3 builds plus simulator smoke tests.

### Main Changes

- Detailed change bullets were not supplied; see the summary above.

### Git Commits

| Hash | Message |
|------|---------|
| `5ef2184` | (see git log) |

### Testing

- Validation was not recorded for this session.

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 15: PSRAM LVGL 帧率优化

**Date**: 2026-07-26
**Task**: PSRAM LVGL 帧率优化
**Branch**: `main`

### Summary

为 ESP32-S3 默认启用 PSRAM LVGL 双缓冲，保留内存容量回退路径，并完成旧 ESP32 与 S3 配置构建及真机日志验证。

### Main Changes

- Detailed change bullets were not supplied; see the summary above.

### Git Commits

| Hash | Message |
|------|---------|
| `00139dcf` | (see git log) |

### Testing

- Validation was not recorded for this session.

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 16: Adjust S3 settings control sizing

**Date**: 2026-07-27
**Task**: Adjust S3 settings control sizing
**Branch**: `main`

### Summary

Scaled settings controls only for S3 logical resolutions, added the 18px settings font, verified simulator and S3 builds, and skipped flashing at user request.

### Main Changes

- Detailed change bullets were not supplied; see the summary above.

### Git Commits

| Hash | Message |
|------|---------|
| `0e182e32` | (see git log) |

### Testing

- Validation was not recorded for this session.

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 17: GPS 一级设置入口

**Date**: 2026-07-27
**Task**: GPS 一级设置入口
**Branch**: `main`

### Summary

新增独立 GPS 一级设置入口与详情页，完成 GPS/控制器裁剪矩阵、横竖屏模拟器、主机自测、ESP-IDF S3 构建及 RFC2217 刷写验证；并确认原生手势返回兼容。

### Main Changes

- Detailed change bullets were not supplied; see the summary above.

### Git Commits

| Hash | Message |
|------|---------|
| `591d4703` | (see git log) |

### Testing

- Validation was not recorded for this session.

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 18: 资源占用诊断日志

**Date**: 2026-07-27
**Task**: 资源占用诊断日志
**Branch**: `main`

### Summary

新增按 UI action 关联的 CPU 与内存资源窗口日志，启用运行时统计配置；ESP32 与 ESP32-S3 构建通过，S3 设备烧录在 stub 阶段阻塞，未完成校验。

### Main Changes

- Detailed change bullets were not supplied; see the summary above.

### Git Commits

| Hash | Message |
|------|---------|
| `6497be2d` | (see git log) |

### Testing

- Validation was not recorded for this session.

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 19: 实现 ANT BMS 真实容量估算

**Date**: 2026-07-27
**Task**: 实现 ANT BMS 真实容量估算
**Branch**: `main`

### Summary

新增 ANT 新旧协议总循环 mAh 解析、基于 SOC 区间的容量估算和 NVS 身份隔离；本地 Web 与 Vercel 设置页显示三态结果。已完成主机自测、Vercel 构建、ESP32-S3 构建、RFC2217 刷写与启动日志验证。

### Main Changes

- Detailed change bullets were not supplied; see the summary above.

### Git Commits

| Hash | Message |
|------|---------|
| `456b5920` | (see git log) |

### Testing

- Validation was not recorded for this session.

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 20: 优化轮播切页黑场过渡

**Date**: 2026-07-28
**Task**: 优化轮播切页黑场过渡
**Branch**: `main`

### Summary

实现黑场标题跟手切页；无头 LVGL 模拟器与 ESP32 profile 构建通过，RFC2217 桥超时而未完成实机确认。

### Main Changes

- Detailed change bullets were not supplied; see the summary above.

### Git Commits

| Hash | Message |
|------|---------|
| `642794e5` | (see git log) |

### Testing

- Validation was not recorded for this session.

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 21: 修复黑场轮播占位页

**Date**: 2026-07-28
**Task**: 修复黑场轮播占位页
**Branch**: `main`

### Summary

将黑场切页从屏幕浮层改为轮播容器内的黑色标题占位页，保持真实页隐藏时的滚动边界；横竖屏无头模拟器、GitNexus 变更映射和 esp32all profile 编译均通过。

### Main Changes

- Detailed change bullets were not supplied; see the summary above.

### Git Commits

| Hash | Message |
|------|---------|
| `1dbabdcf` | (see git log) |

### Testing

- Validation was not recorded for this session.

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 22: 修复模拟器鼠标轮播手势

**Date**: 2026-07-28
**Task**: 修复模拟器鼠标轮播手势
**Branch**: `main`

### Summary

交互式 SDL 模拟器改用 LVGL 指针滚动，鼠标可驱动黑场轮播；无头模式保留设备原生手势回归，并完成横竖屏模拟器与 ESP-IDF 构建验证。

### Main Changes

- Detailed change bullets were not supplied; see the summary above.

### Git Commits

| Hash | Message |
|------|---------|
| `0abbb47a` | (see git log) |

### Testing

- Validation was not recorded for this session.

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 23: 修复 FarDriver 无响应轮询

**Date**: 2026-07-28
**Task**: 修复 FarDriver 无响应轮询
**Branch**: `main`

### Summary

FarDriver Nordic UART 只读轮询改为无响应写；ESP-IDF profile 构建和 RFC2217 刷写通过。设备 NVS 未保存 ctl_mac，需重新绑定后确认实时遥测。

### Main Changes

- Detailed change bullets were not supplied; see the summary above.

### Git Commits

| Hash | Message |
|------|---------|
| `b083590c` | (see git log) |

### Testing

- Validation was not recorded for this session.

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 24: 微调轮播黑卡尺寸与转场时长

**Date**: 2026-07-28
**Task**: 微调轮播黑卡尺寸与转场时长
**Branch**: `main`

### Summary

黑卡内缩调为 16px，收缩与展开均调为 160ms；横竖屏 LVGL 模拟器、隔离 esp32all 正式固件构建和交互式模拟器重启均完成，未烧录。

### Main Changes

- Detailed change bullets were not supplied; see the summary above.

### Git Commits

| Hash | Message |
|------|---------|
| `3af5e2f3` | (see git log) |

### Testing

- Validation was not recorded for this session.

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 25: Refine 480x320 BMS dashboard

**Date**: 2026-07-28
**Task**: Refine 480x320 BMS dashboard
**Branch**: `main`

### Summary

Simplified the native 480x320 BMS dashboard: duration now omits seconds with a titled divider, electrical labels are removed, and safety checks render dynamically so balancing standby has no green check. Verified with the SDL simulator and refreshed the preview.

### Main Changes

- Detailed change bullets were not supplied; see the summary above.

### Git Commits

| Hash | Message |
|------|---------|
| `accfa166` | (see git log) |

### Testing

- Validation was not recorded for this session.

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 26: BMS 容量估算

**Date**: 2026-07-28
**Task**: BMS 容量估算
**Branch**: `main`

### Summary

实现 JK 原生累计充电容量与 Daly/彦阳基于有效遥测电流的真实容量估算；完成主机自检、旧 ESP32 profile 编译、LVGL 横竖屏渲染，并通过 RFC2217 对 MAC 20:e7:c8:5f:ab:a4 完成镜像写入及哈希校验。

### Main Changes

- Detailed change bullets were not supplied; see the summary above.

### Git Commits

| Hash | Message |
|------|---------|
| `b1fdce71` | (see git log) |

### Testing

- Validation was not recorded for this session.

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 27: Add 320x480 BMS portrait dashboard

**Date**: 2026-07-28
**Task**: Add 320x480 BMS portrait dashboard
**Branch**: `main`

### Summary

Implemented the native 320x480 BMS dashboard, added snapshot-label smoke coverage, and verified all four simulator orientations.

### Main Changes

- Detailed change bullets were not supplied; see the summary above.

### Git Commits

| Hash | Message |
|------|---------|
| `ca2a4494` | (see git log) |

### Testing

- Validation was not recorded for this session.

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 28: 优化控制器与火刃竖屏仪表

**Date**: 2026-07-28
**Task**: 优化控制器与火刃竖屏仪表
**Branch**: `main`

### Summary

为控制器和火刃建立 320x480 原生竖屏布局；同步优化 240x320 顶栏、温度、档位层级、指针边框与速度单位对齐，完成三种分辨率模拟器回归。

### Main Changes

- Detailed change bullets were not supplied; see the summary above.

### Git Commits

| Hash | Message |
|------|---------|
| `51d9af98` | (see git log) |

### Testing

- Validation was not recorded for this session.

### Status

[OK] **Completed**

### Next Steps

- None - task complete
