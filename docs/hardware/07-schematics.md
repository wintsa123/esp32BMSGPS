# ESP32-S3 触屏车机硬件方案

- 日期：2026-07-19
- 阶段：8 - R1 原理图与结构化连接表
- 模式：快速模式

## 1. 交付状态

原理图已在 EasyEDA 客户端中实际创建并保存，工程文件位于：

`/home/wintsa/文档/LCEDA-Pro/projects/esp32BmsGps-Hardware-R1.eprj2`

| 项目 | 值 |
|---|---|
| EasyEDA 工程 UUID | `e98c2fda4d3741503d38837bbb806131d742fc2b682d21fbac3aef6f473f8c9e` |
| 原理图 UUID | `3487150dc4fb72de` |
| PCB UUID | `3cbb78f5012cc03c` |
| 工程格式 | EasyEDA 离线 `.eprj2`（SQLite） |
| 原理图页数 | 6 |
| 真实器件统计 | 132 个；J6/R38～R41 已加入，位号无重复 |
| 导线统计 | 347 条 |
| 数据库完整性 | 修复后快照 `PRAGMA integrity_check = ok` |
| ERC 结果 | J6 更新后严格模式 `fatal=0`、`error=0`、`warn=9`、`info=153`；9 条均已从 UI 面板归因 |
| PCB 同步 | 132 个器件、411 个焊盘（含 4×M3 NPTH）、89 个网络；保存关闭重开后持久化 |
| PCB 布局/布线 | `112×68mm` 板框、屏幕投影、4×M3、两个竖向槽与机械禁布区已落地；132 个器件均在板内，22 个 Pogo 在底层；电气走线、过孔、覆铜均为 0；`PROVISIONAL / DO NOT FAB` |

EasyEDA 3.2.166 的 beta 页重命名接口返回 `false`，因此左侧工程树仍显示 `P1`～`P6`；每页画布标题和标题栏 `Name` 已写入下表的正式功能名称，不影响电气网络和页序。

| 页序 | 图页 UUID | 正式功能名称 | 真实器件 | 导线 | 主要内容 |
|---:|---|---|---:|---:|---|
| P1 | `afc1ac792cf611ab` | `01_USB_INPUT_PROTECTION` | 21 | 68 | USB-C、CC、USB ESD、保险丝、TVS、eFuse |
| P2 | `74c6bbff103aba94` | `02_POWER_TREE` | 21 | 46 | 双路 SY8113I，3V3 与 5V_AUDIO |
| P3 | `1c06629878aeb7db` | `03_ESP32_USB_DEBUG` | 17 | 64 | ESP32-S3、GPIO 合同、EN/BOOT、调试与 J6 扩展接口 |
| P4 | `de250bfa176ac6ba` | `04_LCD_TOUCH` | 22 | 68 | 40Pin LCD、6Pin CTP、I80 串阻、背光 |
| P5 | `8623d0b0fa00e882` | `05_GNSS_RF` | 16 | 39 | ATGM336H-F8N76、VBAT、U.FL、有源天线馈电 |
| P6 | `e31f831e38bafbf8` | `06_AUDIO_TEST` | 35 | 62 | HT517、喇叭、默认关断、Pogo 测试点 |

P5 的 3 个、P6 的 5 个空名网络端口均已删除后重建为有名称 `BI` 端口；保存、关闭、重新打开后网络名仍完整。引脚级审计同时确认无缺失封装、无 `AddIntoPcb=false`、无未标记悬空脚、无 NC 误接、无两端器件同网和端口/导线网络冲突。

修复后安全快照为 `/home/wintsa/文档/LCEDA-Pro/projects/esp32BmsGps-Hardware-R1.post-erc-pass-20260718-233800.eprj2`，SHA-256 `9c0c6104e162cb3afa9af3199b7d0adc88af70743feb33c0dbf5c1ab93ebe7ee`。

## 2. 关键器件与 EasyEDA 库核对

