# 多 MCU 可裁剪固件与 ESP32-S3 迁移

## Goal

把当前仅面向经典 ESP32 的单体固件演进为声明式、可按组件图裁剪、可为 ESP-IDF 5.5.4 多目标生成独立构建的固件产品线，同时保持现有 ESP32-WROOM-32E 默认构建和实机行为兼容，并新增 ESP32-S3 WROOM-1-N16R8 的完整硬件配置。

## Background

- 当前根 `CMakeLists.txt` 通过 `COMPONENTS main` 限定组件可达性，但 `main/CMakeLists.txt` 固定依赖所有产品组件。
- `esp_bms_idf_runtime.c` 同时拥有 NVS、GPS、BLE、Wi-Fi、HTTP/OTA、投屏和状态投影，已超过 6500 行；Web UI 作为单个 `main/web/index.html` 嵌入运行时组件。
- 现有默认硬件是 ESP32-WROOM-32E、4 MB Flash、无 PSRAM、ST7789 SPI TFT 和 XPT2046 触摸；默认 GPIO 与双 OTA 分区已由项目规范固定。
- 当前工作区已有未提交的固件、UI、规范和硬件任务改动，本任务必须与其共存，不得回退或覆盖。

## Requirements

### R1. 经典 ESP32 兼容

- 在重构前冻结当前 ESP32 默认配置、GPIO、4 MB 分区、链接 map、size、关键 UI 状态和实机启动日志。
- 原有 `./scripts/esp-idf-env.sh build` 默认选择兼容配置；允许移动现有实现，但不保留重复死副本。
- Setup AP SSID 继续强制 `fuckingBms_` 加随机后缀，密码继续为 8 位随机数字；旧 NVS 凭据不符合策略时自动重生并保存，二维码同时显示 SSID 和密码。

### R2. 声明式跨平台配置器

- 提供零额外运行时依赖的 `scripts/firmware-wizard.sh`、`scripts/firmware-wizard.ps1` 和 Windows `.cmd` 启动器。
- 无参数进入终端菜单；非交互命令固定为 `doctor`、`configure`、`validate`、`build-local`、`build-cloud`。
- Bash 与 PowerShell 只把清单当数据解析，禁止 `source`、`eval` 或执行清单内容；相同输入必须生成字节一致的标准化配置。
- MCU、板型、显示、输入、模块、模块参数、GPIO、依赖、冲突、危险引脚、能力和设置贡献均由版本化 `KEY=VALUE` 清单/组件描述文件声明。
- 每个配置使用独立目录生成标准化配置、sdkconfig、组件依赖、模块注册表、Web 资源、分区表和构建报告，不修改根 `sdkconfig`。

### R3. 编译期模块裁剪

- 固定核心仅包含启动、NVS、LVGL、显示、输入和设置框架；可选模块依次为 BMS、GPS/GNSS、控制器 BLE、音频提示、设备联网与 Web 设置、本地 OTA、手机投屏。
- BLE、Wi-Fi、HTTP 等内部依赖自动补齐；依赖循环和声明冲突必须在构建前失败。
- 未选模块不参与编译、链接、TFT/Web 设置、API 注册或资源嵌入；不能只用运行时开关隐藏。
- TFT 与 Web 设置由核心和所选模块的能力贡献生成；共享下拉框在零/一/多选项时分别隐藏或阻止构建、自动选择并禁用、正常选择。

### R4. 多 MCU、S3 板型和分区

- MCU 目录覆盖 ESP-IDF 5.5.4 的 esp32、esp32s2、esp32c2、esp32c3、esp32c5、esp32c6、esp32c61、esp32h2、esp32p4、esp32s3。
- 能力矩阵必须禁止无 BLE、无 Wi-Fi 或缺少目标显示总线能力的组合；解析依赖锁使用 `dependencies.lock.${IDF_TARGET}` 隔离。
- ESP32-S3 默认板型为 WROOM-1-N16R8：I80 ILI9488、FT6336U、8 MB PSRAM、GNSS GPIO39/40/41、I2S GPIO42/47/48、AMP_SHDN GPIO2，且允许用户覆盖。
- S3 固定双 OTA：`ota_0=0x10000/0x600000`、`ota_1=0x610000/0x600000`、数据区 `0xC10000/0x3F0000`；旧 4 MB 分区保持不变。

### R5. 显示与输入

