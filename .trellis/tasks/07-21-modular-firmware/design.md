# 组件化与裁剪设计

## 边界

`esp_bms_core` 保留应用状态、NVS、显示设置、LVGL/显示/输入初始化和稳定
动作契约。每个可选模块都是一个独立 ESP-IDF component，拥有其硬件驱动、
后台任务、资源和生命周期实现；它只能通过核心定义的状态/事件接口更新
仪表状态，不能让核心包含可选模块头文件。

模块到组件的最终映射如下：

| 模块 | 组件 | 依赖 |
| --- | --- | --- |
| gps | `esp_bms_gps` | UART、GPIO、核心状态 |
| bms | `esp_bms_bms_ble` | NimBLE、核心状态 |
| controller | `esp_bms_controller_ble` | NimBLE、核心状态 |
| audio | `esp_bms_audio_feedback` | 已独立 |
| network | `esp_bms_network` | Wi-Fi、HTTP 基础路由、核心状态 |
| ota | `esp_bms_ota` | network、app_update |
| cast | `esp_bms_cast` | network、WebSocket |

`main` 只编译生成的模块注册表并依次调用模块的 init/start/tick/stop 回调；
它不会直接包含上述模块的头文件。模块 catalog 的 `COMPONENTS` 字段是唯一
组件闭包来源，生成的 profile 同时驱动 `main` 的 `REQUIRES` 与注册表源文件。

## 迁移顺序

1. 先抽取 GPS：其 UART/PPS、NMEA/CASBIN 解析和已有独立 stream 源文件边界
   最清晰，可验证 disabled profile 不含 `esp_bms_gps`。
2. 抽取 BMS BLE 和控制器 BLE，保留协议状态只通过核心快照投影。
3. 拆出 network，再将 HTTP 路由按 network、ota、cast 移动；`index.html` 只由
   network 嵌入。
4. 将可选设置/API/Web 贡献改为模块注册表数据，删除 runtime 中的旧实现。

每次只迁移一个行为完整的模块，legacy 全功能 profile 在每步保持构建和运行。
当前单体未迁移模块继续明确标记为 `legacy-runtime`，生成器不得把它报告为
已裁剪。

## 兼容与验证

默认 legacy profile 启用全部模块并使用现有 ESP32 WROOM-32E GPIO、4 MB 双
OTA 分区。每个模块迁移后至少构建一组 enabled 和 disabled profile；disabled
检查 `build_components`、模块 archive、链接 map、ELF 符号和相关嵌入资源。
有设备镜像变更时按 RFC2217 流程刷写 legacy profile；桥接不可用时记录精确
传输错误，不宣称实机通过。