| 位号 | 器件 | EasyEDA 库核对 | 备注 |
|---|---|---|---|
| J1 | KH-TYPE-C-16P | 精确型号，LCSC `C709357` | 16Pin USB-C 母座 |
| F1 | JFC1206-1300FS | 精确型号，LCSC `C136347` | 3A/63V 贴片保险丝 |
| D1 | SMBJ17A | 东沃条目，LCSC `C284003` | 17V 单向 TVS |
| U1 | USBLC6-2SC6 | UMW 条目，LCSC `C2687116` | USB D+/D- ESD |
| U2 | SGM2536FRXTSP10G/TR | LCSC `C48384692`；实例封装 `VQFN-10_L2.0-W2.0-P0.45-TL` | BOM/PCB 启用，pin3=`SPGD`、pin4=`nFAULT` |
| U3/U4 | SY8113IADC | 精确型号，LCSC `C479075` | 两路独立 Buck |
| U5 | ESP32-S3-WROOM-1-N16R8 | 精确型号，LCSC `C2913202` | 16MB Flash、8MB Octal PSRAM |
| J2 | FPC-0.5HF-40PWBH20 | LCSC `C3446044`；`FPC-SMD_40P-P0.50_FPC-0.5HF-40PWBH20` | 40 个信号脚保持原网络，41/42 机械脚 NC |
| J3 | FH12-6S-0.5SH(55) | LCSC `C202118`；`FPC-SMD_FH12-6S-0.5SH-55` | 6 个信号脚保持原网络，7/8 机械脚 NC |
| J6 | 2.0-5P WZ-D | LCSC `C41361332`；`CONN-TH_5P-P2.00_2.0-5PWZ-D` | PH2.0 卧式弯插通孔；不耐回流，回流后手工/选择焊 |
| R38 / R39～R41 | 0Ω/0805 / 33Ω/0603 | LCSC `C17477 / C23140`，均为基础料 | 3V3 隔离位与三路扩展信号串联阻尼 |
| D3 | USBLC6-4SC6-ES | ElecSuper，LCSC `C5180279` | CTP 四路低电容 ESD；Rev-1.5 PDF 已校验 |
| U6 | ATGM336H-F8N76 | 精确型号，LCSC `C46955980` | 全模多频 GNSS |
| D2 | BAV199 | SOT-23 串联双二极管符号 | 仅使用 pin1→pin2 单结，pin3 NC |
| D4 | PESD5V0R1BSFYL | Nexperia，LCSC `C461221` | SOD-962；DC-biased RF 适用边界未关闭，强制 DNP |
| C16 | CHP5R5L105R-TW 5.5V/1F | LCSC `C2925920`；`CAP-TH_L16.0-W8.0-P11.5-D0.6` | 横卧 H 型，pin1=`GNSS_VBAT`、pin2=`GND` |
| U7 | HT517SPER | 精确型号，扩展库条目 | I2S 单声道 Class-D |
| TP1～TP22 | POGO_PAD_2V0_ENIG_R1 | `TESTPOINT-SMD_BD1.5_P70-5000045R` | 单引脚、约 2.00mm 无孔圆形 SMD 裸盘；BOM 排除、PCB 启用 |

### U2 实例说明

EasyEDA 系统库无独立 FR 条目。R1 使用同封装、引脚位置兼容的系统母件创建 FR 专用实例，并在实例源码中把 pin3/pin4 精确覆写为 `SPGD/nFAULT`；型号、LCSC 料号、说明和两个故障网络也全部按 FR 版本设置。该实例已启用 BOM/PCB，10 个引脚端点逐脚回读正确。不得恢复成 PR 版本的 `PG/PGTH` 网络。

## 3. P1 - USB 输入与保护

### 3.1 主电源路径

| 网络 | 源端 | 目标端 | 参数/说明 |
|---|---|---|---|
| `USB_VBUS` | J1.A4/A9/B4/B9 | F1.1 | USB-C VBUS 四脚并联 |
| `VBUS_FUSED` | F1.2 | D1.K、U2.5(IN) | F1=`3A/63V` |
| `GND` | D1.A | 系统地 | D1=`SMBJ17A`，阴极接 `VBUS_FUSED` |
| `VIN_PROT` | U2.6(OUT) | P2 U3/U4 输入 | eFuse 后受保护母线 |