- 显示目录内置 ILI9341、ILI9488、ST7789、ST7796、GC9A01、NT35510、RGB+ST7701、SSD1306、SH1106、RM67162，以及 ESP32-P4 专用 ILI9881C/EK79007 MIPI-DSI。
- 输入后端支持触摸、按键和旋转编码器；GPIO 校验覆盖存在性、输入/输出能力、重复占用和目标保留引脚。
- 彩屏保留完整仪表 UI；128x64 OLED 使用独立精简状态页和分页设置，不加载彩屏动画和投屏页面。
- 新增或更新 UI preview 图片只放仓库根 `preview/`；TFT 未引入中文字体前只用 ASCII `ZH`/`EN` 表示语言状态，语言切换入口仅在设备设置中。

### R6. BMS BLE 协议

- BMS 模块一次包含 ANT、JK、JBD、Daly，不做品牌或协议级镜像裁剪，四品牌都出现在能力选项中。
- v1 协议集合固定为 ANT 新/旧 BLE；JK JK02_24S、JK02_32S、JK04；JBD FF00/FF01/FF02 与认证变体；Daly D2、P81、A5。
- 每个品牌具备 BLE 服务发现、所需认证、轮询、通知、分包/粘包处理、校验和解析分派；排除 UART、RS485、CAN、Heltec/NEEY 均衡器。
- 只采用许可证清晰的参考；优先 Apache-2.0 并保留 NOTICE。不得复制无许可证 JK 仓库；Daly A5 必须基于公开协议帧或抓包进行清洁实现。

### R7. 本地与云端构建

- `doctor` 检查 ESP-IDF 5.5.4、目标工具链、Git、CMake、Ninja、Python 环境、磁盘和网络；安装前先列问题并询问。
- Windows 10/11 与 apt/dnf/pacman Linux 可调用 Espressif 官方安装路径准备到用户目录；其他发行版给出完整人工命令。
- GitHub Actions API 只构建已推送分支；本地临时配置用校验后的编码输入传递，不自动提交或创建远端分支。
- Token 仅从 `ESP_BMS_GITHUB_TOKEN` 或当前会话读取，不写配置。
- 产物包含应用 bin、完整 flash 包、OTA bin、四位校验码、size、map、标准化配置摘要、构建报告和 SHA-256。

### R8. 验证和可信度标记

- Bash/PowerShell 黄金测试覆盖缺项、非法值、重复 GPIO、危险 GPIO 确认、依赖补齐和循环依赖。
- 每个可选模块都执行启用/禁用构建，并用组件图、map、符号和资源字符串证明未选模块未进入镜像。
- BMS 测试覆盖黄金帧、损坏帧、分包、粘包、CRC/校验、字段边界和协议切换。
- GitHub Actions 构建十个 MCU 的合法基准配置；只有通过实际设备验证的板型可标记 `hardware-verified`，其余明确标为协议兼容但未实机验证。
- 经典 ESP32 使用固定 LAN RFC2217 流程验证启动、显示、触摸、BMS、GPS、Web 和 OTA；S3 在样板可用后验证 I80、触摸、PSRAM、I2S、GNSS、USB 和双 OTA。

## Acceptance Criteria

- [ ] AC1: 旧 ESP32 默认命令构建成功，冻结基线与最终镜像的 GPIO、分区和关键启动/UI 行为对比通过。
- [ ] AC2: Bash 与 PowerShell 对所有黄金清单生成字节一致的标准化配置，恶意清单内容不会被执行。
- [ ] AC3: 十个 MCU 均有合法基准配置；不支持的能力组合在配置期被拒绝，S3 默认配置生成指定 GPIO、PSRAM 和 16 MB 分区。
- [ ] AC4: 七个可选模块均有 on/off 构建证据，off 镜像中不存在对应组件、符号、API 路由、设置文案和嵌入资源。
- [ ] AC5: TFT/Web/API/下拉框在零、一、多能力贡献场景行为一致，中文默认与设置内语言切换规则不回退。
- [ ] AC6: ANT、JK、JBD、Daly 协议测试矩阵通过，许可证/NOTICE 完整，所有未实机协议在报告中如实标记。
- [ ] AC7: 本地与 GitHub Actions 构建都输出规定产物、四位校验码和 SHA-256，Token 与临时配置不落盘到版本化配置。
- [ ] AC8: 主机测试、ESP-IDF 构建、`git diff --check`、GitNexus `detect-changes` 以及适用的 RFC2217 实机验证全部通过，且没有覆盖任务开始前的用户改动。

## Out Of Scope

- BMS 的 UART、RS485、CAN 接口及 Heltec/NEEY 均衡器。
- 在没有公开帧或合法抓包证据时猜测 Daly A5 协议。
- 为 TFT 引入中文点阵字库；当前仅保留 ASCII 语言状态。
- 宣称未接入实机的 MCU、板型或 BMS 型号已通过硬件验证。
