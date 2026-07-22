# ESP-IDF 6.0 升级执行计划

## 预检与边界

- [ ] 记录 `HEAD`、`git status --short`、当前 IDF 5.5.4 版本、`dependencies.lock`
  哈希、legacy/S3 catalog 与 profile 输入的哈希；把它们视为用户既有变更基线。
- [ ] 读取 backend 构建/质量规范和 `trellis-before-dev` 指引；每次编辑函数、类或方法前
  运行 GitNexus upstream impact，若风险 HIGH/CRITICAL，先报告再编辑。
- [ ] 安装或校验独立的官方 ESP-IDF v6.0.2 环境及其 Python 工具，不原地升级/覆盖
  `/home/wintsa/esp/esp-idf-v5.5.4`。

## 迁移

1. 环境和入口
   - 更新 Bash、PowerShell、Python/刷写辅助脚本及 `doctor` 的版本探测和默认回退路径。
   - 使入口优先接受经验证的 `IDF_PATH` v6.0.2，错误时给出中英文可操作诊断。
   - 更新中英文 README、构建规范和 CI（如存在）的版本、安装和不兼容选项说明。

2. 组件图和配置
   - 将 `main/idf_component.yml` 及需要的组件约束升级为 6.0 系列；用 6.0 Component
     Manager 重新解析兼容依赖。
   - 审核 `esp_lvgl_adapter`、LVGL、LCD 和触摸组件的 6.0 支持；只提高到解决 6.0 兼容
     性的最小版本，并更新 lock 哈希。
   - 让 profile 构建使用目标专属 lock；重新生成 legacy 与 S3 的 Kconfig v3 输出，处理
     无效或重命名选项而不是保留旧生成 `sdkconfig`。

3. 编译与 API 适配
   - 在独立的 legacy build 目录进行 `reconfigure` 和构建，逐项处理 CMake、链接 orphan
     section、组件可达性以及由警告升级导致的失败。
   - 在独立的 S3 build 目录重复相同流程，验证 16 MB Flash、8 MB PSRAM、S3 分区和 I80/
     I2C 所需组件；不把 legacy 生成产物复用于 S3。
   - 仅针对实际 IDF 6.0 报错改 API/Kconfig；添加或更新针对这些兼容点的主机/脚本测试。

4. 交付验证
   - 运行配置器自测、`git diff --check`、每个受支持 profile 的 clean build、`idf.py size`
     及分区槽余量检查。
   - 经典 ESP32：提供 RFC2217 固定桥、115200 的刷写/监视命令和显示、Setup AP/Web、GPS、
     BLE BMS、OTA 基础初始化检查表，交由用户实机执行。
   - ESP32-S3：提供与实际 board profile 对应的 USB/串口刷写命令以及 PSRAM、分区、显示、
     触摸与启动日志检查表，交由用户实机执行。若 profile 非 READY，记录阻塞到所属板级
     任务，不以错误 GPIO 刷写。
   - 提交前运行 GitNexus `detect_changes`，确认符号和执行流只覆盖升级范围。

## 验证命令（按实际 profile 参数补全）

```bash
./tests/configurator_selftest.sh
./scripts/esp-idf-env.sh -B firmware-builds/legacy/idf-build reconfigure
./scripts/esp-idf-env.sh -B firmware-builds/legacy/idf-build build
./scripts/esp-idf-env.sh -B firmware-builds/legacy/idf-build size

# S3 使用配置器生成的 target、defaults、partition 与 lock，保持独立 build 目录。
./start.sh build-local --profile esp32s3-n16r8-st7796u-gt1151

git diff --check
node .gitnexus/run.cjs detect-changes --repo esp32BMSGPS
```

## 风险点与停止条件

- 高风险文件：根/组件 CMake、`idf_component.yml`、依赖锁、Kconfig defaults、profile
  生成器和构建入口；它们会同时影响多个组件或目标。
- Component Manager 的 lock 重解可能受网络或上游版本影响；将记录精确版本和哈希，不
  手工伪造 lock。
- 若 legacy 或 S3 的分区/内存预算越界，停止刷写并先处理镜像，而不是擦除设备或修改
  分区来掩盖回归。
- 若当前未提交的 S3/模块化变更阻塞构建，保留其边界和错误证据，针对直接依赖协作处理，
  不覆盖、回退或混入无关的重构。

## 2026-07-22 执行记录

- 已在隔离目录安装并验证官方 ESP-IDF `v6.0.2`、ESP32/ESP32-S3 工具链；项目组件锁已由
  IDF 6 Component Manager 重新解析为 v3 格式。
- legacy ESP32 已以 IDF 6.0.2 完成重配置、全量构建和 `size`：
  `esp32_bms_gps_idf.bin` 为 `0x180b10`（1,575,575 B），最小 `0x1e0000` OTA 槽余
  `0x5f4f0`（20%）。`./tests/configurator_selftest.sh`、
  `./scripts/run-host-selftests.sh`、Shell 语法检查与 `git diff --check` 均通过。
- 重配置暴露了默认启用 GPS 但未进入默认 `REQUIRES` 闭包的问题；已将
  `esp_bms_gps` 纳入 `ESP_BMS_MAIN_REQUIRES_DEFAULT`，并增加配置器回归断言。GPS
  运行时代码未改动。
- S3 实物对应 `esp32s3-n16r8-st7796u-gt1151` 仍为 `BUILD_READY=NO`：catalog 没有
  `GPS_RX`、`GPS_TX`、`GPS_PPS`，且 ST7796U/GT1151 驱动尚未接入运行时 bridge。
  默认 S3 profile 会安全排除 GPS，但本地编译仍因 `BUILD_READY=NO` 被阻止；强制加入 GPS
  时配置器会因 `gps requires input GPIO role GPS_RX` 正确拒绝。该引脚/驱动边界属于
  `07-21-multi-mcu-s3-board-support` 与 `07-21-display-input-drivers`，本任务不猜测 GPIO、
  不生成错误镜像，也不刷写设备。
- GitNexus `detect-changes --repo esp32BMSGPS` 已执行：37 个当前工作树文件、10 个已索引
  符号、0 条受影响流程、风险 LOW；报告混有升级前已有的组件化/S3 改动，提交时必须分离。
