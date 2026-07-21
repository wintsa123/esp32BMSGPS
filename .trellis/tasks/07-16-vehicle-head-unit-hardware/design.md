# 高性能触屏车机 R1 原理图与 PCB 技术设计

## 1. 范围与边界

本阶段交付可在 EasyEDA 中继续布局布线的 R1 原理图，覆盖 USB-C 输入与原生 USB、输入保护、3.3V/5V 电源、ESP32-S3、4.0 英寸 I80 显示与电容触摸、GNSS 与外置有源天线、I2S 单声道功放、维修按键和产测点。

原理图阶段最初不冻结 PCB 板框、安装孔和 FPC 机械位置；后续已按用户确认更新为 `112×68mm` 板框、4×M3、屏幕投影、两个竖向槽及 J2/J3 坐标。外壳、线束长度和正式认证结论仍不在本轮冻结范围。执行顺序为：先关闭原理图电气完整性门，再同步到 PCB；屏幕实物装配证据、飞线清零和严格 DRC 完成前，PCB 仍只能标记为 `PROVISIONAL / DO NOT FAB`，不得进入投板结论。

## 2. 设计依据与快速模式假设

- 主控：`ESP32-S3-WROOM-1-N16R8`，16MB Flash、8MB Octal PSRAM；GPIO35～37 不外用。
- 显示：4.0 英寸、ILI9488、8 位 I8080、40Pin/0.5mm 上接；CTP 为 FT6336U、6Pin/0.5mm 下接。
- 屏幕证据：仓库根目录 `4.0寸I8080接口模块40001数据手册（鸿讯电子）.pdf`。该资料是转接板手册，R1 只复用其裸屏 FPC 连接关系，不复用 34Pin 模块接口。
- 背光假设：LEDA 接 3.3V，LEDK1～3 汇流后经 `2.2Ω/0805` 与低端开关 PWM 调光；`1.5Ω/3.3Ω` 作为替换料，最终值由样品电流和温升决定。
- 输入保护安全回退：`PW1558` 缺少完整门限迟滞和最坏容差资料，R1 改用原厂资料完整的 `SGM2536FR`。功能边界不变。
- PCB 采用 4 层、L2 连续 GND、整板沉金；此约束只影响原理图网络命名和测试点规划。
- 原理图转 PCB 的库资源已落地：U2 为 `SGM2536FRXTSP10G/TR / C48384692` 专用实例，J2/J3 为 `C3446044/C202118` 精确连接器，C16 为 `CHP5R5L105R-TW / C2925920` 横卧实例，TP1～TP22 为单引脚约 2.00mm 无孔 SMD 裸盘。

## 3. 原理图分页

1. `01_USB_INPUT_PROTECTION`：USB-C、CC 电阻、USB ESD、保险丝、TVS、eFuse。
2. `02_POWER_TREE`：双路 SY8113I、3V3/5V_AUDIO、GNSS 滤波与电源预算。
3. `03_ESP32_USB_DEBUG`：ESP32-S3、EN/BOOT、原生 USB、调试 UART、去耦。
4. `04_LCD_TOUCH`：40Pin LCD、6Pin CTP、I80 串阻、背光 PWM、触摸 ESD。
5. `05_GNSS_RF`：ATGM336H-F8N-76、VBAT 超级电容、UART/1PPS、U.FL 与有源天线馈电。
6. `06_AUDIO_TEST`：HT517SPER、喇叭座、EMI 调试位、维修按键与 Pogo 测试点。

## 4. 电源与保护计算

### 4.1 USB-C 与 eFuse

- USB-C：`KH-TYPE-C-16P`，CC1/CC2 各 `5.1kΩ/1%` 下拉；D+/D- 使用 UMW USBLC6-2SC6，阵列正参考接 3V3 并就近 `100nF`。
- VBUS 顺序：`USB_VBUS → JFC1206-1300FS 3A → SMBJ17A → SGM2536FR → VIN_PROT`。
- OVLO：`470k/44.2k`，标称上升门限 `1.20 × (470+44.2)/44.2 = 13.96V`，标称下降门限 `1.09 × (470+44.2)/44.2 = 12.68V`。
- UVLO/EN：`470k/169k`，标称启动约 `4.54V`，为 4.75V 最低正常输入保留容差。
- 限流：`RILIM=1.40kΩ`，按 `3334/RILIM` 得标称约 `2.38A`。
- 软启动：`CSS=1.1nF`；过流消隐 `CITIMER=2.2nF`；IN/OUT 各放 `100nF + 10µF`。
- eFuse 使用自动重试版本；`SPGD/nFAULT` 仅引出测试点，不参与保护闭环。

