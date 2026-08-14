# Technical Design

## Boundary

修改限定在首次产生名称串写的 BLE 候选数据层、候选文本渲染和现有回归检查。
分页索引、配对动作和默认不可发现合同保持不变；若混合名称测试证明扫描存储与快照
正确，则不修改这些层。

## Data Flow

1. NimBLE 扫描回调从单条广告提取 MAC 和该广告自己的名称，名称允许为空。
2. 候选缓存只允许按相同完整 MAC 补全名称，快照逐项复制候选。
3. `settings_bms_ble_refresh_rows()` 对分页映射得到的每个候选都输出一行。
4. 行文本使用该候选名称或 `设备` 占位，并附加 MAC 后两段及 RSSI。
5. `settings_bms_ble_candidate_event_cb()` 继续按固定行高计算视觉行；因为渲染不再跳行，视觉行与
   `settings_bms_ble_candidate_index()` 的结果保持一一对应。

## Key Decisions

- 复用现有 multiline label、固定行高和分页 helper，不新增 LVGL 对象或抽象。
- MAC 仅显示后两段，足以区分当前同名候选，同时控制 240px 竖屏文本宽度。
- 无名候选使用已有中文文案 `设备`；TFT 字库和当前设置页已包含所需字形。
- 不修改真实广播名称，不把所有候选强制命名为 `midea`。
- 先用混合名称回归确定首次串写层，只在该共享根因处修复；不预先重写扫描链路。

## Compatibility And Rollback

- BMS 与控制器共享该渲染函数，因此行为同步修复。
- 数据结构、NVS、BLE 协议和默认可发现状态不变。
- 回滚仅需撤销本任务的 UI 渲染和模拟器测试改动。
