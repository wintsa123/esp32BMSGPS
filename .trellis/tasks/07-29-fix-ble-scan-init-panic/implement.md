# 实施计划

1. 以 IDF 6.0.2 的 `ble_gatts_count_resources()` 和错误码定义确认 `rc=3` 是 GATT 定义无效。
2. 为 notify characteristic 复用已有 access callback，保持 flags 与协议不变。
3. 以 IDF 6.0.2 的 `ble_gap_vars` 动态上下文和 Directed Advertising 接收路径确认空指针根因。
4. 在 legacy `sdkconfig.defaults` 关闭 `CONFIG_BT_NIMBLE_STATIC_TO_DYNAMIC`，并确认配置器不会把它带入 S3/C3/P4。
5. 运行目标 ESP-IDF 构建、host 自检和 GitNexus `detect-changes`；使用 RFC2217 刷写并复现 BMS 扫描及蓝牙可发现。

## Risk And Validation

- `runtime_init_ble_host()` 是 CRITICAL 影响面，验证范围必须包含 BMS 扫描、控制器扫描与本机广播。
- `./scripts/esp-idf-env.sh build`
- `./scripts/run-host-selftests.sh`
- `node .gitnexus/run.cjs detect-changes --scope compare --base-ref main`
- RFC2217：`./scripts/esp-idf-env.sh -p "rfc2217://192.168.2.10:4000?ign_set_control" -b 115200 flash monitor`
- 在生成的 `sdkconfig` 中检查 `CONFIG_BT_NIMBLE_STATIC_TO_DYNAMIC` 已关闭，并比较镜像的 RAM 预算。

## Validation Record

- Passed: host self-tests, configurator self-test, and the ESP32 feature-enabled
  isolated build (`ESP_BMS_FEATURE_PHONE_MEDIA=1`).
- Passed: static GATT definition check confirms the command notify characteristic
  has `runtime_phone_media_gatt_access_cb`.
- Pending: RFC2217 reached the ESP32 bootloader but stalled before image writes;
  the bridge also rejects `idf_monitor` RFC2217 negotiation. The board was hard-reset
  back to its existing application and requires a healthy bridge for scan-path validation.