### 4.2 3.3V 主电源

- `SY8113IADC`，VIN=VIN_PROT，VOUT=3V3，持续设计能力 2A。
- `L=4.7µH`，`RH=100kΩ`，`RL=22.1kΩ`，`CFF=220pF`，`COUT=2×22µF`，`CIN=10µF+100nF`，`CBS=100nF`。
- ESP32、LCD/CTP、GNSS 分支均有本地去耦；GNSS 另经磁珠生成 `3V3_GNSS`，要求样机纹波小于 50mVpp。

### 4.3 5V 音频电源

- 第二颗 `SY8113IADC`，VOUT=5V_AUDIO，持续 1A、音频瞬态 1.5A。
- `L=6.8µH`，`RH=162kΩ`，`RL=22.1kΩ`，标称约 5.00V；`CFF=220pF`，`COUT=2×22µF`。功放附近默认装 `100nF+10µF`；C36=`470µF/16V` 保留封装但强制 DNP，因为约 524µF 总负载在 1ms 软启动下理想充电约 2.62A，可能超过 2.38A eFuse 限流。
- 5V 输入时允许进入压差区，禁止增加升压级；整机峰值输入不超过 10W。

## 5. ESP32-S3 GPIO 合同

| 功能 | GPIO | 说明 |
|---|---:|---|
| LCD D0～D7 | 4～11 | 8 位 I80 数据，靠 MCU 端串 22Ω |
| LCD WR | 12 | I80 写时钟，串 22Ω |
| LCD RS/DC | 13 | 命令/数据选择 |
| LCD CS | 14 | 片选 |
| LCD RESET | 15 | 复位 |
| LCD BL PWM | 16 | 背光 PWM |
| CTP SDA/SCL | 17/18 | I²C，各 4.7kΩ 上拉至 3V3 |
| USB D-/D+ | 19/20 | 固定原生 USB，引脚侧串 22Ω |
| CTP INT | 21 | 中断输入 |
| CTP RESET | 38 | 独立硬复位 |
| GNSS RX/TX | 39/40 | ESP RX 接模块 TXD0；ESP TX 接模块 RXD0 |
| GNSS 1PPS | 41 | 秒脉冲输入 |
| I2S BCLK | 42 | HT517 DI0 |
| 扩展 GPIO | 1 | 经 R39=33Ω 接 J6.3；原 NC 标记移除 |
| 调试 UART TX/RX | 43/44 | 保留 TP8/TP9，并经 R40/R41=33Ω 分支到 J6.4/J6.5；GPIO43 启动日志风险必须保留 |
| LCD TE/FMARK | 3 | 仅经 0Ω DNP 预留，默认不连接以避免启动绑带风险 |
| I2S LRCK/SDOUT | 47/48 | HT517 DI1/DI2 |
| AMP_SHDN | 2 | 经反相开漏级控制 HT517 EN，默认关断 |
| BOOT | 0 | 后盖维修按键 |
| GPIO45/46 | NC | 启动绑带脚，不接外设 |

`EN/CHIP_PU` 使用 `10kΩ` 上拉和 `C15=1µF/0603` 到 GND，按 ESP32-S3-WROOM-1 原厂建议提供上电 RC 延时；保留后盖 `RESET/EN` 按键直接下拉复位。

### 5.1 内部扩展接口 J6

