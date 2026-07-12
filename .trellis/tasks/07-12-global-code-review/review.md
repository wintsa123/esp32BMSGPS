# 全局代码审查报告

基线：`0c334df`（`main`）  
范围：`main/`、`components/`、`tests/`、构建配置与嵌入式 Web UI；不包含本任务和项目说明文件的未提交变更。

## 发现

### 高：NimBLE 主机任务与主循环并发读写同一运行时对象，状态和遥测可能损坏

- 证据：`runtime_init_bms_ble()` 在 `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:4372` 创建独立的 `bms-nimble` 任务；其 GAP 回调在 `:3740-3810` 与 `:3947-4049` 直接更新 `runtime`、`runtime->snapshot`、控制器状态及 `runtime->flags`。
- 同时，主循环在 `main/idf_main.c:138-240` 调用 `esp_bms_idf_runtime_tick()`、读取这些字段并将 `runtime.snapshot` 传给 LVGL；`esp_bms_idf_runtime_tick()` 也在 `:4898-4951` 读写相同的标志、BLE 状态和控制器字段。
- `bms_scan_lock` 仅保护候选数组，`http_pending_lock` 仅保护 HTTP pending 字段；两者都不保护 snapshot、连接句柄、控制器状态或 64 位 `flags`。ESP32 是 32 位目标，64 位掩码的读-改-写不是原子操作。
- 影响：通知、断连或扫描事件与 50ms 主循环交错时，可丢失标志位、读到半更新 snapshot，或对已变化的连接/CCCD 句柄发起 GATT 请求；表现为扫描/连接状态卡住、遥测跳变、偶发无效写入。
- 建议：把 NimBLE 回调转换为队列消息，由主循环独占更新 runtime；或引入覆盖所有跨任务 runtime 字段的 mutex，并在锁内复制完整 snapshot 后再交给 UI。不要以 LVGL 锁替代 runtime 锁。

### 中：Web UI 的“扫描 BMS”在扫描开始前立即读取结果，正常扫描不会自动显示设备

- 证据：`main/web/index.html:382` 在 `POST /api/bms/scan` 返回后立刻调用 `loadBmsCandidates()`；页面中没有轮询、延迟重试或扫描完成事件。
- 后端 `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:2723-2737` 只将请求排入 pending 标志并返回 `204`；真正消费并启动扫描发生在下一轮主循环的 `:4909-4911`。候选项由后续异步 GAP 广播回调 `:3954-3973` 才陆续写入。
- 影响：首次点击通常只能得到旧的或空列表；用户必须再次点击扫描或刷新页面，且无法获知扫描是否完成。
- 建议：扫描按钮置为 loading，在 0.5–1 秒间隔轮询候选接口，直到后端暴露的 `scan_active` 变为 false 或超时；或让扫描接口返回一个 job/state 并由 UI 按状态刷新。

## 验证

- `./scripts/esp-idf-env.sh build`：通过；应用镜像 `0x147de0`，最小 OTA 分区余量 `0x98220`（32%）。
- FarDriver 协议主机自测：以 `gcc -std=c11 -Wall -Wextra -Werror` 编译并运行 `tests/fardriver_protocol_selftest.c`，通过。
- `git diff --check`：通过。
- GitNexus：索引与 `0c334df` 同步；关键调用面分析中，`esp_bms_idf_runtime_apply_action_event` 的上游为 `app_main` 与兼容封装，风险 LOW；当前未提交产品改动为零，变更检测仅见 `AGENTS.md` 与 `CLAUDE.md`，风险 LOW。

## 未覆盖的风险

- 无真实 ANT BMS/FarDriver、Wi-Fi 客户端和触摸硬件的并发长稳测试；尤其应在修复并发问题后验证扫描、重绑、断连、旋转和 Web 配置同时发生的场景。
- 仓库仅有 FarDriver 协议自测，未覆盖 BMS 帧拆包/CRC、HTTP 到主循环的 pending 状态机，或 Web 扫描异步交互。
