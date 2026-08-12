# Design

## Boundaries

- `esp_bms_bms_ble`：BMS advertising/scan-response 名称归一化与诊断。
- `esp_bms_controller_ble`：Controller 名称归一化、双 FarDriver GATT profile 发现、profile 对应命令节奏和连接阶段日志。
- `esp_bms_idf_runtime`：BMS 候选缓存/HTTP JSON 转义、Controller ready snapshot 投影。
- `esp_bms_lvgl_ui`：共享的 12 项候选合同、`More devices` 分页状态、点击绝对索引与返回。
- 既有 scan ownership、action、NVS、遥测解析和 BMS/Controller source adapter 保持不变。

## Candidate Data Flow

```text
NimBLE DISC event
  -> full-input printable-ASCII normalization
  -> per-MAC name cache/merge
  -> bounded 12-candidate runtime array
  -> dashboard snapshot
  -> shared BLE page (0..4 + More devices)
  -> more page (5..11)
  -> absolute candidate index -> existing confirmation/bind action
```

- 候选结构不变，仅将公共上限从 6 调整为 12；继续使用静态数组和首次发现顺序。
- `settings_ble_more_page` 是唯一新增 UI 状态。刷新函数按 source 和该状态计算 `start/count`；点击回调用相同映射，避免渲染与命中分叉。
- count <= 6 时不显示 more 入口。count > 6 时首层固定保留 5 个候选和入口；更多页显示其余最多 7 个。
- snapshot 更新时若候选缩减到 5 个以内，more 状态自动回首层，防止 offset 越界。

## Name Contract

- 三条现有名称复制路径都遍历 `name_len`，只在输出达到 24 字节时停止；保留 `0x20..0x7e` 全部字符。
- 继续以 MAC 缓存有名报告，空报告不覆盖名称。
- HTTP handler 对 JSON 字符串至少转义 `"`、`\` 和控制字符；使用仓库现有 helper，如无则加入一个有界局部函数，不引入 JSON 依赖。
- 无 local-name 字段仍显示编号。诊断只在解析返回失败且未得到名字时按扫描周期限频记录 MAC/长度/错误，不输出原始隐私数据或逐广播刷屏。

## Controller Profile State Machine

```text
GAP connected
  -> discover Nordic UART service
     -> found: discover NUS notify/write -> CCCD -> subscribe -> WAIT_FRAME
     -> not found: discover FFE0 service
        -> found: discover FFEC/FFEF -> CCCD -> subscribe -> WAIT_FRAME
        -> not found/failure: terminate -> BACKOFF

WAIT_FRAME -> send profile command/poll -> first telemetry-bearing FarDriver frame -> READY
WAIT_FRAME -> no valid frame before timeout -> terminate -> BACKOFF
READY(NUS)  -> five-byte read polling
READY(FFE0) -> open once + keepalive
disconnect  -> clear profile/ready/subscription/telemetry
```

- 使用一个小型内部 profile enum/静态表表达两套已知 UUID 和命令模式；这是两种真实实现，不扩展成通用 adapter/framework。
- `CONTROLLER_ONLINE` 由有效 handle、ONLINE phase 和 `CONTROLLER_SUBSCRIBED` 共同投影；ONLINE phase 只在首个成功更新仪表遥测有效位的 FarDriver 帧后进入，参数块本身不触发。GAP handle 仍可用于“连接中”生命周期和主动 terminate，但不能驱动专属 UI。
- CCCD 写只有异步回调确认成功后才设置 subscribed 并进入 WAIT_FRAME；写失败或首帧超时记录阶段并终止，避免伪在线。
- NUS 恢复仓库历史已有五字节 read request builder/self-test；FFE0 保留当前 open/keepalive builder。

## Compatibility And Risk

- Nordic UART 优先保证 APK-backed 现代 FarDriver；FFE0 回退保留最近提交引入的旧/替代固件兼容。
- snapshot 扩容会改变精确 ABI size assertion并增加静态 RAM；不迁移 NVS 或 wire format。构建 map 和真机 heap 是接受门禁。
- UI 刷新与 runtime snapshot 投影均为 CRITICAL；修改前已有 impact 结果必须在实现代理上下文中复核，修改后用 compare change detection 验证实际范围。
- 失败时可分别回滚候选容量/分页、名称字符策略、controller profile fallback；不需要破坏性配置回滚。
