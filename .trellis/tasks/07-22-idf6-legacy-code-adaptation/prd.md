# 旧代码全面适配 ESP-IDF 6.0

## Goal

将现有 ESP-IDF C 固件、组件定义、Kconfig、CMake、依赖解析、构建入口和开发者文档
完整适配固定的 ESP-IDF v6.0.2，使任何受支持的构建路径不再依赖或回退到 5.5。

## Confirmed Facts

- 本机 ESP-IDF v6.0.2 环境、工具链和 Python 环境已经通过项目包装器验证；5.5 SDK 和
  工具环境已删除。
- 活动源码、启动脚本、README 和 CI 搜索不到 5.5 回退路径；剩余命中来自历史 Trellis
  任务记录、Serena 记忆和防回退自测断言，不代表构建依赖。
- 工作区当前已有 52 处未提交的用户/并行任务变更，涉及组件拆分、profile 和构建输入；
  本任务不得覆盖、回退或混入它们。
- 父任务已记录：legacy ESP32 曾在 IDF 6.0.2 通过完整构建；当前 S3
  `esp32s3-n16r8-st7796u-gt1151` profile 因板级 GPIO/显示/触摸驱动尚未就绪被安全拒绝。
- 当前 catalog 共有一个经典 ESP32 profile 和两个 ESP32-S3 profile。S3 runtime 仍把
  ST7789/SPI/XPT2046 和经典 ESP32 引脚固化在 LVGL bridge；ST7796/GT1151 依赖已在
  `main/idf_component.yml` 声明但尚未使用。
- `esp32s3-n16r8-st7796u-gt1151` 已声明 16 位 I80 和 GT1151 I2C 引脚，但没有 GPS
  角色；`esp32s3-wroom-1-n16r8-i80` 已确定为 ILI9488/FT6336U，但 catalog 尚未录入其
  I80 与触摸引脚，必须从已验证的原理图合同同步，而不能依赖代码默认值。
- 既有 `firmware/catalog/*.env`、`firmware.env` 和 `profile.cmake` 已构成 profile 配置
  链。该链当前只生成模块闭包和通用 GPIO 清单，尚未生成 bridge 消费的显示/触摸参数。
- `docs/hardware/07-schematics.md` 已给出 ILI9488/FT6336U S3 的引脚合同：I80 D0..D7 为
  GPIO4..11，WR/DC/CS/RST 为 GPIO12/13/14/15，背光为 GPIO16，触摸 SDA/SCL 为
  GPIO17/18，INT/RST 为 GPIO21/38，GPS 为 GPIO39/40/41。
- 每次生成的 profile 已保存为 `firmware-builds/<profile>/firmware.env`，但当前交互式
  Board 菜单只枚举静态 `firmware/catalog/board/*.env`，无法发现或复用已保存配置。

## Requirements

- R1. 使用 ESP-IDF v6.0.2 对 `main/` 和所有 `components/` 的旧 C API、CMake、Kconfig
  与组件依赖逐项重新配置和编译；只根据实际 6.0 报错修改兼容代码。
- R2. 保持现有功能契约：经典 ESP32 的分区、无 PSRAM 约束、Setup AP/Web UI、GPS、BLE
  BMS、OTA、TFT/LVGL 和中文优先 Web UI 行为不得因升级倒退。
- R3. 重新解析受管组件依赖，确保 `dependencies.lock` 与 IDF 6.0、各目标/profile 的
  组件闭包一致；不得手工伪造锁文件或复用旧构建目录。
- R4. 所有已声明的 ESP32 与 ESP32-S3 profile 均须适配 v6.0.2 并完成独立构建；当前
  `BUILD_READY=NO` 不能作为跳过 IDF 6.0 适配的理由，但必须先补齐其真实的板级契约。
- R5. 入口、诊断、README、CI 和自动化测试只支持 v6.0.2；历史任务记录保留其历史事实，
  不作为活动文档重写。
- R6. 每次修改函数、方法、类或 CMake/Kconfig 符号前先执行 GitNexus upstream impact；
  若结果为 HIGH 或 CRITICAL，先报告风险。
- R7. 依据已选模块和显示/触摸类型计算所需 GPIO 角色。profile 未预置某个已选功能的角色
  时，配置器必须提示用户输入十进制 GPIO 数字，写入生成的 `firmware.env`，再执行存在性、
  方向、危险脚和重复占用校验；未选功能不得要求其 GPIO。
- R8. LVGL bridge 不得内置任何板型、GPIO、显示控制器或触摸控制器默认值。所有 I/O、
  总线、控制器、分辨率、时序、复位/背光与旋转参数由 catalog/profile 配置文件定义，并
  在构建时生成固件配置；bridge 只验证并消费该配置。
- R9. 下次启动 Bash 或 PowerShell 配置器时，Board 菜单必须列出有效的已保存 profile。
  选择后导入其 `firmware.env` 并重新校验；仅扫描 `firmware-builds/<profile>/firmware.env`，
  忽略隐藏的备份、临时目录和无效文件，且不得把保存的 profile 写回静态 board catalog。
  通过校验后直接复用现有构建入口开始构建，不再逐步询问硬件或模块选项。

## Acceptance Criteria

- [ ] 经典 ESP32 以新建的 IDF 6.0.2 构建目录完成 `reconfigure`、`build` 和 `size`，
      镜像仍落入 `0x1e0000` OTA 槽。
- [ ] 组件管理器、CMake、Kconfig、编译和链接阶段没有遗留的 IDF 5.5 API/配置错误，且
      不通过关闭 `-Werror` 或链接检查规避错误。
- [ ] `./tests/configurator_selftest.sh`、相关主机自测、`git diff --check` 及项目环境
      诊断通过。
- [ ] 每个已声明 ESP32/ESP32-S3 profile 使用独立的 IDF 6.0 构建目录并完成 build；
      所有 GPIO 均来自 catalog、已验证原理图或配置器收集的用户数字，不生成猜测性镜像。
- [ ] 经典 ESP32 和每个 S3 profile 的显示/触摸配置均来自其生成的 profile 配置；静态
      检查证明 bridge 的公开头文件和实现不再包含固定的板型 GPIO 或默认控制器宏。
- [ ] 配置器自测覆盖：未选 GPS 时不要求 GPS GPIO；选中 GPS 且 board 未预置 GPS GPIO 时
      接受用户填写的数字、写入 profile 并通过校验；无效、危险或冲突的数字被拒绝。
- [ ] Bash 与 PowerShell 自测覆盖：保存 profile 后，下次交互的 Board 菜单能列出它；
      选择后恢复原 MCU、board、显示、输入、模块与 GPIO，直接进入已有构建入口，同时
      忽略备份和无效配置。
- [ ] GitNexus `detect_changes` 仅显示迁移预期影响；既有未提交变更保持原状。

## Out Of Scope

- 修改历史任务记录来抹除其 5.5 背景。
- 未经原理图或用户确认而猜测 GPIO，或向未确认匹配的设备刷写。
- 覆盖或回退本任务开始前已经存在的工作区变更。