- `J6=2.0-5P WZ-D / C41361332`，PH2.0、1×5、卧式弯插通孔，开口朝板外；因库料明确标注“不耐温”，只允许回流后手工/选择焊。
- 针序固定为 `GND / 3V3_EXP / EXP_GPIO1 / EXP_UART_TX_GPIO43 / EXP_UART_RX_GPIO44`，不得在 PCB 阶段为了走线方便调换。
- `R38=0Ω/0805` 把 `3V3` 隔离为 `3V3_EXP`；`R39～R41=33Ω/0603` 分别形成三个接口侧信号网络。建议扩展板持续取电不超过 300mA，且禁止从 J6 向主板反向供电。
- J6 是壳内非热插拔扩展口，不增加独立 ESD 阵列；若后续改成外露接口，必须回到原理图阶段补齐 ESD/浪涌/短路/EMI/防水设计。
- PCB 优先放在左下板边空区，避开左下 M3 的 `R4mm` 全层禁布、J1 输入区、GNSS/RF 和底层 Pogo 阵列；接口朝外且不得侵入屏幕、外壳或线束弯折空间。

## 6. 模块合同

### 6.1 LCD/CTP

- LCD 40Pin：1～4 电阻触摸脚 NC；5 GND；6 IOVCC=3V3；7 VCC=3V3；8 FMARK 经 DNP 接 TE；9 CS；10 RS；11 WR；12 RD 固定高；13/14 串行数据脚 NC；15 RESET；16 GND；17～24 DB0～DB7；25～32 DB8～DB15 NC；33 LEDA；34～36 LEDK；37 GND；38/39/40 固定 IM0/IM1/IM2=`1/1/0`。
- CTP 6Pin：VCC、SDA、SCL、INT、RST、GND；SDA/SCL 各 4.7kΩ 上拉，四根信号经过可选低电容 ESD 阵列。
- RD 不接 MCU，使用 10kΩ 上拉至 3V3。
- J2 使用 `FPC-0.5HF-40PWBH20 / C3446044`，J3 使用 `FH12-6S-0.5SH(55) / C202118`；机械固定脚均标 NC，PCB 放置前用屏幕实物核对触点面与插入方向。

### 6.2 GNSS

- ATGM336H-F8N-76：1/10/12 GND，2 TXD0，3 RXD0，4 1PPS，6 VBAT，8 VCC，11 RF_IN，14 VCC_RF；5 ON/OFF 与 9 nRESET 保持默认状态并各留测试点；7/13/15～18 悬空。
- `3V3 → 330Ω → BAV199 单结 → VBAT/CHP5R5L105R-TW 1F超级电容`，保留充电路径测试点；C16 使用 16×8mm、11.5mm 脚距横卧封装。
- RF 拆为 `U6.11 → GNSS_RF_MODULE → R34(0Ω) → GNSS_RF_ANT → J4`；VCC_RF 经 47nH 馈入 `GNSS_RF_ANT`。C32/C33=1pF C0G、DNP。D4=`PESD5V0R1BSFYL` 强制 DNP：v2 明确排除连接 DC supply 的线路，v3 警告不得连接无限流 DC 源，而天线侧带 3.3V 偏置且适用边界未关闭；替代料必须明确支持 DC-biased RF 且结电容不高于 0.2pF。

### 6.3 音频

- HT517SPER ESOP8：DI0/DI1/DI2 分别经 R35/R37/R36=33Ω 接 BCLK/LRCK/SDOUT，C39～C41=10pF C0G、DNP；GAIN/CH 先用 100kΩ 到 VDD 的低增益档；EP 接大面积 GND。
- EN 由 2N7002 开漏反相级控制，使 MCU 复位/上电默认关断，避免爆音。
- OUTP/OUTN 经 FB1/FB2=0805 0Ω 默认装配到 PH2.0-2P；磁珠替代需 ≥2A、低 DCR 且两路对称验证。C37/C38=1nF C0G、DNP，驱动 4Ω/5W 防水膜喇叭，实际目标约 3W。
- TP1～TP22 使用约 2.00mm 无孔单圆盘 SMD 测试点；PCB 中全部置底层、2.54mm 针床节距、沉金且排除钢网，不装实体器件。

## 7. 风险、验证与回退

