# Design

- CMake 先尝试 SDL2 config package；非 Windows 再回退到 pkg-config。所有分支归一为内部目标 `esp_bms_simulator_sdl2`。
- Windows 重启使用当前可执行文件重新拉起进程；Unix 保留 Bash runner 的 `execvp()` 行为。
- SDL dummy driver 通过平台包装函数设置，避免把 Windows CRT 细节散落在 `main()`。
- PowerShell 脚本只负责依赖定位、CMake 配置/构建和启动，不下载或安装依赖。

## 蓝牙边界

Windows BLE 后端通过独立 host adapter 接入 snapshot/action，不直接移植 ESP-IDF NimBLE。
远驱 FFE0 profile 固定使用 service `FFE0`、notify `FFEC`、write `FFEC`。
硬件闭环按 `扫描 -> 连接 -> CCCD -> 遥测请求 -> 原始通知 -> 16 字节解析`
验收；任一步缺失都不能把数据链路标记为成功。

APK 静态分析只用于确认安全的连接探测与遥测命令。不得复刻其中的 flash-save
流程，也不得向只写配置特征 `FFF8` 发送探测数据。