### 3.2 USB 与 CC

| 网络 | 连接 | 参数 |
|---|---|---|
| `USB_DN` | J1.A7/B7 ↔ U1.I/O1 ↔ U5.GPIO19 | U1=`USBLC6-2SC6` |
| `USB_DP` | J1.A6/B6 ↔ U1.I/O2 ↔ U5.GPIO20 | U1 参考端接 `3V3`，GND 就近回流 |
| `USB_CC1` | J1.A5 → R1 → GND | R1=`5.1kΩ/1%` |
| `USB_CC2` | J1.B5 → R2 → GND | R2=`5.1kΩ/1%` |

### 3.3 SGM2536FR 设置

| 引脚/网络 | 配置 | 标称结果 |
|---|---|---|
| EN/UVLO | 470kΩ / 169kΩ | 启动约 `4.54V` |
| OVLO | 470kΩ / 44.2kΩ | 上升约 `13.96V`，下降约 `12.68V` |
| ILIM | 1.40kΩ 到 GND | 约 `2.38A` |
| SS | 1.1nF 到 GND | 软启动 |
| ITIMER | 2.2nF 到 GND | 过流消隐 |
| SPGD | `EFUSE_SPGD_TP` | 仅测试点，不进入保护闭环 |
| nFAULT | `EFUSE_NFAULT_TP` | 仅测试点，不进入保护闭环 |

U2 IN/OUT 各放置 `100nF + 10µF`。12V 通过 USB-C 属于专用非标准线束，禁止接手机或普通 USB 设备。

## 4. P2 - 双路电源树

### 4.1 3V3

| 项目 | 连接/数值 |
|---|---|
| U3 | SY8113IADC，IN/EN=`VIN_PROT`，GND=`GND` |
| 开关节点 | U3.LX → L1.1，网络 `SW_3V3` |
| 电感 | L1=`4.7µH`，首板要求 `Isat≥4A` 并复核 DCR/温升 |
| 反馈 | RH R8=`100kΩ`，RL R9=`22.1kΩ`，FB=`FB_3V3` |
| 前馈 | C5=`220pF` 跨 RH |
| 启动 | C6=`100nF`，BS 到 `SW_3V3` |
| 输入 | C7=`10µF/25V` + 100nF |
| 输出 | C8/C9=`2×22µF/25V` |
| 额定能力 | `3.3V/2A` 持续，需实测 Wi-Fi/BLE 峰值与 brownout |

### 4.2 5V_AUDIO

| 项目 | 连接/数值 |
|---|---|
| U4 | SY8113IADC，IN/EN=`VIN_PROT` |
| 开关节点 | U4.LX → L2.1，网络 `SW_5V_AUDIO` |
| 电感 | L2=`6.8µH`，首板要求 `Isat≥4A` |
| 反馈 | RH R10=`162kΩ`，RL R11=`22.1kΩ`，FB=`FB_5V_AUDIO` |
| 前馈/启动 | C10=`220pF`，C14=`100nF` |
| 输入/输出 | C11=`10µF/25V`；C12/C13=`2×22µF/25V` |
| 功放本地去耦 | C34=`100nF` + C35=`10µF` 默认装配；C36=`470µF/16V` 强制 DNP |
| 额定能力 | `5V/1A` 持续、`1.5A` 音频瞬态；5V 输入允许压差降压 |

C36 保留低 ESR 聚合物 SMD 封装，但当前不进入 BOM。现有约 44µF Buck 输出电容加 10µF 与 470µF 后总负载约 524µF；按 1ms 软启动估算理想充电电流约 2.62A，高于 2.38A eFuse 标称限流，可能导致掉压或反复重启。试装前必须在 5V/12V 输入下验证启动、反复通断和 `nFAULT`。

## 5. P3 - ESP32-S3 GPIO 合同