- 高：屏幕卖家可能随机替换驱动/触摸/接口；首批来料必须核对配置和 FPC。
- 高：背光缺少裸屏额定参数；R1 仅为样机可调电路，不得直接宣称量产冻结。
- 高：SGM2536FR、SY8113I 与 HT517 的散热和电源瞬态必须实测。
- 高：D4 的带直流偏置 RF 适用边界未关闭，当前强制 DNP；替代 RF ESD 需重新做插损、定位与天线状态验证。
- 高：C36 470µF 可能在启动时触发 eFuse 限流，当前强制 DNP；试装需重开 5V/12V 启动和反复通断验证。
- 中：12V 通过 USB-C 为非标准供电，必须使用专用标识线束且不得连接手机。
- 中：外置有源 GNSS 天线与 U.FL/SMA 尾线需要做频段、增益、驻波和 ESD 验证。
- 回退：`PW1558` 在 R1 中已作废；未来若取得完整规格书并独立验证门限、短路安全工作区与故障时序，只可作为重画输入保护页的样品候选；其他页面只依赖 `VIN_PROT`，无需重画。

## 8. 验收

- EasyEDA 中存在上述 6 个原理图分页；电源配置、去耦、串阻、DNP/匹配位和 NC 必须由真实器件/引脚属性表达，不能以网络名或说明文字替代。
- 自动引脚级网络审查确认每个电源域有来源、每颗两端器件不出现意外同网、USB/TE/RF/音频拆网符合连接表，且位号/Unique ID/封装无重复或缺失。
- 保存重开后的严格 ERC 为 `fatal=0、error=0、warn=6、info=151`。6 条 warning 已逐条导出：3 条为相同网络名重复标记，3 条为器件属性/供应商编号标准化建议；不得宣称“无警告”。
- 结构化连接表与 EasyEDA 原理图一致，电源轨、电流预算和测试点可追溯。
- U2、J2/J3、C16 与 TP1～TP22 均绑定可解析封装；只有完成上述电气门后才允许同步 PCB。

## 9. PCB 阶段续作设计

### 9.1 当前状态与同步边界

- EasyEDA `PCB1`（UUID `3cbb78f5012cc03c`）已完成官方交互导入并保存重开：回读为 127 个器件、394 个焊盘、84 个网络。原理图与 PCB 均为 127 个器件；缺失/多余器件、重复位号/Unique ID、位号错配和封装错配均为 0。
- `pcb_Document.importChanges("3487150dc4fb72de")` 返回 `true` 只代表拉起交互流程；本次是在界面执行“应用修改”并完成非零回读后才判定同步成功。同步后快照为 `post-pcb-import-20260718-235800.eprj2`，`PRAGMA integrity_check=ok`，SHA-256 `15a359c28dec21436136e61cefd65a0048c28e81e3d6621a9785bb93e30ab012`。
- 2026-07-19 已按用户“4 寸屏下方长方形主板”要求完成第一轮初始布局：创建 `108×62mm` 临时长方形板框，127 个器件全部进入板内，TP1～TP22 全部翻到底层并按 2.54mm 针床节距排列；输入/eFuse、双 Buck、LCD/CTP、GNSS/RF、音频、ESP32/USB 已分域，ESP32 模组旋转后天线端贴右侧板边。板上顶层丝印明确写有 `PROVISIONAL 108x62mm - DO NOT FAB` 与 `ANTENNA / METAL-FREE`。
- 用户随后明确 Type-C 应位于 ESP32 对侧边的中间。当前临时包络中，J1 已置于左侧板边几何中点 `x=170mil、y=-1220.4724mil、rotation=90°`，开口朝左，与右侧 U5 对置；U1、R1/R2 和 C19 随接口收拢，保持 ESD、CC 下拉和 3.3V 参考去耦靠近接口。该相对位置要求冻结，绝对坐标仍随最终板框缩放或镜像复核。
- 保存、关闭、重开后的 API 回读为 127 个器件、394 个焊盘、4 条板框线、2 条警示文字；器件包围盒全部位于板框内，普通顶层器件包围盒碰撞为 0。Pogo 封装外框在严格 2.54mm 节距下相交约 0.33mm，但其实际约 2.00mm 底层裸盘仍保留约 0.54mm 间隙，按针床合同保留。走线、过孔、区域和覆铜仍为 0；DNP、NC 和 BOM 排除不等同于 PCB 排除，因此仍无投板资格。
- 修改前快照为 `pre-rect-layout-20260719-010433.eprj2`，SHA-256 `15a359c28dec21436136e61cefd65a0048c28e81e3d6621a9785bb93e30ab012`；修改后快照为 `post-provisional-rect-layout-20260719-011754.eprj2`，`PRAGMA integrity_check=ok`，SHA-256 `0daf4c3133118b845ac0e45beb1c73865eda4e0e8035412eca9acd00e3962248`。
- Type-C 调整前快照为 `pre-typec-center-20260719-100145.eprj2`，SHA-256 `0daf4c3133118b845ac0e45beb1c73865eda4e0e8035412eca9acd00e3962248`；保存重开后的快照为 `post-typec-opposite-center-20260719-100747.eprj2`，`PRAGMA integrity_check=ok`，SHA-256 `a4cef7c1f1c0417e8da0cc2839a5dac2d28663b85f95c2925d48b8a9e905e8dc`。
- 2026-07-19 的后续机械冻结已取代 `108×62mm` 临时包络：当前板框为 `112×68mm`，屏幕投影为 `x=8.715～103.285mm、y=3.56～64.44mm`，新增 4 个 `Ø3.2mm` 非金属化 M3 孔并使 PCB 焊盘总数从 394 增至 398。保存、关闭、重开后的 EasyEDA 回读仍为 127 个器件、398 个焊盘；板框、屏幕投影、两个机械槽、12 个机械禁布区域和 4 个安装孔均持久化。

