# 功能组件拆分与中文定制化编译脚本

## 目标

继续将可选功能从单体 runtime 拆为 ESP-IDF 组件，并让定制固件配置器在 Bash、
PowerShell 与 CMD 入口提供中文或英文的配置、校验和编译体验。用户选中的模块必须
决定实际组件闭包；未选模块不得被误报为已从镜像移除。

## 已确认事实

- 本任务是 `07-21-multi-mcu-trimmable-firmware-s3` 的子任务，承接网络、OTA 和
  配置器本地化，不新增第二套 catalog、profile 或入口。
- GPS、BMS BLE、控制器 BLE 和音频已经独立为 ESP-IDF component。工作区正在将
  Setup AP、HTTP、首页资源迁至 `esp_bms_network`，将 OTA 请求处理迁至
  `esp_bms_ota`。
- 模块 catalog 的 `COMPONENTS` 与生成的 `profile.cmake` 共同决定组件闭包；网络和
  OTA 已映射到新 component，投屏仍为 `legacy-runtime`。
- Bash 已生成网络/OTA feature；PowerShell 尚未生成完整 feature 集，且两端当前生成
  的标准化配置文件名不同，必须以内容而非本地化文案保持一致。
- 所有现有命令、选项、退出码、路径约束和 `KEY=VALUE` schema 是自动化接口，必须
  继续使用 ASCII；工作树已有未提交改动，不能覆盖无关内容。

## 需求

### R1. 网络和 OTA 组件边界

- 网络组件拥有 Wi-Fi、Setup AP、HTTP 服务启停与内嵌首页；runtime 只保留状态、API
  分发与网络驱动契约。
- OTA 组件拥有验证码、接收、CRC、分区更新和重启；OTA 未选时不链接实现或
  `app_update`，`/api/ota` 返回稳定的 501 结果。
- `main` 只经生成模块注册表管理可选模块，不直接依赖模块私有实现。
- 关闭 network 时不构建 `esp_bms_network`；OTA 通过 catalog 自动选择 network。
  投屏仍明确报告为 legacy runtime，本任务不承诺其裁剪。

### R2. 一次性语言选择

- 无参数启动 `start.sh`、`start.ps1` 或 `start.cmd` 时，先显示语言菜单，用户每次都
  选择简体中文或英文，随后才进入配置菜单。无效选择必须重新询问。
- 语言只保留在当前进程：不创建偏好文件，不写入 profile，也不影响下次启动。
- 非交互命令默认中文，并支持 `--lang zh|en` 覆盖本次调用；`build-local` 调用包装
  脚本时必须传递当前语言。
- 帮助、菜单、成功、诊断和错误均使用所选语言；命令、选项、模块 ID、GPIO、文件路径
  和生成文件字段保持可复制的 ASCII。

### R3. 跨平台一致性与兼容

- Bash 和 PowerShell 对相同输入生成相同的标准化配置内容、feature 开关和组件闭包，
  其中包含 audio、BMS、controller、GPS、network 与 OTA。
- 语言改动不得改变安全校验、命令语义、退出码、默认 legacy ESP32 profile，或 Setup AP
  的 `fuckingBms_` SSID、8 位数字密码及二维码同时显示 SSID/密码的行为。

## 验收标准

- [ ] 网络、OTA、CMake、catalog、生成注册表和 runtime feature 条件一致，legacy 默认
  profile 保留原有网络和 OTA 行为。
- [ ] network off 不含 `esp_bms_network`；ota off 不含 `esp_bms_ota` 或
  `app_update`，并以 component 闭包、archive/map/ELF/资源检查为证据。
- [ ] Bash/PowerShell 的 network、OTA on/off profile 生成等价；没有把 legacy 投屏
  误报为已裁剪。
- [ ] 每次无参数交互均先选择语言，中文/英文菜单和提示正确；`--lang` 有效且语言不会
  出现在任何生成配置文件中。
- [ ] `tests/configurator_selftest.sh`、针对性 profile 构建、`git diff --check` 和
  GitNexus `detect-changes` 通过；固件行为变动按 RFC2217 流程尝试实机验证并如实记录。

## 范围外

- 投屏的真实 component 迁移。
- 云构建后端、GUI 或 Web 配置器。
- 修改命令名、schema 或生成字段来实现本地化。
