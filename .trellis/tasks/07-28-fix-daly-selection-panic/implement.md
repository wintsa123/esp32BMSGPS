# 实施计划

1. 在 `bms_stop()` 计算既有 host ready/synced 条件，只在条件为真时调用 NimBLE GAP API。
2. 保留无条件的 flag、连接状态、遥测和 UI 信息清理。
3. 构建 ESP-IDF 固件，并检查 diff 仅包含任务文件与该 BLE 组件。
4. 通过 RFC2217 刷写固件，在无绑定 MAC 情况下连续选择多个 BMS 类型；再验证扫描或已连接时
   更换类型可正常取消并恢复为 idle。
5. 运行 GitNexus `detect-changes`，确认受影响符号和执行流符合本设计。

## Validation

- `./scripts/esp-idf-env.sh build`
- RFC2217 flash，端口为 `rfc2217://192.168.2.10:4000?ign_set_control`，波特率 `115200`
- 串口日志中没有 `Guru Meditation Error`、`LoadProhibited` 或软件重启
- 设备上选择至少两个不同类型，并检查重启后选择仍由既有 NVS 流程恢复
