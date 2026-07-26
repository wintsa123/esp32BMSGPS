# BMS 骑行峰值记录设计

## Boundaries

- `esp_bms_bms_ble` 继续负责协议解码。解码到完整 BMS 帧后只把规范化字段交给 runtime；不得在 NimBLE 通知回调中写 NVS。
- `esp_bms_ride_records` 是 `esp_bms_idf_runtime` 内的一个小型纯 C 状态模块，负责固定 5 槽记录、峰值比较和淘汰，便于宿主机自测。
- `esp_bms_idf_runtime` 负责本次开机标志、跨任务同步、NVS 读写和 HTTP JSON；它是唯一的持久化与 API 所有者。
- `main/web/index.html` 与 `vercel/src/App.tsx` 只展示设备返回的数据。Vercel 继续直连设备 HTTP，不增加云端存储或代理。

## Data Flow

```
完整 BMS 通知帧
  -> bms_apply_telemetry
  -> runtime 受保护的内存峰值状态
  -> esp_bms_idf_runtime_tick 主循环落盘 NVS
  -> GET /api/bms/ride-records
  -> 热点网页 / Vercel 页面
```

`bms_apply_telemetry` 位于 NimBLE 通知路径，因此只能执行有界内存复制、比较和置脏标志。主循环每 50 ms 检查置脏标志，复制一致的历史快照后再执行 NVS 写入。HTTP 请求同样先复制历史，再序列化，避免长时间持锁。

## Record Model

每条记录包含 `max_current` 和 `max_delta` 两个峰值快照。快照字段为：

- `pack_voltage_mv`
- `current_deci_amps`，保留原始符号
- `delta_cell_voltage_mv`
- `soc_percent`
- 6 位温度有效掩码和对应 6 个 `int16_t` 温度

纯状态模块维护 oldest-to-newest 的固定 5 槽数组和数量。每次启动时 runtime 把“本次开机已创建记录”设为 false：

1. 首个完整有效 BMS 样本追加新记录，并同时初始化两组峰值快照。
2. 后续样本只有在 `abs(current_deci_amps)` 严格变大时替换 `max_current`；只有在压差严格变大时替换 `max_delta`。
3. 一个样本同时突破两个峰值时一次更新两个快照，并只产生一次持久化请求。
4. 满 5 条后追加新记录前左移并丢弃最早条目。

绝对值计算必须先提升到 `int32_t`，避免 `INT16_MIN` 溢出。温度缺失用有效掩码表达，HTTP 输出对应位置为 `null`；核心的电压、电流、压差、SOC 无效或帧为 partial 时整个样本忽略。

## Persistence

NVS 的 `esp_bms` 命名空间增加一个版本化 blob 键。blob 只含固定数组、记录数和格式版本，不含本次开机的活动标志。下次启动会把上一次直接断电留下的最后记录自然视为历史，再等待首个有效 BMS 帧创建新记录。

读取缺失、尺寸不匹配、版本不匹配或数量越界时视为无历史，不迁移不可信数据。NVS 写失败时保留内存数据并以受限重试频率再次尝试，同时记录可操作日志。由于只在首次样本和严格峰值突破时写入，不对每个 50 ms tick 写 Flash。

五条固定记录远小于 1 KB；不需要 PSRAM。NVS/Flash 是唯一来源，既能在无 PSRAM 的默认板上工作，也能跨电源中断恢复。

## HTTP Contract

新增 `GET /api/bms/ride-records`，返回最新记录在前：

```json
{
  "records": [
    {
      "current": true,
      "max_current": {
        "pack_voltage_mv": 55200,
        "current_deci_amps": -830,
        "delta_cell_voltage_mv": 18,
        "soc_percent": 74,
        "temperatures_c": [25, 27, null, null, null, null]
      },
      "max_delta": {
        "pack_voltage_mv": 53900,
        "current_deci_amps": -410,
        "delta_cell_voltage_mv": 31,
        "soc_percent": 69,
        "temperatures_c": [29, 30, null, null, null, null]
      }
    }
  ]
}
```

`current` 只表示本次开机正在更新的记录；重启后此前最后一条不再标记为 current。接口保持只读和加法兼容。用分块 JSON 响应而非扩大现有 1 KB 栈缓冲，确保 5 条记录和全部温度始终可返回。现有通用 CORS/PNA 响应头继续适用。

## Web Presentation

热点页在状态区下方加入“骑行峰值记录”区域；Vercel 概览页加入同样的信息区。每条展示最大电流和最大压差两行，显示电压、电流、压差、SOC 及有效温度；空历史使用明确中文空态。英文文本加入现有字典，语言入口仍只在设备设置中。

Vercel 通过设备 HTTP API 获取历史。BLE 控制链路不承载这一最多 5 条的 JSON 历史，避免扩大 BLE 协议范围。

## Compatibility And Rollback

- 旧 NVS 中没有 blob 时返回空数组；原有 `/api/status` 和配置接口不变。
- BMS、网络或 Web 功能编译期关闭时，不得因此影响启动；HTTP 路由沿用既有功能守卫。
- 回退时删除新增 NVS key 与路由/展示代码即可，旧 blob 会被忽略，不涉及分区迁移。
