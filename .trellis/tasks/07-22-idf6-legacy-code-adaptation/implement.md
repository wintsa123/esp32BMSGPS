# IDF 6 配置驱动适配执行计划

## 1. Baseline And Impact Gates

- 记录当前 `git status --short`、catalog、`dependencies.lock` 和三个 profile 的生成输入哈希。
- 对每个待编辑的函数、CMake/Kconfig 符号执行 GitNexus upstream impact；HIGH 或 CRITICAL
  结果先报告，不直接改动。
- 分别以独立目录重跑 legacy ESP32 与两个 S3 profile 的 IDF 6.0.2 配置，保留首个报错。

## 2. Configuration Contract

- 扩展 catalog schema、board/display/input/module 记录，使控制器、分辨率、时序、旋转、
  背光、触摸变换及 GPIO 角色均为数据，不再依赖 C 宏默认值。
- 在 `start.sh` 和 `start.ps1` 实现相同的按模块/driver 角色计算、缺失 GPIO 交互收集、
  CLI 覆盖与完整校验；生成 `firmware.env`、`profile.cmake` 和硬件配置头。
- 为有效保存 profile 增加 Board 菜单发现与一键构建；排除隐藏、备份和无效配置。
- 扩展 `tests/configurator_selftest.sh`，保持 Bash/PowerShell 规范化输出一致。

## 3. Firmware Adaptation

- 引入最小的 `esp_bms_hardware_config` 共享组件，公开生成配置并供 `main`、bridge 使用。
- 修改 `idf_main.c` 仅从该组件取得硬件配置，然后应用已有的 NVS 运行时旋转。
- 将 LVGL bridge 改为 bus/controller 分派，移除 `ESP_BMS_LVGL_BRIDGE_DEFAULT_CONFIG` 和
  所有固定 GPIO；保持现有错误传播、触摸校准、LVGL 锁和 RGB565 写入契约。
- 用实际 IDF 6.0.2 构建错误指导受管 LCD/触摸组件与 CMake 依赖的最小更新，并重新生成锁文件。

## 4. Profile Verification

- 经典 ESP32：新目录 reconfigure/build/size，检查 `0x1e0000` OTA 槽余量。
- ESP32-S3 ILI9488/FT6336U：使用原理图 GPIO，在独立目录完成 build；硬件存在时再刷写验证。
- ESP32-S3 ST7796U/GT1151：未选 GPS 时完成 build；选 GPS 时验证缺失角色输入、生成配置和
  build。硬件存在时再验证显示、触摸、PSRAM 与启动日志。
- 每次构建前清除或新建 profile 专属 build 目录，绝不复用 5.5 输出。

## 5. Quality And Handoff

- 运行 `./tests/configurator_selftest.sh`、相关主机自测、`git diff --check`、三个 profile
  的 `idf.py size` 和项目 `doctor`。
- 对所有本任务改动符号执行 GitNexus `detect_changes`，确认影响仅限配置、build、bridge 和
  必要运行时适配。
- 按项目 LAN RFC2217 合同对用户指定且匹配的实物进行一次刷写尝试；报告结果与未验证硬件。
- 完成后通过 `trellis-check`、更新硬件/build spec，并在不混入既有工作区变更的前提下提交。

## Rollback Points

- catalog/profile 变化：恢复单个 `.env` 与生成器，重新生成对应 profile。
- bridge/config 变化：恢复共享配置边界和分派实现，重跑 legacy build。
- 受管组件变更：恢复 `idf_component.yml` 和锁文件作为同一原子组，重新解析而不手工编辑锁。
