# ESP32-S3 与多 MCU 板级适配

## Goal

让配置和构建覆盖 ESP-IDF 5.5.4 十个 MCU 目标，并提供可覆盖 GPIO 的 ESP32-S3 WROOM-1-N16R8 默认板。

## Requirements

- catalog 覆盖 esp32/s2/c2/c3/c5/c6/c61/h2/p4/s3 的 BLE、Wi-Fi、GPIO、保留引脚、显示总线、Flash/PSRAM 能力。
- 经典 ESP32 board 保留当前默认引脚和 4 MB 分区。
- S3 默认使用 I80 ILI9488、FT6336U、8 MB PSRAM、GNSS 39/40/41、I2S 42/47/48、AMP_SHDN 2，可覆盖并重新校验。
- S3 使用指定 16 MB 双 OTA/数据分区；每个目标使用 `dependencies.lock.${IDF_TARGET}`。
- 只有实际设备通过验证的 board 才标记 `hardware-verified`。

## Acceptance Criteria

- [ ] 十个目标各有至少一个合法基准 profile 并通过配置生成。
- [ ] 无 BLE、无 Wi-Fi 或无对应显示总线的组合在构建前拒绝。
- [ ] S3 默认 profile 的 GPIO、PSRAM、Flash、分区逐字段符合需求。
- [ ] 任意 GPIO 覆盖后重新执行存在性、能力、占用和危险检查。
- [ ] 目标间不共享或覆盖错误的 managed-component lock。
