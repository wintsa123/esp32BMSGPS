# 模拟器支持 Debian 与 Windows 本机

## Goal

让 LVGL 桌面模拟器同时支持 Debian pkg-config SDL2 与 Windows 原生 SDL2 CMake 包，并提供对应启动脚本。

## Requirements

- 保持 Debian `pkg-config` SDL2 构建入口，同时支持 Windows 原生 SDL2 CMake package。
- Windows PowerShell 启动入口必须支持横屏、竖屏 headless 运行。
- Windows BLE 使用独立 Host bridge 接入模拟器，不移植 ESP-IDF NimBLE。
- FFE0 型远驱控制器使用 `FFE0/FFEC/FFEC` 服务、通知、写入契约。
- 实机验收必须同时验证连接、订阅、原始通知和遥测解析，不能只以连接成功作为完成条件。

## Acceptance Criteria

- [x] Windows 横屏、竖屏 headless 构建运行通过。
- [x] Host BLE 能扫描、连接、订阅并主动断开真实控制器。
- [x] FFE0 写特征由不存在的 `FFEF` 修正为真实设备提供的 `FFEC`。
- [ ] 至少收到一帧真实控制器通知，并解析出 RPM、电压、电流、档位和温度字段。
- [ ] ESP-IDF v6.0.2 构建、RFC2217 烧录和启动日志验证通过。

## Notes

- Keep `prd.md` focused on requirements, constraints, and acceptance criteria.
- Lightweight tasks can remain PRD-only.
- For complex tasks, add `design.md` for technical design and `implement.md` for execution planning before `task.py start`.
# 模拟器支持 Debian 与 Windows 本机

## 目标

让仓库现有 LVGL/SDL2 模拟器在 Debian 和 Windows 原生环境使用同一份生产 UI 源码完成配置、构建和运行。

## 需求

- Debian 保持通过系统 `pkg-config` 发现 SDL2，并继续支持现有 Bash 启动脚本。
- Windows 支持 SDL2 官方 CMake package，允许通过 `SDL2_ROOT` 指定解压后的开发包，不要求 WSL、MSYS2 或 pkg-config。
- Windows 提供 PowerShell 启动入口，支持透传所有模拟器参数。
- 主机入口不得硬依赖 `unistd.h`、`setenv()`、`execvp()` 或 Unix `libm`。
- 不复制 LVGL 页面，不进入 ESP-IDF 组件图，不改变固件构建行为。

## 验收标准

- Windows 原生 CMake 能配置并构建 `esp_bms_lvgl_simulator.exe`。
- Windows `--headless` 与 `--headless --portrait` 均退出 0 并输出 `headless smoke passed`。
- Debian 仍可执行 `./scripts/run-lvgl-simulator.sh --headless`。
- 缺少 SDL2 时给出包含 Debian 与 Windows 安装/配置方法的明确错误。
- 新增脚本不安装全局软件，不修改系统环境变量。
