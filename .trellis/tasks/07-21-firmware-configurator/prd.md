# 跨平台固件配置器

## Goal

用 Bash、PowerShell 和 CMD 提供相同的声明式配置、校验与生成体验，在烧录前形成可审计的唯一标准化配置。

最终用户入口固定在仓库根目录：Linux/macOS 使用 `./start.sh`，Windows
使用 `start.cmd`。`start.cmd` 转发给同目录的 `start.ps1`，不把 PowerShell
实现藏在 CMD 的不安全字符串拼接中。

## Requirements

- 实现无参数菜单及 `doctor/configure/validate/build-local/build-cloud` 命令；CMD 仅作 Windows 入口转发。
- Windows 入口应自动发现已安装的 ESP-IDF 6.0.2，并在当前进程配置 `IDF_PATH` 后导入环境；交互式本地编译找不到时询问是否安装。
- 两端安全解析同一版本化 `KEY=VALUE` catalog，禁止执行输入；所有模块和菜单项均从描述文件发现。
- 配置顺序固定为 MCU、板型、显示/输入、模块、模块参数、GPIO；旧板和 S3 提供默认值且允许覆盖。
- 确定性解析依赖/冲突/能力、GPIO 类型/占用/危险确认，拒绝缺项、非法值和循环依赖。
- 仪表选择与模块选择同样使用上下移动、空格切换、回车确认；仅在 GPS 或控制器蓝牙已选时显示，控制器仪表必须依赖控制器蓝牙。
- 每个 profile 生成独立目录、标准化配置、sdkconfig 输入、CMake 选择、模块注册表、Web 资源、分区和报告；不修改根 `sdkconfig`。
- 用户选择的模块必须驱动 ESP-IDF 组件可达性；模块尚未从当前单体运行时拆出前，配置器不得声称其已经从镜像移除。

## Acceptance Criteria

- [ ] Linux Bash 与 Windows PowerShell 对黄金 fixtures 的标准化输出逐字节相同。
- [ ] 恶意值、重复键、未知键/版本、路径穿越和 shell/PowerShell 载荷均被拒绝且不执行。
- [ ] 缺项、非法枚举、依赖补齐、冲突、循环依赖、重复/无能力 GPIO 和危险确认测试齐全。
- [ ] 新增测试模块只增加组件描述文件即可出现在菜单和生成结果中。
- [ ] 五个非交互命令具备稳定退出码和可用于 CI 的输出。
- [ ] GPS-only 配置仅包含 S1000RR/火刃仪表；未选 GPS 和控制器时保存空仪表列表。
