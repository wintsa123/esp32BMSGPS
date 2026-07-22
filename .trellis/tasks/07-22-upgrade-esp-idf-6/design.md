# ESP-IDF 6.0 升级设计

## 目标版本与边界

迁移固定到官方 ESP-IDF **v6.0.2**（6.0 系列的当前稳定补丁版本），而不是未固定的
`release/v6.0` 分支或 `latest`。项目的 `IDF_PATH` 可以指向该版本安装目录；仓库提供
的回退路径、诊断提示和文档均指向 `esp-idf-v6.0.2`。项目组件元数据声明 6.0 系列范围，
避免误用 5.5 或未来 6.1。

本任务只处理为 IDF 6.0 正确构建、运行和验证必须做的源代码、Kconfig、CMake、受管组件
与工具入口变更。它不复制或替代正在进行的模块拆分、S3 显示驱动适配和十目标产品线工作；
但是这些变更进入当前工作树后，必须与 6.0 一起构建，不能被升级回退。

## 事实与迁移风险

官方 5.5→6.0 指南指出：

- ESP-IDF 6.0 默认把编译器警告视为错误，且链接器拒绝最终 ELF 中的 orphan section。
- 构建配置改用 esp-idf-kconfig v3，需重新生成而非复用 5.5 的 `sdkconfig` 输出。
- 内置 `json`、ESP-MQTT 和若干旧协议接口已移至受管组件或移除。
- `esp_lvgl_adapter` 0.6.0 起已包含 IDF 6.0 兼容修复；项目锁定的 0.6.2 是可用起点。

当前源码未引用 cJSON、ESP-MQTT、旧 I2C 或旧 ADC API；ADC 已使用 oneshot API。因此
优先风险是 Kconfig 重生成、组件解析、CMake 依赖闭包、警告升级为错误，以及当前脏工作树
中的组件拆分，而非已知 API 的机械替换。

来源：

- https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32/migration-guides/release-6.x/6.0/index.html
- https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32/migration-guides/release-6.x/6.0/build-system.html
- https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32/migration-guides/release-6.x/6.0/protocols.html
- https://components.espressif.com/components/espressif/esp_lvgl_adapter/versions/0.6.2/changelog?language=en

## 构建与依赖策略

```
IDF v6.0.2 环境
  ├─ 官方入口：scripts/esp-idf-env.sh / start.sh / start.ps1
  ├─ 配置器：按 profile 生成 target、sdkconfig defaults、partition 与组件闭包
  ├─ 组件管理器：解析 6.0 兼容版本，生成 target 对应 dependencies.lock
  └─ 独立 build 目录：idf.py -B <profile>/idf-build reconfigure/build
       ├─ legacy ESP32 → 4 MB / 无 PSRAM / 双 OTA
       └─ S3 N16R8 → 16 MB / 8 MB PSRAM / S3 partition
```

1. 先在独立 IDF 6.0.2 build 目录配置，绝不让 6.0 读取或覆盖已有 5.5 build 目录。
2. 组件管理器在每个目标下重新解析锁文件；无法同时满足的依赖必须通过目标专属 lock 或
   受限组件版本解决，不能复制一个目标的 `managed_components` 到另一个目标。
3. 保留 `IDF_PATH` 优先级，但环境脚本和 `doctor` 必须确认其真实版本是 6.0.2，防止
   名称正确但工具链错误。默认回退仅是受控的 v6.0.2 安装目录。
4. 所有 `sdkconfig` 都由 6.0 `reconfigure` 再生。提交的是设计性 defaults/受控 profile
   输入，不把构建目录、临时 `sdkconfig` 或 Component Manager 缓存当成源文件。

## 兼容修复原则

- 按构建报错逐项修复；每次改动函数、CMake target 或 Kconfig 前先运行 GitNexus upstream
  impact，若为 HIGH/CRITICAL 先报告风险。
- 优先更新组件依赖声明、公共头文件和 Kconfig defaults；只在 6.0 删除/改签 API 的证据
  明确时改运行时代码。
- 不通过全局关闭 `-Werror`、链接器检查或 Kconfig 警告来掩盖迁移问题。
- 不降低经典 ESP32 的 Flash/内存/分区约束，也不以 S3 的 PSRAM 条件掩盖 legacy 缺陷。
- S3 profile 若仍标记 `BUILD_READY=NO`，先查明是否为已有板级任务的未完成驱动边界；
  仅为 IDF 6.0 API/配置兼容所需的改动属于本任务，未授权的整套新显示/触摸驱动开发不
  作为升级的替代品。

## 验证与回退

验证顺序为：配置器自测 → clean reconfigure/build（legacy）→ clean reconfigure/build（S3）
→ 分区/尺寸/依赖锁审计 → 生成经典 ESP32 RFC2217 与 S3 USB 手动刷写命令。用户自行执行
刷写、启动、显示/触摸/PSRAM 等实机验收；本任务分别报告可构建证据和待用户确认的硬件项。

回退以小粒度提交/文件组为单位：恢复错误的版本约束、组件锁或适配补丁后，用新建的 IDF
6.0 build 目录重新验证。绝不执行重置工作树或删除其他任务的未提交文件。若兼容性阻塞，
保留 6.0 诊断和最小复现，不悄然把入口切回 5.5。
