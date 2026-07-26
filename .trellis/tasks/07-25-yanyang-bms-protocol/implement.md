# 实施计划：彦阳 BMS 协议适配与目录重组

## 变更顺序

1. 读取 `trellis-before-dev` 和相关规范，确认 ESP-IDF 6.0.2、目标 `esp32`、2 MB Flash、
   无 PSRAM 的构建约束；对每个待修改符号执行 GitNexus 上游影响分析。
2. 创建纯 C 协议目录与内部契约；迁移现有 ANT 帧解析至 `protocols/ant/`，保持其现有
   帧和字段行为不变；更新组件 CMake 源文件列表。
3. 实现 `protocols/yanyang/`：128 位 UUID、只读轮询队列、Modbus CRC、分包/粘包流重组、
   `0x0001` 主遥测解析和单位换算。添加 APK 来源注释，仅标注证据，不复制应用代码。
4. 使 BLE 状态机按 `bms_type` 使用正确 UUID、轮询与解析器；错误帧不刷新在线时间戳或
   仪表快照。
5. 扩展运行时枚举、NVS 策略、HTTP 配置和 manifest，接受 `yanyang`；扩展 LVGL 动作、
   标签数组、静态断言、保存动作和模拟器动作映射；扩展 Web 静态后备下拉与白名单。
6. 新增 `tests/yanyang_bms_protocol_selftest.c`，并接入 `scripts/run-host-selftests.sh`；测试
   纯解析器的请求、CRC、完整/分包/粘包/损坏输入和字段边界。
7. 运行 `./scripts/run-host-selftests.sh`、`./scripts/esp-idf-env.sh build`、相关 LVGL 模拟器
   冒烟检查，以及 GitNexus `detect-changes`。本次按用户要求不执行烧录。

## 风险与检查点

- `settings_show_bms_type_picker` 的 GitNexus 影响为 CRITICAL：17 个符号、10 条流程，
  包含模拟器。必须更新动作序列及其静态断言，不能依赖枚举减法的隐式范围。
- `runtime_select_bms_type` 影响运行时动作和 `app_main`；保存前须验证旧 NVS 值仍映射到
  原品牌，非法值回退 ANT。
- 用保护板实机验证发现不符合 APK 的服务 UUID、从地址或响应页时，保留主机自测结果并
  记录实际抓包差异；不通过猜测添加写命令。
- 工作区现有 `esp_bms_idf_runtime.c` 和 `main/web/index.html` 用户改动必须保留，只在相关
  位置合并。

## 验收命令

```bash
./scripts/run-host-selftests.sh
./scripts/esp-idf-env.sh build
./scripts/run-lvgl-simulator.sh --self-test
node .gitnexus/run.cjs detect-changes -r esp32BMSGPS
./scripts/esp-idf-env.sh -p "rfc2217://192.168.2.10:4000?ign_set_control" -b 115200 flash
```

构建或烧录失败时，先保留完整错误输出；协议和设置层不得带着失败的编译或未解释的测试
失败进入提交。
