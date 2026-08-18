# 设计

## 传输抽象

APK 维持现有 `DeviceApi` 的业务模型，在其下增加 transport 选择：优先绑定的设备 Wi-Fi HTTP；没有 Wi-Fi 时使用设备 BLE GATT 会话。BLE 请求使用短帧 JSON，包含 `id`、`method`、`path`、`body`，响应返回同一 `id`、HTTP 风格状态码和 JSON body。单个请求串行，超时即断开并清理缓存。

固件新增独立的 BLE GATT service/characteristics：write/request 与 notify/response。服务只接受 APK 协议版本、白名单路径和受限 JSON 大小，内部调用现有状态/配置处理函数，不复制业务校验或直接写 NVS。投屏相关 `/api/cast/*` 不在白名单内。

## APK 状态

增加设备连接模式 `NONE/WIFI/BLE`。`refreshDeviceProfile`、设备数据刷新和设置保存通过统一调用入口选择 transport；已有 Wi-Fi 绑定逻辑优先级不变。`connectExistingWifiOrLoadInfo` 在无 Wi-Fi 时不再尝试 HTTP，而是显示 BLE 模式信息；`loadInfo` 仅 Wi-Fi 调用，投屏按钮同时要求 `mode == WIFI` 和投屏能力已加载。

BLE 扫描独立于 BMS/控制器扫描目标，使用设备服务 UUID 过滤，连接时发现特征并发送请求。生命周期结束或断连时取消回调、清空响应和设备缓存。

## 兼容与回退

旧固件无设备服务时仍可扫描到其他 BLE 外设，但 APK 不将其识别为设备；用户看到“未发现支持设备 BLE 服务”。Wi-Fi 路径完全保留。BLE 请求只复用已存在的设备业务入口，失败时不会改变旧 HTTP 行为。
