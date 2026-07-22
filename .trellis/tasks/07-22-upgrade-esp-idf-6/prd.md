# 整体升级 ESP-IDF 6.0

## Goal

将整个固件工程及其本机构建、Windows 入口、组件依赖锁和持续集成（如存在）从当前
ESP-IDF 5.5.4 基线迁移到 ESP-IDF 6.0，同时保持既有产品功能、板级配置和可裁剪多
MCU 架构可构建、可验证。

## Confirmed Facts

- 根 `sdkconfig` 由 ESP-IDF 5.5.4 生成；仓库 README、`start.sh`、`start.ps1`、
  `scripts/esp-idf-env.sh` 及若干 Windows 辅助脚本将 5.5.4 作为默认回退环境。
- `main/idf_component.yml` 当前声明 IDF `>=5.5`，受管组件锁定在
  `dependencies.lock`；其中包含 LVGL 9.5.0、`esp_lvgl_adapter` 0.6.2，以及 LCD
  和触摸组件。
- 当前默认产品为 ESP32-WROOM-32E（4 MB Flash、无 PSRAM、ST7789/XPT2046、双 OTA
  分区），固件具有 Setup AP、Web UI、GPS、BLE BMS、OTA、TFT/LVGL 等运行时契约。
- ESP32-S3 实机现已到位。当前 CLI 默认的 S3 profile 是
  `esp32s3-n16r8-st7796u-gt1151`（16 MB Flash、8 MB PSRAM、16 位 I80 ST7796U 和
  GT1151）；另一 S3-WROOM-1 ILI9488/FT6336U profile 仍标注未就绪。
- 现有的“多 MCU 可裁剪固件与 ESP32-S3 迁移”任务正在将工程扩展为十个 IDF 目标和
  target-specific dependency lock；其目录、组件边界和构建生成器仍在进行中。
- 工作区已有 36 处未提交变更，包含 runtime 组件拆分、组件 CMake、
  `dependencies.lock` 和 legacy 板型配置；这些变更不应被本任务覆盖或误认为迁移产物。

## Requirements

- R1. 目标构建链、组件管理器、工具链和项目配置均以 ESP-IDF 6.0 为准，版本策略在
  规划期间明确并可复现；ESP-IDF 5.5.4 不再作为受支持的仓库构建路径。
- R2. 更新所有面向开发者的环境探测、构建、刷写、诊断和文档入口，不再将 ESP-IDF
  5.5.4 宣称为默认开发环境。
- R3. 对受管组件进行 ESP-IDF 6.0 兼容性解析并生成可审计的锁文件；不得跨 MCU 目标
  复用不兼容的依赖锁。
- R4. 保持既有硬件与产品契约：经典 ESP32 的分区、GPIO、无 PSRAM 约束、TFT/触摸、
  GPS、BLE、Wi-Fi Setup AP、中文优先 Web UI 和 OTA 行为。
- R5. 与现有多 MCU/组件化迁移对齐，不能回退其组件边界、profile 构建隔离或 S3 支持。
- R6. 遵循脏工作区边界：仅修改本迁移明确拥有的文件，不覆盖任务开始前的用户变更。
- R7. 完成适当的主机、ESP-IDF 构建和可用硬件验证，并记录无法在当前硬件上验证的
  目标及原因。

## Acceptance Criteria

- [ ] 目标 ESP-IDF 6.0 环境可通过项目正式入口完成干净的配置和构建。
- [ ] 所有受支持的构建 profile 均使用其正确 MCU 目标和 IDF 6.0 依赖锁，并且不复用
      旧 IDF 生成的构建目录。
- [ ] 经典 ESP32 默认构建保持可用，并提供 LAN RFC2217 刷写/启动验证命令供用户手动执行。
- [ ] 已到位的 ESP32-S3 生成对应 profile 的可刷写产物，并提供 Flash/PSRAM、显示与触摸
      初始化的手动验证步骤；其余尚无实物的声明目标明确标记为 build-only。
- [ ] 项目入口、README（中英文）、环境诊断和 CI（如存在）不再错误要求或回退到 5.5.4。
- [ ] 组件 API/Kconfig/构建告警均已处理；最终镜像仍符合对应分区槽大小约束。
- [ ] 不覆盖升级前已存在的用户修改；变更范围和执行流在提交前经 GitNexus
      `detect_changes` 核验。

## Out Of Scope

- 不以升级为名新增未规划的产品功能、变更硬件接线或改变用户可见业务行为。
- 不将尚未完成的多 MCU/组件化工作回退到单体实现；若该工作形成前置依赖，应在计划中
  明确排序而不是复制或绕过它。

## Decisions

- D1. 直接以 ESP-IDF 6.0 取代 ESP-IDF 5.5.4；仓库入口、依赖锁、CI 和文档只维护
  一条 IDF 6.0 构建路径，不长期维护双版本兼容。
- D2. ESP32-WROOM-32E 和已到位 ESP32-S3 均为本次实机验证目标。S3 验收默认采用
  当前 CLI 默认的 `esp32s3-n16r8-st7796u-gt1151` profile；若连接设备的芯片/板型
  识别与此不符，先调整为对应已声明 profile，再执行刷写，不猜测或复用错误 GPIO。
- D3. 刷写、启动与功能实机验证由用户手动完成；本任务必须提供已构建的相应产物、准确
  的目标/profile 命令和需要观察的验收项，但不主动写入任一设备。

## Notes

- 这是复杂的跨构建链迁移；在开始实施前必须补齐 `design.md` 和 `implement.md`。