### 9.2 PCB 同步成功后可执行的初始布局

- 建立 4 层板约束：L1 器件/高速信号、L2 连续 GND、L3 电源与低速信号、L4 信号/GND；在正式阻抗单确认前不虚构介质厚度或线宽结论。
- 按输入保护、双 Buck、ESP32/USB、LCD/CTP、GNSS/RF、音频、底层产测七个功能域聚类；先完成相对位置、朝向和关键回路收敛，再进行普通信号扇出。
- USB D+/D- 建立差分对与 90Ω 目标约束；U1 靠 J1、22Ω 串阻靠 U5，禁止长测试支路。GNSS RF 走线只保留连续地参考、最短路径和 π/ESD 调试位，最终 50Ω 线宽等待嘉立创实际层叠计算。
- U3/U4 的输入电容、芯片、LX、电感、输出电容形成最小热回路，FB 从输出电容正端 Kelvin 回采并远离 SW；U7、喇叭输出和 5V_AUDIO 储能远离 GNSS 与 ESP32 天线区。
- TP1～TP22 全部放在底层、按 2.54mm 针床节距排布并排除钢网；TP6/TP7 只允许短、对称、无分叉残桩的 USB 测试接入方案。
- 当前 `112×68mm` 布局把 GNSS/U.FL 放在左上，J1 固定在左侧边中点 `(4.318,34.0)mm、90°`，输入/eFuse、双 Buck、音频和 ESP32/USB 仍按功能域分区；U5 在右侧且天线端伸出屏幕投影。J2 位于 `(37.971419,33.01)mm、-90°`，J3 位于 `(15.214127,53.56)mm、+90°`，两者入口均朝左并正对各自竖向穿线槽。
- J6 增量导入后固定在左下板边内侧 `(16.51,58.42)mm、0°`，Pad1～Pad5 从左到右为 `GND/3V3_EXP/EXP_GPIO1/EXP_UART_TX_GPIO43/EXP_UART_RX_GPIO44`；R38～R41 位于其上方 `y=47.752mm`。导入默认把新器件停放在板框外，已在用户指出后立即移回板内；最终严格 DRC 的 Clearance/Keepout 为 0，不能把导入暂存位置当作布局结论。

### 9.3 机械冻结与仍需实物关闭项

