# JK、JBD、Daly BLE 协议实现

## 目标

在现有 ESP-IDF NimBLE BMS 客户端中同步接入 JK、JBD 和 Daly BLE 协议。彦阳协议及其目录不在本任务范围。

## 已确认事实

- `esp_bms_bms_ble` 已有统一扫描、连接、服务发现、订阅、轮询和遥测投影状态机。
- 目前 JK、JBD、Daly 仅能在 Web/TFT 选择，未有独立协议目录或实现。
- JK、JBD 与 Daly 参考仓库分别提供其 BLE 帧与服务特征约定；用户授权直接同步其协议行为。

## 要求

- 每个品牌的协议文件必须位于 `components/esp_bms_bms_ble/protocols/<brand>/`。
- 协议解析保持纯 C，不依赖 NimBLE；BLE 相关的 UUID、请求与状态机分派集中在 `esp_bms_bms_ble.c`。
- JK 支持固定 300 字节通知帧及状态轮询。
- JBD 支持 `DD ... 77` 状态/电芯帧、分包重组和 `FF AA` 认证响应；只发只读请求。
- Daly 支持 D2 与 P81 的 Modbus 风格读取帧和状态帧；A5 没有可验证的本仓库参考帧，不伪造 A5 支持。
- 所有品牌将基础遥测投影到现有 `esp_bms_bms_telemetry_t`，坏校验或长度不合法的帧不得更新遥测。

## 验收标准

- [ ] JK、JBD、Daly 各有独立目录、头文件、解析器和主组件 CMake 源文件登记。
- [ ] BLE 状态机针对每个品牌选择正确的服务/读写/通知特征和轮询请求。
- [ ] 三个 parser 自检覆盖有效帧、分包或粘包、损坏校验帧以及关键遥测字段。
- [ ] `./scripts/esp-idf-env.sh build` 通过，协议自检通过。

## 不在范围内

- 彦阳协议修改。
- BMS 写配置、MOS 控制、固件升级以及未有可验证帧定义的 Daly A5。
