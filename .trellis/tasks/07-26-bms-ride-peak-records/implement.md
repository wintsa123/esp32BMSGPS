# BMS 骑行峰值记录实施计划

## Scope And Risk

- 计划修改的 runtime tick、HTTP API handler 和 BMS telemetry apply 路径已由 GitNexus 初查为 LOW 风险：前两者分别有 `app_main` 或无直接上游调用，BMS apply 只有 `bms_frame_push` 直接调用。
- 编码前对每个实际修改的函数重新执行 GitNexus upstream impact；若结果升为 HIGH/CRITICAL，先向用户报告再继续。
- 不修改 `main/idf_main.c`、协议解码器、分区或硬件配置。

## Implementation Steps

1. 在 `esp_bms_idf_runtime` 组件增加纯 C 的 `esp_bms_ride_records` 源文件和头文件，定义固定 5 槽、样本/快照结构、首次样本创建、绝对电流峰值、压差峰值和 oldest-first 淘汰逻辑。
2. 增加 `tests/ride_records_selftest.c`，覆盖首样本双峰初始化、严格比较、负值绝对电流、同帧双峰更新、无效样本忽略和第 6 条淘汰；将其加入 `scripts/run-host-selftests.sh`。
3. 扩展 runtime 状态，保存历史、一次开机的 session 标志、受保护的置脏状态和 NVS 重试节流。初始化时加载并校验 NVS blob；首次有效样本才创建本次记录。
4. 在 `bms_apply_telemetry` 的完整帧路径把规范化样本交给 runtime 的有界内存更新 API。该 API 不分配、不记录日志、不调用 NVS，并用短临界区保护记录状态。
5. 在 `esp_bms_idf_runtime_tick` 中复制脏历史并持久化到 NVS；失败保留脏状态并限频重试。BMS 断开、超时或重连只影响实时状态，绝不创建新旅程记录。
6. 实现 `GET /api/bms/ride-records` 的只读分块 JSON 响应，返回 newest-first 的固定上限数组和 `null` 温度位；复用现有 CORS/PNA 头。
7. 更新嵌入式网页和 Vercel 的数据类型、加载/刷新逻辑、中文/英文文本和紧凑历史展示；不增加依赖或云端请求。

## Validation

```bash
./scripts/run-host-selftests.sh
./scripts/esp-idf-env.sh build
npm run typecheck --prefix vercel
npm run build --prefix vercel
git diff --check
node .gitnexus/run.cjs detect-changes -r esp32BMSGPS
```

验证点：自测必须证明 `INT16_MIN` 的绝对值不会溢出、同次开机只有一条记录、重启后新增记录、严格比较不重复写入、5 条淘汰正确。固件构建通过后，使用实际 BMS 检查一次峰值更新、重启恢复与两个 Web 页显示；硬件验证缺失时明确标记为未验证。

## Rollback Points

- 纯状态模块和自测可先独立回退，不影响 BMS BLE。
- NVS blob/主循环接入可单独回退，既有实时 BMS 显示不受影响。
- HTTP 和任一 Web 展示层可单独回退；原有 `/api/status`、设置与 BLE 控制接口保持不变。
