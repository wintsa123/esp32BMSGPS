# FlashDB 时序存储设计

## 目标与边界

本次固件只启用板载 Flash。两个开发板的 TF 卡座暂不进入代码配置；FlashDB 后端保留清晰边界，未来拿到 TF 接口和 GPIO 后再增加后端。设备不再嵌入热点 Web 页面，Android APK 通过 SoftAP HTTP API 访问数据。

## 分层

`esp_bms_flashdb` 组件独占 FlashDB、FAL 端口、逻辑分区、记录编码、会话管理和查询游标。`esp_bms_idf_runtime` 只在 1 秒采样点提供已锁定的 GPS/BMS 快照和故障状态；HTTP 层只调用存储查询 DTO，不接触 FlashDB 地址或结构体。Android `DeviceApi` 负责 JSON 解码，`MainActivity` 负责会话选择和分页展示。

## 数据与时间

- 联合样本固定 32 字节：版本、字段有效性/状态、GPS 经纬度 E7、包电压、电流、SOC、单体 min/max/avg/delta、最多 6 路压缩温度、BMS 类型和会话相对秒。结构体使用显式定宽字段和静态断言，未提供的字段只由有效性位表达。
- 采样任务每秒最多追加一条；GPS 更新慢于 1 秒时沿用最近值但携带新鲜度/有效性位，过期或无定位不得伪造坐标。BMS 同理。
- FlashDB 内部时间戳使用持久化 `session_seq` 加相对秒组成的 64 位单调键；`session_seq` 只在开始新会话时写一次 NVS，避免重启后时间回退。
- 首次得到完整 GPS UTC 后追加一次会话锚点记录（同一槽位的元数据记录），将 `anchor_elapsed_s` 映射到 `anchor_utc_s`。HTTP 查询把相对秒换算为 UTC；没有锚点的会话返回 `time_calibrated=false`，只允许相对时长读取。

## 会话槽与容量

每个会话槽是一个独立 TSDB，最多三个槽；故障事件是第四个独立 TSDB。新会话开始前扫描槽状态：优先使用空槽；无空槽时清理最旧槽，再初始化新会话。经典 ESP32 的小分区只有一个可用样本槽，按实际条数保存截断记录；达到槽容量后结束本次记录并报告 `capacity_reached`，不循环覆盖该会话开头。

逻辑 FAL 分区位于板级 `flashdb` 自定义物理分区内，擦除粒度 4 KiB。S3 规划三个约 896 KiB 样本槽和独立故障区；经典 ESP32 在 OTA 后约 192 KiB 尾部空间中按分区表预留故障区，其余用于一个截断样本槽。启动检查使用 FAL 实际分区长度、编码后单条大小和故障区保留量计算可用时长，不使用芯片标称容量。

## 故障事件

保护/警告集合发生新增、清除或变化时追加一个固定事件；稳定状态不重复写。事件携带时间键、BMS 类型、绑定身份摘要、已知代码和未知位。事件 TSDB 独立滚动，样本槽清理永不触碰它。

## API

- `GET /api/history/sessions`：返回最多 3 份会话摘要、校准状态、开始/结束时间或相对时长、样本数、截断/容量状态和后端容量状态。
- `GET /api/history/samples?session=<id>&from=<s>&to=<s>&cursor=<token>&limit=<n>`：返回分页联合样本；范围可以是 UTC 或未校准会话的相对秒。
- `GET /api/history/faults?from=<utc>&to=<utc>&cursor=<token>&limit=<n>`：分页故障事件。
- 现有 `/api/gps/track` 保留，改为当前/最近会话的 GPS 投影，避免破坏 Android 既有调用；新增 API 采用响应版本和稳定错误码。

查询在设备端逐条回调并限制 `limit`，不把完整 5 小时数据装入 RAM。Android 统一在 `DeviceApi` 解码可选字段和游标，页面按会话加载，不复制完整历史。

## 构建与兼容

- 固定 FlashDB 2.2.0 源码和许可证，启用 TSDB、64 位时间戳、FAL，禁用 KVDB；上游无 IDF manifest，采用项目组件内固定源码副本。
- 每个板级分区表增加独占 `flashdb` 自定义分区；不得与 OTA、NVS 或已声明区域重叠。FlashDB FAL 逻辑分区只在该物理分区范围内。
- 删除 `main/web/index.html` 的 CMake 嵌入和 `/` 根页面处理；保留网络、SoftAP、HTTP API、OTA 和投屏路由。
- 旧 GPS NVS blob 仅在 FlashDB 首次成功导入后清理；迁移过程有一次性标记和重复启动保护。

## 故障与回滚

FlashDB 初始化、追加、查询或损坏恢复失败时设置持久化降级状态，实时 GPS/BMS/BLE/Wi-Fi/OTA 继续运行。单槽写满只结束该会话；故障区写满按自身 rollover 策略处理。升级回滚时保留自定义分区和版本字段，未知记录跳过而不阻塞启动。
