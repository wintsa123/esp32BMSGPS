# Implementation

1. 调整 SDL2 CMake 发现逻辑并归一链接目标。
2. 为 `simulator/main.c` 增加 Windows/Unix 主机兼容包装。
3. 新增 `scripts/run-lvgl-simulator.ps1`。
4. 使用临时 SDL2 开发包运行 Windows 双方向 headless 验证。
5. 运行 CMake 静态配置检查、`git diff --check` 和 GitNexus `detect_changes`。
6. Windows Host BLE 已扫描并连接 `YuanQuFOC972`，确认真实 GATT 为
   `FFE0/FFEC`，并修正桥与固件的写 UUID。
7. 已按官方 APK 时序订阅 FFEC 并发送安全探测/遥测请求；连接和写入成功，
   但通知计数为 0，FFEC 读取值始终为 `22 02`。真实遥测闭环仍未完成。
8. 当前 Windows 主机缺少项目要求的 ESP-IDF v6.0.2，固定 RFC2217 端点
   `192.168.2.10:4000` 也未监听；固件构建、烧录和启动日志待具备正确环境后执行。

## Bug Analysis: FFE0 写特征误配为 FFEF

### 1. 根因类别

- **B/C/D：跨层契约、传播遗漏、测试缺口**。Host bridge、ESP-IDF profile 和
  真实 GATT 没有共享同一个可执行契约，模拟测试只验证了写调用，没有验证目标
  特征确实存在。

### 2. 失败尝试

1. 旧 `AA13...` 命令写向 `FFEF`：目标特征不存在，连接成功也不可能产生数据。
2. 改为 `FFEC` 后只验证写成功：仍不足以证明设备发出了通知。
3. 复刻 APK 安全探测/遥测时序：连接与写入均成功但通知为 0，说明设备端状态或
   固件响应仍是独立变量，不能继续通过猜测配置写入解决。

### 3. 预防机制

- **P0 / DONE**：桥与固件统一为 `FFE0/FFEC/FFEC`，增加源码契约断言。
- **P0 / DONE**：硬件验收要求原始通知数大于 0 且至少一帧通过解析。
- **P1 / TODO**：在 ESP-IDF v6.0.2 环境完成构建、RFC2217 烧录与首帧超时日志验证。

### 4. 系统性扩展

- 其他 BLE profile 也必须同时核对 service、notify、write、CCCD 和真实设备属性。
- 扫描、连接、订阅、写入、通知、解析应分别记录结果，避免用单一“已连接”状态
  覆盖整个数据链路。

### 5. 知识沉淀

- `.trellis/spec/backend/quality-guidelines.md` 已加入 FFE0 写特征与数据闭环契约。
- 本任务保持 `in_progress`，不在零通知状态下归档或提交完成标记。
