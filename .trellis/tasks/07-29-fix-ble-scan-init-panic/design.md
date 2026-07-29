# 设计：phone-media GATT 与 legacy NimBLE 初始化修复

## Boundary

修改范围包括已有的 `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c` phone-media GATT 定义，以及 legacy ESP32 的 `config/sdkconfig/sdkconfig.defaults`。BMS/控制器状态机、Host 初始化、UI 返回逻辑和 GATT 协议定义不变。

## Data Flow

`START_BMS_BIND -> runtime_apply_pending_http_bms_scan -> bms_start_for_bind -> ensure_ble_host -> runtime_init_ble_host`。

NimBLE 在 Host 启动前逐个校验服务定义。notify 特征没有 read/write 属性，因此复用状态写入回调只满足“非空 callback”的 API 合约，不改变任何可从对端访问的属性；真正的状态写入仍由同一回调在 `BLE_GATT_ACCESS_OP_WRITE_CHR` 且加密连接时处理。

配置器只为 legacy ESP32 选择基础 `sdkconfig.defaults`；S3/C3/P4 使用 MCU 专用 defaults。基础配置关闭 `CONFIG_BT_NIMBLE_STATIC_TO_DYNAMIC`，使 `ble_gap_vars` 等启动上下文驻留 BSS，早到的 Directed Advertising Report 不会穿过空上下文。

## Compatibility And Rollback

- phone-media 关闭时，预处理器不编译其注册与回收分支；现有 BLE 行为保持不变。
- phone-media 开启时，成功路径仍在 Host 启动前登记 GATT 服务，Android 协议 UUID 与特征不变。
- 单一初始化字段改动，可直接回退；不会触及 BMS 数据或 NVS 绑定。
- 配置只作用于 legacy ESP32；静态 RAM 占用增加，必须以独立构建和真机冷启动确认 LVGL 初始化仍有余量。
