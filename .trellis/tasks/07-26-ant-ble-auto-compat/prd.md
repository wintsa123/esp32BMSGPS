# ANT 新旧 BLE 自动兼容

## Goal

绑定 ANT BMS 后无需用户选择协议，兼容上游 `syssi/esphome-ant-bms` 已有可验证实现的新版与旧版 BLE 帧协议，并只在成功校验完整响应后显示遥测。

## Confirmed Facts

- 新旧 ANT BLE 都使用 GATT 服务/通知特征 `0xFFE0` / `0xFFE1`，不能通过 UUID 或扫描名称可靠区分。
- 现有实现只支持新版：请求 `7E A1 01 00 00 BE 18 55 AA 55`，接收 `7E A1 ... AA 55`、Modbus CRC16 帧，见 `components/esp_bms_bms_ble/esp_bms_bms_ble.c:395` 与 `protocols/ant/esp_bms_ant_protocol.c:69`。
- 上游旧版 BLE 使用只读请求 `DB DB 00 00 00 00`，接收固定 140 字节、`AA 55 AA` 开头、16 位累加校验帧。
- Web/TFT 现有 ANT 选项保持为单一品牌项；本任务不新增协议选择项。

## Requirements

1. 在 `bms_type=ant` 的同一 BLE 连接状态机中支持新版与旧版 ANT BLE 协议。
2. 订阅成功后先发新版只读状态请求；在有限超时内没有通过校验的新版完整帧时，发送旧版只读寄存器请求。之后按已锁定协议轮询。
3. 不得通过设备名、MAC 前缀、UUID 或未校验的片段锁定协议；只有完整且校验正确的状态帧可锁定新版或旧版。
4. 新旧协议都必须正确处理 BLE 分包、粘包、损坏校验和，以及协议切换/重连时的缓冲清理。
5. 将两种协议都投影到现有 `esp_bms_bms_telemetry_t`，并保持现有仪表与 Web 配置中的单一 ANT 选项。
6. 自动探测和周期轮询只允许读取遥测，绝不发送写寄存器、认证、MOS 控制或配置修改命令。

## Acceptance Criteria

- [ ] 新版和旧版上游黄金帧均能解析出电压、电流、SOC、电芯统计、温度及容量等基础遥测。
- [ ] 新版请求、旧版请求、分包、粘包、损坏帧和重连后的未知协议状态均有可执行主机自检。
- [ ] 自动探测仅在校验后的完整帧到达时锁定；两种探测请求均是只读命令。
- [ ] UI 和 HTTP 配置中仍只显示一个 ANT 品牌选项，不出现新/旧协议选择。
- [ ] `./scripts/run-host-selftests.sh` 与目标 ESP-IDF 构建通过。

## Out Of Scope

- UART、RS485、CAN ANT 协议。
- 上游未提供可验证帧或协议定义的 ANT 衍生板。
- BMS 写配置、认证、MOS 控制、升级和基于名称/MAC 的推测性兼容。
