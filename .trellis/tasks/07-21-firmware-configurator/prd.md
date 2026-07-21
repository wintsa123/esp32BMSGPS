# 跨平台固件配置器

## Goal

用 Bash、PowerShell 和 CMD 提供相同的声明式配置、校验与生成体验，在烧录前形成可审计的唯一标准化配置。

最终用户入口固定在仓库根目录：Linux/macOS 使用 `./start.sh`，Windows
使用 `start.cmd`。`start.cmd` 转发给同目录的 `start.ps1`，不把 PowerShell
实现藏在 CMD 的不安全字符串拼接中。

## Requirements

- 实现无参数菜单及 `doctor/configure/validate/build-local/build-cloud` 命令；CMD 仅作 Windows 入口转发。
- 两端安全解析同一版本化 `KEY=VALUE` catalog，禁止执行输入；所有模块和菜单项均从描述文件发现。
- 配置顺序固定为 MCU、板型、显示/输入、模块、模块参数、GPIO；旧板和 S3 提供默认值且允许覆盖。
- 确定性解析依赖/冲突/能力、GPIO 类型/占用/危险确认，拒绝缺项、非法值和循环依赖。
- 每个 profile 生成独立目录、标准化配置、sdkconfig 输入、CMake 选择、模块注册表、Web 资源、分区和报告；不修改根 `sdkconfig`。
- 用户选择的模块必须驱动 ESP-IDF 组件可达性；模块尚未从当前单体运行时拆出前，配置器不得声称其已经从镜像移除。

## Acceptance Criteria

- [ ] Linux Bash 与 Windows PowerShell 对黄金 fixtures 的标准化输出逐字节相同。
- [ ] 恶意值、重复键、未知键/版本、路径穿越和 shell/PowerShell 载荷均被拒绝且不执行。
- [ ] 缺项、非法枚举、依赖补齐、冲突、循环依赖、重复/无能力 GPIO 和危险确认测试齐全。
- [ ] 新增测试模块只增加组件描述文件即可出现在菜单和生成结果中。
- [ ] 五个非交互命令具备稳定退出码和可用于 CI 的输出。