- `LCD-40001-module-manual.pdf` 第 1～2 页给出的 `108.1 × 61.74 mm` 和孔位属于供应商 40001 转接模块/结构图；本设计明确不使用该 34Pin 转接板，不能据此推导裸屏 40Pin、CTP 6Pin FPC 的精确坐标。
- 当前 R1 机械合同由用户冻结为：板框 `112×68mm`；屏幕投影 `94.57×60.88mm` 且居中；4×M3 NPTH 位于 `(4,4)`、`(108,4)`、`(4,64)`、`(108,64)mm`；孔周 `R4mm` 全层禁布。该合同已进入 EasyEDA，但仍不得把 40001 转接模块图纸当作其证据。
- 2026-07-20 用户要求增加穿线装配余量后，40Pin 槽更新为竖向 `3×23mm、R1.5`、中心 `(32,33.01)mm`；6Pin 槽更新为竖向 `3×6mm、R1.5`、中心 `(8.715,53.56)mm`。槽外 `0.5mm` 区域禁止走线、填充、覆铜和内电层；左、下侧和 6Pin 上侧 `1mm` 禁止器件，40Pin 上侧按 C16 外框保留 `0.9mm`，连接器所在右侧按 `J2≈1.0mm、J3≈0.3mm` 的入口间隙作为明确例外。
- 40Pin 总长 `42.1mm`。根部到槽左边 `21.785mm`，穿槽/翻面保守段 `4.6mm`，槽到 J2 入口 `1.0mm`，插入段 `4.5mm`，不含根部回折共 `31.885mm`。按 `R1.5mm` 回折且两侧各 `1mm` 释放段，回折展开 `6.712mm`，设计路径 `38.597mm`，余量 `3.503mm`。`R1.5mm` 是工程假设，屏幕实物或供应商若要求更大半径，必须重开机械布局。
- 6Pin 总长 `11.05mm`。名义路径为半槽 `1.5mm` + 板厚 `1.6mm` + 入口间隙 `0.3mm` + 插入 `4.5mm` = `7.9mm`，余量 `3.15mm`；按全槽宽保守计算为 `9.4mm`，余量仅 `1.65mm`。因此 6Pin 必须以实物完全插入、锁扣闭合且无持续拉力为放行条件。
- `preview/pcb-final-112x68-fpc-mechanical-1to1.{svg,png,pdf}` 是当前 1:1 机械合同图；打印必须使用 100% 实际尺寸，并先用 50mm 校准线确认打印机未缩放。
- 为避免 `FH12-6S` 在整板 1:1 图中因实物尺寸过小而被误认为漏放，整板图使用洋红虚线框标出 J3，并另提供 `preview/pcb-final-112x68-j3-6pin-detail-4x.{svg,png,pdf}`。4× 图只用于辨认接口、锁扣、6 个信号触点和穿线方向，尺寸验收仍以 1:1 图与实物为准。

### 9.4 PCB 阶段验收证据

- PCB API 回读的器件/图元/层/网络统计与原理图同步清单一致。
- 原理图 ERC 与 PCB DRC 分开记录；PCB 为空、无板框或未布线时的“通过”不计入验收。
- 正式投板前飞线为 0，所有 DRC 例外逐条记录，关键回路和天线/RF/USB/大电流路径完成人工复核。
- 板框、J2/J3、4×M3、两个竖向槽和结构禁布区已按 `112×68mm` 机械合同进入 EasyEDA并保存重开；投板前仍必须用屏幕实物或供应商结构图关闭 FPC 最小弯曲半径、触点面、锁扣空间、金属背板绝缘与天线遮挡。
- J6 更新后的保存重开回读为 132 个器件、411 个焊盘、89 个网络；电气走线和过孔仍为 0。当前严格 PCB DRC 只返回 `Connection Error (374)`，Clearance/Keepout 为 0；这是一条未布线状态证据，不是 DRC 通过。正式投板前必须完成布线/覆铜、飞线清零并重新运行严格 DRC。

### 9.5 自动布线路径诊断