| ESP32-S3 引脚 | 网络 | 目标 |
|---|---|---|
| GPIO4～GPIO11 | `LCD_D0`～`LCD_D7` | P4 R14～R21 → J2.DB0～DB7 |
| GPIO12 | `LCD_WR` | P4 R22 → J2.WR |
| GPIO13/14/15 | `LCD_DC` / `LCD_CS` / `LCD_RST` | J2.10/9/15 |
| GPIO16 | `LCD_BL_PWM` | P4 Q1.G |
| GPIO17/18 | `CTP_SDA` / `CTP_SCL` | J3.2/3，4.7kΩ 上拉 |
| GPIO19/20 | `USB_DN` / `USB_DP` | J1/U1；模块侧 22Ω 串阻 |
| GPIO21/38 | `CTP_INT` / `CTP_RST` | J3.4/5 |
| GPIO39/40/41 | `GNSS_RX` / `GNSS_TX` / `GNSS_1PPS` | U6.2/3/4 |
| GPIO42/47/48 | `I2S_BCLK` / `I2S_LRCK` / `I2S_SDOUT` | U7.DI0/DI1/DI2 |
| GPIO1 | `GPIO1_EXP_RAW` | R39 → `EXP_GPIO1` → J6.3 |
| GPIO43/44 | `DBG_UART_TX_GPIO43` / `DBG_UART_RX_GPIO44` | 保留 TP8/TP9，并经 R40/R41 → J6.4/J6.5 |
| GPIO2 | `AMP_SHDN` | P6 Q2.G |
| GPIO3 | `LCD_TE_DNP` | J2.FMARK，经 0Ω DNP |
| GPIO0 | `BOOT_GPIO0` | SW2、10kΩ 上拉 |
| EN | `ESP_EN` | SW1、R12=10kΩ 上拉、C15=`1µF/50V X5R 0603` 到 GND |

限制：GPIO35～37 被 N16R8 Octal PSRAM 占用，不外接；GPIO45/46 为启动绑带脚，保持 NC；WROOM-1 天线端必须在板边并执行全层禁铜/禁器件。

### 5.1 J6 壳内扩展接口

| J6 引脚 | 网络 | 连接/限制 |
|---:|---|---|
| 1 | `GND` | 系统地 |
| 2 | `3V3_EXP` | `3V3 → R38(0Ω/0805) → J6.2`；只允许主板输出，建议持续≤300mA，禁止反灌 |
| 3 | `EXP_GPIO1` | `U5.GPIO1 → GPIO1_EXP_RAW → R39(33Ω) → J6.3` |
| 4 | `EXP_UART_TX_GPIO43` | `U5.TXD0/TP8 → R40(33Ω) → J6.4`；上电可能输出 ROM/启动日志 |
| 5 | `EXP_UART_RX_GPIO44` | `U5.RXD0/TP9 → R41(33Ω) → J6.5`；外设启动时不得强驱冲突 |

J6 中心为 `(16.51,58.42)mm、0°`，Pad1～Pad5 从左到右排列，连接器本体位于左下板边内侧并朝外；R38～R41 位于其上方。严格 PCB DRC 已确认新增器件无 Clearance/Keepout 错误。该接口只按机壳内部、非热插拔扩展口使用；若未来外露，必须补做 ESD/浪涌/短路/线缆 EMI/防水设计。

## 6. P4 - LCD 与 CTP

### 6.1 LCD 40Pin

| J2 引脚 | 网络/处理 |
|---:|---|
| 1～4 | NC，旧电阻触摸脚不使用 |
| 5、16、37 | GND |
| 6、7 | 3V3（IOVCC/VCC） |
| 8 | `LCD_TE_DNP` |
| 9、10 | `LCD_CS`、`LCD_DC` |
| 11 | `LCD_WR_FPC`，经 R22=22Ω 接 `LCD_WR` |
| 12 | 10kΩ 上拉至 3V3，RD 不接 MCU |
| 13、14 | NC |
| 15 | `LCD_RST` |
| 17～24 | DB0～DB7，经 R14～R21=22Ω 接 `LCD_D0`～`LCD_D7` |
| 25～32 | NC，16 位高数据位不使用 |
| 33 | LEDA=`3V3` |
| 34～36 | 汇流为 `LCD_LED_K` |
| 38、39、40 | IM0/IM1/IM2=`1/1/0`，选择 8 位 I8080 |

