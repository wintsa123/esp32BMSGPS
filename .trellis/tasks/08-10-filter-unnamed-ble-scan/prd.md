# Filter unnamed BLE scan candidates

## Goal

TBD.

## Requirements

- TBD

## Acceptance Criteria

- [ ] TBD

## Notes

- Keep `prd.md` focused on requirements, constraints, and acceptance criteria.
- Lightweight tasks can remain PRD-only.
- For complex tasks, add `design.md` for technical design and `implement.md` for execution planning before `task.py start`.
# BLE 扫描候选过滤

## 背景

BMS 与控制器扫描会把没有可见名称的 BLE 广播保存到候选列表，TFT 页面随后用“设备 1”“设备 2”等兜底名称展示。部分广播在手机和电脑蓝牙列表中不可见，用户不希望在本设备列表中看到这些未知条目。

## 需求

- BMS 和控制器扫描列表只保留广播名称中包含中文字符的设备。
- 无名称、仅 ASCII 名称、仅符号/数字名称的广播不得进入候选列表，也不得生成“设备 N”条目。
- BMS 与控制器的扫描行为保持一致。
- 扫描绑定流程仍使用原始 MAC，中文名称应能安全通过 Web JSON；TFT 没有中文字库时不能因为名称解析失败而生成未知设备条目。
- 清空扫描结果后列表应显示原有“未发现”状态；扫描完成后没有合格设备时列表为空。

## 验收标准

1. 含中文的广播名称出现在 BMS/控制器候选列表中。
2. 无名称或不含中文的广播名称不出现在任一列表中。
3. 不再出现“设备 1”“设备 2”这类扫描兜底项。
4. Web `/api/bms/candidates` 与 TFT 设置页都遵循同一过滤结果。
5. 相关单元/模拟器测试、格式检查和固件构建通过。
