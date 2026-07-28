# Daly 与彦阳 BMS 真实容量估算

## Goal

为已绑定的 Daly 或彦阳 BMS 提供可持久化的可用容量估算，并复用 JK
任务已有的灰色、不可点击的“容量估算”设置行。两种协议都没有已验证的
累计 Ah 字段，因此以经过校验的遥测电流在本地积分成单调累计吞吐量，
再交给现有 SOC 跨度估算器。

## Confirmed Facts

- Daly `D2` 状态帧已解码包电压、电流、SOC 与剩余容量；电流为
  `u16_be - 30000`，单位为 `0.1 A`。`P81` 已接入的完整状态帧也携带
  电压、电流和 SOC（`protocols/daly/esp_bms_daly_protocol.c:48`）。当前
  Daly 轮询每 `500 ms` 发送一项、四项轮换
  （`esp_bms_bms_ble.c:37`、`esp_bms_bms_ble.c:626`）。
- 彦阳 `0x0001` 主页已解码包电压、电流、SOC、剩余容量与额定容量；电流
  是有符号大端值除以 10，单位为 `0.1 A`。主页是四项 `500 ms` 轮询中的
  一项，因此约每两秒有一份主页遥测
  （`protocols/yanyang/esp_bms_yanyang_protocol.c:68`、
  `esp_bms_bms_ble.c:590`）。
- 两个协议当前均未解析到可作为累计充电量或循环容量的字段，不能用
  `total_capacity_mah`、剩余容量或电压替代累计吞吐量。
- 现有估算器用累计 mAh 的变化除以 SOC 跨度，并要求至少 20% SOC、1 Ah
  的吞吐量；其方向反转会重新锚定，并将最多四个样本平滑
  （`esp_bms_capacity_estimate.c:36`）。
- JK 任务已建立估算的 NVS 身份键、仪表盘投影和只读设置行。本任务依赖
  JK 任务完成后的该接口，不修改其 JK 协议解析规则
  （`esp_bms_idf_runtime.c:1650`、`esp_bms_lvgl_ui.c:4563`）。

## Requirements

1. Daly 和彦阳仅使用通过既有长度/CRC 校验且包含有效包电压、电流、SOC
   的非 partial 遥测帧积分；SOC 必须在 `0..100`。不得增加 BLE 请求、
   轮询频率、FreeRTOS 任务、定时器或动态分配。
2. 合成累计量按 `abs(current_deci_amps) * delta_us / 36,000,000` 计算 mAh，
   保留整数余数，充电和放电都计为吞吐量。默认使用 `0.5 A` 小电流死区；
   此值为内部校准常量，不做用户设置项。
3. 相邻有效遥测的时间差超过 3 秒、时间异常或 BMS 连接重建时，不得积分
   该间隔，并重置 SOC 估算锚点。保留已确认的历史估值，等待新的完整
   SOC 跨度样本更新它。
4. 同一 BMS 类型与绑定 MAC 重启后，合成累计量从持久化的
   `last_accepted_cycle_mah` 接续；类型或 MAC 不匹配时，清除估值投影、
   估算状态与积分器，不能把旧 BMS 的累计量用于新 BMS。
5. 运行时容量支持策略、NVS blob 校验和 HTTP 状态统一扩展到
   ANT、JK、Daly、彦阳。JBD 继续显示为不支持；既有 ANT/JK 记录必须可读。
6. 保护板设置只保留一个灰色、不可操作的容量估算 list item。Daly/彦阳
   在数据不足时显示“估算中”，完成时显示 Ah；JBD 显示不支持文案。页面
   在容量值改变、BMS 类型改变或绑定 MAC 改变后刷新。
7. 容量估算不可由电压推导。磷酸铁锂等电池在中段 SOC-电压平台很平，
   所以结果依赖 BMS 上报的 SOC：若该 BMS 的 SOC 本身是电压估算，结果仍
   只能视为估算值，不能标为标称容量或健康度。

## Acceptance Criteria

- [ ] Daly 和彦阳的有效遥测能提供积分所需的有符号 `0.1 A` 电流与 SOC；
      损坏校验帧、partial 帧和非法 SOC 不会改变积分器或容量估算。
- [ ] 纯 C 自检覆盖正/负电流取绝对值、两秒累积的余数精度、小电流死区、
      超过三秒时跳过积分并重新锚定，以及从 `last_accepted_cycle_mah`
      恢复后的首次采样。
- [ ] Daly 与彦阳在单调 SOC 的至少 20% 跨度内可形成估值；SOC 方向反转、
      断连、BMS 类型变化或绑定 MAC 变化不会把错误吞吐量并入样本。
- [ ] NVS 和 HTTP 对 ANT、JK、Daly、彦阳一致地报告 ready / estimating，
      旧 ANT/JK blob 保持可加载，JBD 保持 unsupported。
- [ ] 保护板设置中的灰色只读 list item 对 Daly/彦阳正确显示“估算中”或
      Ah，不能触发动作；JBD 显示不支持。
- [ ] 主机自检、LVGL 双方向无头模拟、`oldesp32` 构建和真实设备连机观察
      均通过。

## Out Of Scope

- JBD 或其他 BMS 的容量估算。
- 电压曲线、内阻、温度或标称容量推导的 SOH/健康度模型。
- BMS 写配置、校准、额外 BLE 轮询，以及持久化每一个电流积分点。