背光路径：`LCD_LED_K → R23(2.2Ω/0805 初值) → Q1.D`，Q1=`2N7002`，Q1.S=GND，Q1.G=`LCD_BL_PWM`。`1.5Ω` 与 `3.3Ω` 作为 EVT 替换值，最终按屏样品电流、压降和温升冻结。

### 6.2 CTP 6Pin

| J3 引脚 | 网络 |
|---:|---|
| 1 | 3V3 |
| 2 | `CTP_SDA`，R24=4.7kΩ 上拉 |
| 3 | `CTP_SCL`，R25=4.7kΩ 上拉 |
| 4 | `CTP_INT` |
| 5 | `CTP_RST` |
| 6 | GND |

J2 已锁定 `FPC-0.5HF-40PWBH20 / C3446044`，J3 已锁定 `FH12-6S-0.5SH(55) / C202118`，电气连接和机械封装均可进入 PCB。放置前仍须用屏幕实物核对触点面、FPC 插入方向和锁扣操作空间；这是机械验证项，不再是原理图转 PCB 的库阻塞项。

## 7. P5 - GNSS 与射频

| U6 引脚 | 网络/处理 |
|---:|---|
| 1、10、12 | GND |
| 2 TXD0 | `GNSS_RX`（ESP32 接收） |
| 3 RXD0 | `GNSS_TX`（ESP32 发送） |
| 4 1PPS | `GNSS_1PPS` |
| 5 ON/OFF | `GNSS_ONOFF_TP`，保持正常开启，不接 GPIO |
| 6 VBAT | `GNSS_VBAT` |
| 7、13、15～18 | NC |
| 8 VCC | `3V3_GNSS` |
| 9 nRESET | `GNSS_NRESET_TP`，保持非复位，不接 GPIO |
| 11 RF_IN | `GNSS_RF_MODULE` |
| 14 VCC_RF | `GNSS_VCC_RF` → L3=47nH → `GNSS_RF_ANT` |

VBAT：`3V3 → R26 330Ω → D2 BAV199 pin1→pin2 → GNSS_VBAT/C16正极`，C16负极到 GND，D2.pin3 NC。

C16 实例参数为 5.5V/1F、漏电 `6µA@72h`、ESR `360mΩ@1kHz`、容量容差 `0%～+100%`、工作温度 `-40～+70℃`；封装本体约 16×8mm、脚距 11.5mm、孔径 0.6mm。PCB 阶段需保留卧装空间并用中性无腐蚀胶抗振固定。

RF 主通路已真实拆网：`U6.11 → GNSS_RF_MODULE → R34(0Ω/0402) → GNSS_RF_ANT → J4.SIG`；L3=47nH 从 `GNSS_VCC_RF` 向 `GNSS_RF_ANT` 注入 3.3V 有源天线偏置。C32/C33=`1pF C0G/0402` 为 π 匹配支路并强制 DNP，只有 VNA/定位实测后才允许调整。

D4=`PESD5V0R1BSFYL / C461221` 位于天线侧并连接 GND，但强制 DNP。Nexperia v2 明确写明该器件不用于连接 DC supply 的线路；v3 改为警告不得连接无限流 DC 源，以免击穿后 snap-back 状态持续。`GNSS_RF_ANT` 带 3.3V 偏置，现有偏置限流与保护器件的适用边界尚未关闭，因此 D4 不能默认装配。替代器件必须明确支持 DC-biased RF 馈线、结电容不高于 0.2pF，并通过插损、C/N0、定位和 OPEN/OK/SHORT 验证。J4 到 SMA 防水穿墙母座使用短 RG1.13 50Ω 尾线，RF 线上不增加测试点。

P5 本地去耦为 C30=`10µF/25V`、C31=`100nF/50V`。TP19=`GNSS_ONOFF_TP`、TP20=`GNSS_NRESET_TP`、TP21=`GNSS_VBAT_CHG`、TP22=`GNSS_VBAT`；ON/OFF 与 nRESET 不接 ESP32，夹具只允许按模块手册下拉控制。

