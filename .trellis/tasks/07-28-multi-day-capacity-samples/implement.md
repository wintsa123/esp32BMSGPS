# Implementation Plan

1. 在修改前读取相关运行时规范，并对 `esp_bms_capacity_estimate_observe`、`esp_bms_idf_runtime_observe_bms_capacity` 和 `esp_bms_idf_runtime_observe_bms_capacity_from_current` 执行 GitNexus 上游影响分析；如发现 HIGH 或 CRITICAL 风险，先向用户报告。
2. 在容量估算模块实现固定 8 条样本历史、排序后的中位数、25% 离群值拒绝、环形替换和基于全部保留值的平均数；保留现有锚点和连续性保护。
3. 将就绪门槛改为 3 条合格样本，扩展容量估算自检，覆盖启动建基线、离群拒绝、环形满额替换、平均值和中断重锚。
4. 将运行时 NVS blob 升级为版本 2，保存固定样本数组和环形元数据，加入最大 80 字节的编译期约束；安全忽略版本 1 记录，保持类型和 MAC 隔离。
5. 运行格式化、容量估算自检和项目既有主机测试；执行目标固件构建。提交前运行 GitNexus `detect_changes`，确认只影响容量估算、持久化和对应测试流程。

## Risk Controls

- 不改变 BMS 协议解码和轮询周期，避免扩大 BLE 链路风险。
- 不记录无界历史或原始遥测，NVS 使用量恒定。
- 离群判定只在有 3 条基线后执行；引导阶段仍依赖现有结构性校验。
- 版本不匹配时重新学习，不尝试从无原始样本的版本 1 平均值反推历史。
