# Technical Design

## Scope Boundary

本任务只修改 APK 侧体验和 APK 侧蓝牙数据流；不修改 TFT 页面、LVGL 布局或固件控制器仪表。固件只在确有必要支持“按类型保存手机选择的 MAC”时增加最小命令处理和 NVS 回读。

## Current Data Flow

```text
手机 Android BLE 扫描
  -> APK BMS/Controller 独立候选模型
  -> 用户选择并确认类型
  -> APK 通过已连接的单片机 BLE 写命令下发 MAC
  -> 单片机校验、按类型保存、返回确认

APK 直接连接控制器 BLE
  -> GATT service/characteristic/notify
  -> 控制器帧解析
  -> APK ControllerDashboard 页面
```

APK 与单片机 BLE 连接、APK 与控制器 BLE 连接是两个不同的连接角色；手机不能把“手机扫到的 MAC”自动变成单片机已绑定状态，必须经过显式写命令和确认。

## Navigation And Lists

- 将当前 BMS 导航文案改成“仪表”，保留 BMS 详情/设置为仪表集合中的独立入口。
- 抽出最小的 `BleDeviceCandidate` 数据模型，增加 `source = BMS | CONTROLLER`，每个 source 独立数组、选中 MAC、扫描会话和连接状态。
- 复用现有 `SearchRequest` / `SearchResponse` 和扫描停止机制；不引入新的 BLE 扫描库。
- 两个列表共享行样式和空状态组件，但不共享候选集合或绑定 key。选择后先弹确认，确认按钮携带 source，防止回调使用错误列表索引。

## Controller Dashboard

- 新增 APK Activity/Fragment（优先复用现有 Fragment 导航结构），使用原生 `ConstraintLayout`/`LinearLayout` 和现有颜色/字体资源。
- 首屏采用“速度 + 挡位”双主指标；下方四个紧凑数据单元显示功率/电流、RPM、控制器温度、电机温度；顶部显示连接设备名和连接状态。
- 颜色只区分状态：速度使用高对比主色，挡位使用蓝色强调，告警使用现有红色资源；不加入新依赖或复杂图表。
- 数据更新沿用现有 `BluetoothLeService` / EventBus 广播；断开立即清空控制器快照并显示 `--`，不保留过期值。
- 若 APK 当前只解析 BMS 帧，则先接入仓库已有 FarDriver 字段/命令合同；没有可复用解析代码时，首版只展示已验证字段并把未支持字段标为 `--`，不得猜测单位。

## MAC Provisioning

- 优先复用 APK 已连接的单片机 BLE GATT 写入路径（`BleDataUtils.writeParam` / `MyApplication.writeCharacteristic`）。
- 命令合同必须包含类型、6 字节 MAC、版本或长度和校验；设备端先校验格式与类型，再写对应 NVS key，成功后返回 ACK/回读值。
- BMS 与 Controller 使用不同 action/key；写失败不覆盖旧绑定。没有 ACK 的旧命令只可作为兼容 fallback，不能作为“保存成功”依据。
- 设备端保存属于固件最小变更；若当前 APK/固件无法形成闭环，则保留扫描选择和本地展示，明确标注“未同步到设备”，不伪造成功。

## Build And Risk

- 第一门禁是确认 Android 源工程或可重复的 APK 重打包流程；反编译目录本身不是可靠的 Gradle 工程。
- APK 侧高风险集中在导航/Fragment 生命周期、BLE 扫描回调竞态和旧资源 ID；固件侧高风险集中在命令兼容性与 NVS 写入。
- 不修改现有固件 NimBLE 扫描仲裁、BMS/Controller 候选快照或 TFT 控制器页面，除非 MAC 下发闭环验证证明必须改动。