## 8. P6 - 音频与产测

### 8.1 HT517

| U7 引脚 | 网络/处理 |
|---:|---|
| 1 DI0 | `I2S_BCLK_AMP`，经 R35=33Ω 接 `I2S_BCLK` |
| 2 DI2 | `I2S_SDIN_AMP`，经 R36=33Ω 接 `I2S_SDOUT` |
| 3 GAIN/CH | `AMP_GAIN_CH`，R27=100kΩ 到 5V_AUDIO |
| 4 EN | `AMP_EN`；低电平关断 |
| 5 VDD | 5V_AUDIO |
| 6 OUTN | `AUDIO_OUTN_AMP` → FB2(0Ω默认) → `AUDIO_OUTN` → J5.2 |
| 7 OUTP | `AUDIO_OUTP_AMP` → FB1(0Ω默认) → `AUDIO_OUTP` → J5.1 |
| 8 DI1 | `I2S_LRCK_AMP`，经 R37=33Ω 接 `I2S_LRCK` |
| 9 EP/GND | 大面积 GND 与热过孔 |

默认关断逻辑：R28 将 `AMP_SHDN` 上拉至 3V3，Q2 导通把 `AMP_EN` 拉低；固件将 `AMP_SHDN` 拉低后 Q2 关断，R29 将 `AMP_EN` 上拉至 5V_AUDIO，功放开启。

U7 本地默认装 C34=100nF、C35=10µF；C36=470µF/16V 强制 DNP。R35～R37 为 33Ω/0603 默认装配并靠 ESP32 源端，C39～C41 为 10pF C0G/0402、靠 U7 接收端且强制 DNP。

OUTP/OUTN 为 BTL 差分输出，喇叭任一端都不得接 GND。FB1/FB2 使用 `0805W8F0000T5E / C17477` 的 0Ω 默认装配；磁珠替代需额定不低于 2A、低 DCR 且两路对称验证。C37/C38=1nF C0G/0603 从连接器侧两路输出分别到 GND，强制 DNP；不得在仍装 0Ω 时把它们描述为已验证的 EMI 滤波器。

### 8.2 Pogo 测试点

| 测试点 | 网络 | 测试点 | 网络 |
|---|---|---|---|
| TP1 | VIN_PROT | TP8 | DBG_UART_TX_GPIO43 |
| TP2 | 3V3 | TP9 | DBG_UART_RX_GPIO44 |
| TP3 | GND | TP10 | GNSS_RX |
| TP4 | ESP_EN | TP11 | GNSS_TX |
| TP5 | BOOT_GPIO0 | TP12 | GNSS_1PPS |
| TP6 | USB_DN | TP13 | LCD_BL_PWM |
| TP7 | USB_DP | TP14 | AMP_SHDN |
| TP15 | USB_VBUS | TP16 | 5V_AUDIO |

| 测试点 | 网络 | 测试点 | 网络 |
|---|---|---|---|
| TP17 | EFUSE_SPGD_TP | TP18 | EFUSE_NFAULT_TP |
| TP19 | GNSS_ONOFF_TP | TP20 | GNSS_NRESET_TP |
| TP21 | GNSS_VBAT_CHG | TP22 | GNSS_VBAT |

TP1～TP22 已全部替换为单引脚、可正常转 PCB 的无孔圆形 SMD 焊盘，公共封装为 `TESTPOINT-SMD_BD1.5_P70-5000045R`，库焊盘回读直径约 2.00mm。PCB 中必须全部翻到底层、保持 2.54mm 针床节距、使用沉金、不装排针或测试连接器，并从钢网开口中排除；TP6/TP7 的 USB D-/D+ 测试支路还需复核长度、对称性和差分间距。

## 9. ERC 警告与批准边界

J6 更新后的保存重开严格 ERC 共 162 条：`fatal=0`、`error=0`、`warn=9`、`info=153`。9 条 warning 已从 EasyEDA DRC 面板逐条核对：