- 调用 EasyEDA 原生 `pcb_Document.autoRouting()` 返回 `success=false、totalNetsCount=84、successNetsCount=0、duration=0`；调用前后走线/过孔数量均为 0。PCB 已启用 Top、Bottom、L2_GND、L3_PWR_SIGNAL 四个信号层，板框也闭合，因此结果记录为原生自动布线后端未启动或不可用，不归因于 J2/J3 或板框缺失。
- J6 更新后再次请求只布 7 条低速扩展网络并限制 Top/Bottom；桥接运行时不暴露文档中的枚举对象，因此按 API 文档序列化值重试 `RoutingNets` 与示例中的 `nets` 两种写法。后端均忽略筛选，把全部 89 个网络列为失败并返回 `success=false、successNetsCount=0、duration=0`；调用前后铜线/过孔仍为 `0/0`。这进一步确认原生后端未真正启动，不得继续用参数微调掩盖工具不可用。
- J6 自动布线前快照为 `/home/wintsa/文档/LCEDA-Pro/projects/esp32BmsGps-Hardware-R1.pre-native-autoroute-j6-20260719-162255.eprj2`，最终保存重开快照为 `/home/wintsa/文档/LCEDA-Pro/projects/esp32BmsGps-Hardware-R1.post-expansion-j6-20260719-162657.eprj2`；两者 SHA-256 均为 `009d746ed937bc157982c8207f342ccad0836a459489d9dd5b46465bfe6b4a4b`，SQLite `integrity_check=ok`，相同哈希证明失败的自动布线没有修改工程。
- 原生自动布线前快照为 `/home/wintsa/文档/LCEDA-Pro/projects/esp32BmsGps-Hardware-R1.pre-native-autoroute-20260719-142305.eprj2`，SHA-256 `5622b9b3b08d7729af41429aee93f5165884402ae9ab64f821112110aafc7547`，SQLite `integrity_check=ok`。
- EasyEDA 的 PADS 导出在 `pcb-rnd 3.0.6` 中出现错误比例，转换画布约 `132.66×20.08mm`，已排除。Altium ASCII 导出经临时构建的 `pcb-rnd 3.1.6` 恢复 127 个器件、84 个网络和 J2/J3；转换器把已禁用的 `Inner3` 错恢复成第 5 铜层，并把外板框归入 keepout，因此先在临时副本中删除该层、恢复 Top/L2_GND/L3_PWR_SIGNAL/Bottom 四层，并把外板框与两个槽分离。
- 修正后的 FreeRouting 输入 DSN 为 `/tmp/pcb-altium-20260719-143823/PCB1-4layer-ready.dsn`，SHA-256 `f16506dfbed80670564fe33d8ec4a6ac675ef99afa24bd152f1d5547d8da7fbe`；自检结果为 4 个信号层、127 个器件、84 个网络、1 个 `112×68mm` 边界和 14 个走线/过孔禁布对象。J2/J3、两个 FPC 槽、4×M3 和 ESP32 天线区均在输入中。
- FreeRouting `1.9.0` 单线程运行到第 33 轮后安全停止，总耗时 `10m40.71s`。SES 为 `/tmp/pcb-altium-20260719-143823/PCB1-4layer-routed.ses`，SHA-256 `58ca0bf2d39aad4bd9d0a24e9c8843456e9d212a4012dc16405e5b191858ccd6`；包含 82 个有路线记录的网络、510 段线和 161 个过孔，GUI 仍显示 `to route: 1`。SES 路由段中未出现 `AMP_GAIN_CH` 与 `BST_5V_AUDIO`，回导前必须用连通性检查区分零长度/焊盘直接接触与真实未连接。
- 该首轮路线把 401/62/39/8 段线路分别放在 Top/L2_GND/L3_PWR_SIGNAL/Bottom。`L2_GND` 上出现 62 段普通信号，直接违反“连续完整 GND 平面”的冻结约束；因此这版 SES 只证明外部工具链可解析和产出路线，不得作为最终 PCB 回导或 DRC 输入。诊断截图保存在 `preview/pcb-freerouting-provisional-pass33.png`，SHA-256 `e01430770eb2e9ea8e65bd64ef131318c161dcc49d3f6ce142b7b97c1a83ca64`。

### 9.6 布线收敛顺序

1. 在外部路由输入中把 `L2_GND` 限制为连续 GND 平面，不允许普通信号路线；L3 只承载电源与低速信号，Top/Bottom 承担主要普通信号。
2. 在 EasyEDA 中手工优先完成并锁定 USB D+/D−、GNSS RF、双 Buck 热回路、eFuse/大电流输入、I80 时钟与音频 BTL 回路，再对普通低速网络使用外部自动布线辅助。
3. 自动结果回导前必须确认器件坐标、J2/J3 方向、两个槽、4×M3 和天线禁布区没有变化；回导后保存、关闭、重开并重新回读。
4. 只有飞线为 0、严格 DRC 无未解释错误、L2 连续性通过人工复核，并完成屏幕实物装配门后，才能移除 `PROVISIONAL / DO NOT FAB`。

### 9.7 J6 白名单布线与 USB 规则回读

