# 修复 FarDriver 无响应轮询

## Goal

将 FarDriver Nordic UART 的只读轮询切换为无响应写，保留 CCCD 带响应写，并验证控制器页面能接收并投影遥测。

## Confirmed Facts

- 现代 FarDriver APK 使用 Nordic UART 服务 `6e400001-b5a3-f393-e0a9-e50e24dcca9e`，通知特征为 `...0003...`，写入特征为 `...0002...`；固件已按此发现服务和双特征（`components/esp_bms_controller_ble/esp_bms_controller_ble.c:64-76, 359-390`）。
- APK 的 Nordic UART 写入逻辑优先使用 Android 的无响应写类型；固件对同一 5 字节只读请求使用带响应写 `ble_gattc_write_flat`（`components/esp_bms_controller_ble/esp_bms_controller_ble.c:286-308`）。
- 控制器在线状态在 GAP 连接后即被投影，遥测仅在收到并通过校验的 16 字节通知后更新（`components/esp_bms_controller_ble/esp_bms_controller_ble.c:456-531`）。因此连接成功不代表轮询请求已被控制器接受。
- 后续轮询仅在控制器页或速度仪表页运行；订阅完成时只发送第一条请求（`components/esp_bms_controller_ble/esp_bms_controller_ble.c:311-329, 747-765`）。
- 当前 FarDriver 协议 host 自检只覆盖请求构造和帧解析，不覆盖 ESP-IDF GATT 写入模式；它已通过。

## Requirements

### R1 - 无响应只读轮询

- `controller_send_read_request()` 必须以 NimBLE 的无响应写 API 发送现有的 5 字节只读请求。
- 请求字节、轮询地址序列、间隔与成功后轮询索引推进语义保持不变。
- CCCD 订阅继续使用带响应写，不能改变通知订阅行为。

### R2 - 失败可观测性

- 无响应写 API 立即返回非零时，记录包含返回码和当前轮询地址的控制器日志。
- 失败时不得推进轮询索引或清除现有遥测状态。

### R3 - 保持范围

- 不发送 FarDriver 参数写入、控制指令、密码或远程驱动指令。
- 不改变解析字段、NVS、Web API、设置和 LVGL 布局。

## Acceptance Criteria

- [ ] 仅控制器只读轮询改用 `ble_gattc_write_no_rsp_flat`；CCCD 路径仍为 `ble_gattc_write_flat`。
- [ ] 无响应写失败时有包含地址与返回码的日志，且轮询索引不前进。
- [ ] FarDriver 协议 host 自检、受影响 ESP-IDF 构建和 `git diff --check` 通过。
- [ ] 通过固定 RFC2217 桥接完成一次刷写尝试，并在设备可连接时检查控制器页是否出现遥测；若硬件不可用，明确记录原因。

## Out Of Scope

- 协议帧格式、字段缩放或轮胎/传动比计算修正。
- 后台页面的持续轮询策略调整。
- FarDriver 写寄存器、调参、密码、故障诊断或远程控制。