| 序号 | 实际 warning | 处置 |
|---:|---|---|
| 1 | 导线 `$1N1` 有多个网络名：`USB_VBUS、USB_VBUS` | 两个名称完全相同，无跨网短接；保留为重复标记例外 |
| 2 | 导线 `$2N8` 有多个网络名：`FB_3V3、FB_3V3` | 两个名称完全相同，无跨网短接；保留为重复标记例外 |
| 3 | 导线 `$2N27` 有多个网络名：`FB_5V_AUDIO、FB_5V_AUDIO` | 两个名称完全相同，无跨网短接；保留为重复标记例外 |
| 4 | 导线 `$3N33` 有多个网络名：`DBG_UART_RX_GPIO44、DBG_UART_RX_GPIO44` | J6 UART RX 分支与原 TP9 网络名称完全相同，无跨网短接 |
| 5 | 导线 `$3N34` 有多个网络名：`DBG_UART_TX_GPIO43、DBG_UART_TX_GPIO43` | J6 UART TX 分支与原 TP8 网络名称完全相同，无跨网短接 |
| 6 | 器件属性与供应商编号不匹配，建议器件标准化：第一组 | 库实例/自定义属性标准化建议，不改变符号引脚、封装或网络 |
| 7 | 器件属性与供应商编号不匹配，建议器件标准化：第二组 | 同上；投板前继续以实例型号、封装和采购清单交叉核对 |
| 8 | 器件属性与供应商编号不匹配，建议器件标准化：第三组 | 同上；不把该建议误报为电气通过或无警告 |
| 9 | 器件属性与供应商编号不匹配，建议器件标准化：含 J6/R38～R41 的第四组 | 新增器件使用系统库精确封装/料号，网络和 PCB 同步已回读；保留标准化建议 |

153 条 info 主要是空的可选属性提示，不作为电气错误。独立引脚级审计另行确认：J2/J3 机械脚、U5 未使用/绑带脚、U6.7/13/15～18、D2.3 均以真实 NC 表达；U5.GPIO1 已解除 NC 并经 R39 接 J6；所有 DNP 位仍 `AddIntoPcb=true` 以保留焊盘，但 `AddIntoBom=false`。其中 D4 与 C36 是强制 DNP，不得因 ERC `error=0` 而默认装配。

<div style="break-before: page; page-break-before: always;"></div>

## 10. 原理图评审结论

R1 已覆盖输入保护、两路电源、主控与下载调试、显示触摸、GNSS、音频和产测网络。原先阻塞 PCB 转换的 4 项已经全部关闭：

1. U2 已建立 FR 专用实例并绑定 2×2mm 10Pin 封装，BOM/PCB 启用。
2. J2/J3 已替换为最终连接器实例和精确封装，信号网络保持不变。
3. C16 已替换为 CHP5R5L105R-TW 横卧 H 型实例和精确封装。
4. TP1～TP22 已替换为可转 PCB 的单引脚无孔 SMD 裸盘。

以上 4 项库/封装阻塞、原理图电气门和原理图→PCB 同步门均已关闭。J6/R38～R41 增量交互导入后再次保存、关闭、重开，PCB UUID `3cbb78f5012cc03c` 回读为 132 个器件、411 个焊盘（含 4×M3 NPTH）、89 个网络；新增位号和网络均持久化。

PCB 已进入机械冻结后的布局阶段：`112×68mm` 板框内有 132 个器件，TP1～TP22 位于底层 2.54mm 阵列，ESP32 天线端伸出 `94.57×60.88mm` 屏幕投影。40Pin/6Pin 槽均为竖向，J2/J3 入口均朝左；J6 位于左下板边内侧。电气走线、过孔和覆铜仍为 0，严格 DRC 仅返回 `Connection Error (374)`，Clearance/Keepout 为 0。嘉立创EDA原生自动布线对 89 个网络仍以 `success=false、duration=0` 失败且未生成线/孔。正式投板前仍须用屏幕和 J6 线束实物关闭折弯、触点面、锁扣、绝缘和插拔余量，并完成 4 层规则、布线覆铜、Pogo 钢网排除、飞线清零、严格 DRC 和 EVT/DFM；在此之前统一标记 `PROVISIONAL / DO NOT FAB`。