- EasyEDA 原生自动布线在修复板框后仍返回 `success=false、totalNetsCount=89、successNetsCount=0、duration=0`，并将全部 89 网络列为失败；这与板框闭合问题无关，仍定位为原生计算端未实际执行白名单。
- 原四条 Layer 11 `LINE` 即使端点完全闭合且 `zoomToBoardOutline()=true`，自动布线/制造导出层仍不把它们识别为单一板区，`getDsnFile()` 返回 `undefined`。最终将四条 Line 替换为一个 Layer 11 闭合 `POLY`，路径为 `(0,0) -> (4409.4488,0) -> (4409.4488,-2677.1654) -> (0,-2677.1654) -> (0,0)`。
- FreeRouting SES 只作路径建议：EasyEDA SES 导入会量化线宽、丢失/误解底层路径，且 TP8/TP9 是底层 SMD 测试点。最终通过原生图元 API 显式指定 Top/Bottom，并采用“先过孔、后线段”的创建顺序保证跨层连通。
- 保留的铜线只属于 `3V3_EXP / EXP_GPIO1 / EXP_UART_TX_GPIO43 / EXP_UART_RX_GPIO44 / GPIO1_EXP_RAW / DBG_UART_TX_GPIO43 / DBG_UART_RX_GPIO44` 七个白名单网络。`3V3_EXP` 使用 20mil，GPIO/UART 使用 8mil；L2/L3 仍为 0 条普通信号线。
- 保留差分对 `USB_CONN_DP_DN` 和 `USB_MCU_DP_DN`，新增等长组 `USB_CONN_LENGTH_MATCH=[USB_DP,USB_DN]` 与 `USB_MCU_LENGTH_MATCH=[USB_DP_MCU,USB_DN_MCU]`；不得跨 22Ω 串阻将四个网络混成一组。本次未布 USB 铜线，90Ω 差分阻抗线宽/线距仍须等嘉立创实际层叠后计算。
- 保存关闭重开后为 132 个器件、411 个焊盘、89 个网络、Top/Bottom `24/14` 条铜线、6 个过孔。严格 DRC 仅有整板尚未布线的 `Connection Error (358)`；七个目标网络的 Connection Error 为 0，Clearance/Keepout 为 0。这只完成 J6 局部布线，不改变整板 `DO NOT FAB`。
- 保存后快照为 `/home/wintsa/文档/LCEDA-Pro/projects/esp32BmsGps-Hardware-R1.post-j6-routing-20260719-200622.eprj2`，SHA-256 `72e448577573d87c51aae087777dd7a26ff369637ae651e431fab1751ffe4d21`，SQLite `integrity_check=ok`。

### 9.8 板框未被自动布线识别的复盘

- **根因分类**：B（跨层契约）+ E（隐式假设）。画布/缩放层接受多个 Line，自动布线与制造导出层需要单个合并后的闭合轮廓对象。
- **前两次修复为何失败**：第一次只恢复了 4 条 Layer 11 Line；第二次只调整了线段方向并验证 `zoomToBoardOutline()`。两者都未读取自动布线真正使用的 DSN 边界。
- **P0 预防机制**：自动布线前必须同时满足 `Layer11 POLY count=1`、`LINE count=0`、`zoomToBoardOutline()=true`、`getDsnFile()!=undefined`且 DSN 包含 `boundary(path ...)`。
- **系统扩展**：DXF/外部导入的板框、槽和禁布区都可能有“看起来正确，但对象类型错误”问题；验收必须检查下游导出产物，不能只检查画布。
- **最终证据**：保存关闭重开后，DSN `88130` 字节并包含 `boundary(path signal ...)`，标准自动布线 JSON `209915` 字节，JRouter JSON `210192` 字节；严格 DRC 仍仅 `Connection Error (358)`。
- **回滚/完成快照**：`pre-polyline-outline-20260719.eprj2` SHA-256 `72e448577573d87c51aae087777dd7a26ff369637ae651e431fab1751ffe4d21`；`post-polyline-outline-20260719.eprj2` SHA-256 `1fad6e78b9b2cd365682aad415e6510de7d1904156fd0b2f222853e1164a2e8b`；两者 SQLite `integrity_check=ok`。
