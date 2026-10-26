# 实施计划

## 顺序

1. 实施前加载 `trellis-before-dev`；确认 GitNexus 索引可用并分别对现存配置解析/应用、BMS BLE 通知调度、控制器通知处理和 LVGL BMS action 回调执行 upstream impact。HIGH/CRITICAL 风险按规范提示并纳入实现，不作为额外审批门槛。
2. 新增 EM02 协议纯模块和 selftest：固定四条请求向量；按 byte-count+5 重组 Modbus 响应；验证 CRC、身份型号、半包/粘包、异常 byte-count；只投影参考源码和公共遥测结构共同支持的字段。
3. 追加 BMS 类型 7，并贯通 BMS 类型文本、运行时连接/轮询/断开清理、BLE 写/通知 UUID 和绑定后回连；贯通 Web 下拉与运行时 settings manifest、LVGL 当前项显示。保留既有 0–6 枚举含义。
4. 补蓝宝 Web 配对码表单与配置保存：将非空输入限制为六位数字并接入现有 pending config/NVS 路径；GET `/api/config` 移除 `bluebabe_code` 明文，只提供是否已保存状态；前端留空时不覆盖现有码。
5. 在 LVGL 型号列表禁用蓝宝交互并灰显，保留当前选择标记；在 action 回调中拒绝蓝宝切换，作为 UI 之外的二次保护。
6. 调整控制器 BLE 通知接收长度上限，使其覆盖甲胄最大合法 envelope，检查所有协议共享 `ESP_BMS_CTRL_FRAME_MAX_LEN` 的调用点和发送缓冲，不扩大不相关的协议允许长度。
7. 完成其余已接入保护板/控制器对照审查，逐项补足交叉验证文档；对证据足够的确定性协议差异添加回归用例并修复，对语义不确定项仅记录。
8. 更新设备清单、参考协议、限制和自测结果文档；执行 `trellis-check`，构建目标固件并运行 host selftests、配置/API 回归测试。可以用设备时再做 EM02/相关控制器真机抓包；无硬件时明确标注未验收。
9. 检查 `git diff` 与 `gitnexus detect-changes --scope all`；修复检查发现的问题后再进入提交流程。

## 主要边界文件

- BMS 类型/API/NVS：`components/esp_bms_idf_runtime/include/esp_bms_idf_runtime.h`、`components/esp_bms_idf_runtime/esp_bms_idf_runtime.c`。
- BMS BLE 接入：`components/esp_bms_bms_ble/esp_bms_bms_ble.c`、`components/esp_bms_bms_ble/protocols/`、该组件 `CMakeLists.txt`。
- Web：`main/web/index.html` 及其配置回归测试。
- LVGL：`components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c`、`ui_settings.c`、`ui_settings_system.c`、对应头文件与动作测试。
- 控制器传输边界：`components/esp_bms_controller_ble/esp_bms_controller_ble.c`、`components/esp_bms_controller_protocol/include/esp_bms_controller_protocol.h`、甲胄协议及相关测试。
- 证据：`docs/protocols/motexa-baseline.md`、`docs/protocols/cross-verification.md`、`tests/`。

## 验证命令

- `bash scripts/run-host-selftests.sh`
- `python3 -m unittest discover -s tests -p 'test_*.py'`
- 对本任务变更运行 repository documented ESP-IDF firmware build（通过 `trellis-check` / hardware-build spec 确认精确 target/profile）。
- `node .gitnexus/run.cjs detect-changes --scope all --repo .`；若 CLI 缺失，使用已配置 GitNexus MCP。`partial`/`truncated` 结果必须继续查询，不能当作无影响。

## 风险与回滚点

- EM02 参考的 Modbus 03 byte-count 易与总帧长混淆；selftest 固定四个完整帧长。
- 扩展配置 pending 结构可能同时服务 HTTP 与 BLE API；只扩展 HTTP 提交路径，验证其它调用方留空时行为不变。
- 蓝宝配置 GET 当前返回明文配对码；改为 saved 标志后需同步 Web 表单，不得在错误提示/日志中记录码值。
- 控制器公共帧上限可能被协议 parser 和发送路径复用；先审查调用点，仅提升通知接收容量所需边界。
- 所有新增协议代码可独立移除；NVS 枚举和既有蓝宝码格式保持不变，回滚无需迁移数据。
