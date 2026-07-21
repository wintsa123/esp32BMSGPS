# 高性能触屏车机 R1 原理图与 PCB 实施计划

## 1. 资料与规划

- [x] 将快速模式、4.0 英寸屏幕结论与 SGM2536 安全回退记录到 PRD/设计文档。
- [x] 下载并校验 SGM2536、SY8113I、HT517、ATGM336H-F8N-76、ILI9488 与 ESP32-S3 相关资料；只把真实 PDF 放入 `docs/hardware/datasheets/`。
- [x] 修订 `docs/hardware/01-requirements.md`、`03-components.md`、`04-constraints.md`、`05-validation.md`、`06-decisions.md` 与 `07-schematics.md`；完成态按保存重开后的 ERC 与 127 器件回读证据重写。

## 2. EasyEDA 实施

- [x] 检查 Bridge、活动窗口、当前工程和当前文档类型。
- [x] 创建并打开 `esp32BmsGps-Hardware-R1` 工程，建立 6 个原理图分页。
- [x] 从库中搜索并核对关键器件的符号、封装和订货型号；U2 已替换为 `SGM2536FRXTSP10G/TR / C48384692` 专用实例，pin3/pin4 精确为 `SPGD/nFAULT`，并绑定 2×2mm 10Pin 封装。
- [x] J2/J3 已替换为 `FPC-0.5HF-40PWBH20 / C3446044` 与 `FH12-6S-0.5SH(55) / C202118`，信号网络保持不变，机械脚标 NC。
- [x] C16 已替换为 `CHP5R5L105R-TW / C2925920` 横卧 H 型实例；TP1～TP22 已替换为单引脚约 2.00mm 无孔 SMD 裸盘。
- [x] 按 `design.md` 完成器件放置、跨页网络、参数、位号、真实 DNP/NC 与测试点标注；网络名或文字说明未代替器件与 `NoConnected=true`。
- [x] P1 补齐 R3～R7、C1～C4、C17～C19，形成真实 eFuse 门限、限流、软启动、计时、IN/OUT 去耦与 U1 钳位参考去耦。
- [x] P2 修复 C9/C13 同网错误，增加两路 Buck 输入 100nF，并用真实磁珠把 `3V3` 接到 `3V3_GNSS`。
- [x] P3 增加 USB 22Ω 串阻与 DNP 调试电容、ESP32 22µF+100nF 本地储能，并拆分 USB MCU 侧网络。
- [x] P4 增加 LCD/CTP 本地去耦、LCD RD 上拉、真实 TE 0Ω DNP 和 D3=`USBLC6-4SC6-ES` 四路 CTP ESD 阵列。
- [x] P5 增加 GNSS 去耦、R34/C32/C33 π 匹配和 D4 RF ESD 实体位；D4 因带直流偏置线路的适用边界未关闭而强制 DNP，47nH 馈电注入天线侧。
- [x] P6 增加 HT517 100nF/10µF，C36 470µF 保留实体位但强制 DNP；FB1/FB2=0Ω 默认装配，C37/C38 与 C39～C41 DNP，R35～R37=33Ω 默认装配。
- [x] 增加 TP17～TP22，覆盖 eFuse 故障、GNSS 控制和 VBAT 充电/保持网络；所有有意未使用引脚均设置真实 NC。
- [x] 保存工程并记录工程、原理图和分页 UUID。

## 3. 检查与修复

- [x] 删除并重建 P5 的 3 个、P6 的 5 个空名网络端口；保存、关闭、重新打开后名称仍持久化。
- [x] 修复后重新运行 EasyEDA 严格 ERC：`fatal=0、error=0、warn=6、info=151`；6 条 warning 已逐条导出为 3 条相同网名重复标记和 3 条库属性标准化建议。
- [x] 修复后重新人工复核 USB-C 正反插、eFuse 配置、降压反馈/输入输出电容、ESP32 启动绑带、LCD 模式/TE、GNSS 电源与 RF、HT517 BTL/储能和超级电容极性。
- [x] 对照修订后的 `07-schematics.md` 执行全工程引脚级网络审查；P1～P6 为 `21/21/12/22/16/35` 个真实器件、合计 127，未发现重复 ID、缺封装、悬空/NC/同网/端口冲突。
- [x] 使用 GitNexus `detect_changes()` 检查仓库变更范围；本任务不得触碰固件运行逻辑。

GitNexus 对整个未提交工作区返回 `critical`（107 个代码符号、82 条流程），均来自其他已存在的固件/UI 改动；本轮只修改本任务文档、`docs/hardware/` 与仓库外 EasyEDA 工程，未修改任何固件符号。

## 4. 风险与回滚点

- 输入保护页是独立回滚点：R1 冻结 SGM2536FR，PW1558 已作废且不做兼容焊盘；若未来经完整资料与样品验证重新启用，只修改该页和 BOM。
- J2/J3 精确封装已经冻结；PCB 放置前仍须用屏幕实物核对触点面、插入方向、锁扣空间和机械坐标，出现不兼容时回滚连接器实例而不改显示网络合同。
- Pogo 快速模式采用原生可解析的约 2.00mm 无孔圆盘，而非原计划 1.27mm 裸盘；PCB 阶段必须全部置底层、保持 2.54mm 针床节距、排除钢网并复核 USB D+/D− 支路。
- 用户已有未提交改动必须保留；只修改本任务规划文件、`docs/hardware/` 和 EasyEDA 工程。

## 5. 完成判据

- [x] 6 个原理图分页均已创建并保存。
- [x] 原理图 ERC 结果及 6 条 warning 例外已按修复后工程重新记录；PCB DRC 仍待同步、布局和布线后执行。
- [x] `07-schematics.md` 可独立交给硬件工程师复核，且与 EasyEDA 工程回读一致。
- [x] 高风险假设均有 EVT 验证动作，所有 DNP/待定料号都有真实器件位置和封装；D4、C36 明确强制 DNP。
- [x] J6 更新后重新生成绿色主题 A4 PDF 报告；最终为 36 页、637078 字节，SHA-256 `6ad523c5ad7ad996c79b4011422c043f8f91916822c484b95a650f9c8dc05e8d`，HTML SHA-256 `a2396499d90a4354558cc38c521e176887ebae8b400378fc3f1470d0cf6eed2d`。36 页已全部渲染为 PNG 并按 6 组联系表目视复核；仅封面属于低文本页，无空白页、裁切、重叠、乱码或页脚贴线。
- [x] 原理图电气完整性门已通过，PCB 导入也已回读非零；只放行后续初始布局，不放行投板。

## 6. PCB 初始布局续作

- [x] 通过 EasyEDA API 核对 `PCB1` 导入前基线：PCB 文档存在，但器件和其它图元均为 0。
- [x] 在同步/修复前后生成一致的 EasyEDA 工程快照；新增 `pre-p5-emptyport-repair-20260718-232500.eprj2`（SHA-256 `a408e7a2678eac1e840963e06b11eb43bd4e3b07cbdc50fd86f4ddbab5ba6ade`）和 `post-erc-pass-20260718-233800.eprj2`（SHA-256 `9c0c6104e162cb3afa9af3199b7d0adc88af70743feb33c0dbf5c1ab93ebe7ee`），两者 `PRAGMA integrity_check=ok`；早期快照继续保留。
- [x] 从原理图 UUID `3487150dc4fb72de` 向 PCB UUID `3cbb78f5012cc03c` 导入变更并在嘉立创EDA界面应用修改；保存重开后为 127 个器件、394 个焊盘、84 个网络，原理图/PCB 127:127，缺失/多余器件、重复位号/Unique ID、位号错配和封装错配均为 0。
- [x] 生成同步后快照 `post-pcb-import-20260718-235800.eprj2`，`PRAGMA integrity_check=ok`，SHA-256 `15a359c28dec21436136e61cefd65a0048c28e81e3d6621a9785bb93e30ab012`。
- [ ] 配置 4 层、网络类、USB 差分对和基础间距/过孔规则；实际阻抗线宽等待嘉立创层叠计算，不凭默认值宣称 90Ω/50Ω 已实现。
- [x] 完成功能域初始摆放：采用 `108×62mm` 临时长方形板框，127 个器件全部进入板内；输入/eFuse、双 Buck、ESP32/USB、LCD/CTP、GNSS/RF、音频完成分域，TP1～TP22 全部置底层 2.54mm 阵列。顶层丝印明确写入 `PROVISIONAL 108x62mm - DO NOT FAB` 和天线金属净空警示，未冻结项继续保留。
- [ ] 复查原理图电气问题并将必要修正重新同步到 PCB，保持 EasyEDA、`07-schematics.md`、`04-constraints.md` 三者一致。
- [x] 对初始布局运行并记录 PCB 几何回读与严格 DRC：器件越界为 0，普通顶层器件包围盒碰撞为 0；Pogo 外框相交是 2.54mm 针床节距下的已知封装外框例外，实际约 2.00mm 裸盘不相交。严格 DRC 仅返回 `Connection Error (360)`，与走线为 0 一致，明确不记为通过。
- [x] 保存、关闭、重开并回读持久化结果；生成 `post-provisional-rect-layout-20260719-011754.eprj2`，`PRAGMA integrity_check=ok`，SHA-256 `0daf4c3133118b845ac0e45beb1c73865eda4e0e8035412eca9acd00e3962248`，并将画布证据保存为 `preview/pcb-provisional-108x62.png`。
- [x] 按用户“Type-C 位于 ESP32 对侧边中间”的澄清，将 J1 置于临时板框左侧边中点 `x=170mil、y=-1220.4724mil、90°`，并将 U1、R1/R2、C19 收拢到接口旁；保存重开后 127 个器件不变，严格 DRC 仍仅为未布线的 `Connection Error (360)`。调整前/后快照分别为 `pre-typec-center-20260719-100145.eprj2` 和 `post-typec-opposite-center-20260719-100747.eprj2`，后者 `PRAGMA integrity_check=ok`、SHA-256 `a4cef7c1f1c0417e8da0cc2839a5dac2d28663b85f95c2925d48b8a9e905e8dc`；预览已覆盖更新，SHA-256 `15d0cc635e86cb9302c633efea782fac55f64542597b298039ed82e5281320d4`。
- [x] 以用户冻结的 R1 机械合同把板框更新为 `112×68mm`，加入居中的 `94.57×60.88mm` 屏幕投影、4×`Ø3.2mm` M3 NPTH、孔周 `R4mm` 全层禁布、两个非金属化圆角穿线槽和槽外机械禁布区；J1 保持左侧中点，U5 保持右侧且天线端伸出屏幕投影。
- [x] 修正槽方向：40Pin 槽为竖向 `3×22mm`、中心 `(32,33.01)mm`；6Pin 槽为竖向 `3×5mm`、中心 `(8.715,53.56)mm`。J2/J3 分别为 `(37.971419,33.01)mm、-90°` 与 `(15.214127,53.56)mm、+90°`，两者入口均朝左。
- [x] 2026-07-20 按用户新增余量要求把 40Pin/6Pin 槽分别加长为 `3×23mm / 3×6mm`，中心、3mm 宽度和 R1.5 圆端不变；同步重建 `4×24mm / 4×7mm` 全层无铜包络及三侧器件禁布。40Pin 上侧因 C16 外框采用 `0.9mm`，其余受约束侧保持 `1mm`。保存重开后 132 个器件、411 个焊盘、89 个网络、12 个禁布区均保持，新增 Clearance/Keepout/Short 为 0。
- [x] 完成 FPC 全路径计算：40Pin 不含根部回折需 `31.885mm`；按 `R1.5mm + 两侧各1mm` 释放段后需 `38.597mm`，42.1mm 总长余量 `3.503mm`。6Pin 名义需 `7.9mm`、保守需 `9.4mm`，11.05mm 总长最坏余量 `1.65mm`。
- [x] 保存、关闭、重开并回读最终机械数据：127 个器件、398 个焊盘、2 个机械槽、12 个机械禁布区域和 4 个 M3 NPTH 均持久化。严格 DRC 仅有未布线的 `Connection Error (360)`，新增 Clearance/Keepout 错误为 0。快照 `post-fpc-orientation-path-verified-20260719-133816.eprj2` 的 `PRAGMA integrity_check=ok`，SHA-256 `5622b9b3b08d7729af41429aee93f5165884402ae9ab64f821112110aafc7547`。
- [x] 生成 `preview/pcb-final-112x68-fpc-mechanical-1to1.{svg,png,pdf}`；PDF 页面为 `190×120mm`，含 50mm 打印校准线，并经 300dpi 全页回渲确认无裁切或文字重叠。
- [x] 针对整板图中 J3 过小的问题，增加洋红虚线高亮与 `J3 / 6Pin CTP 接口` 标签，并生成 `preview/pcb-final-112x68-j3-6pin-detail-4x.{svg,png,pdf}`；4× 图明确显示 6 个信号触点、2 个固定脚、左向插入口、0.3mm 槽口间隙和 1.65mm 保守余量，300dpi 回渲检查无裁切或重叠。
- [ ] 用屏幕实物或供应商裸屏结构图验证 FPC 最小静态弯曲半径、触点面、J2/J3 完全插入与锁扣闭合、金属背板绝缘和 Type-C/M3 干涉，再完成布线、覆铜、3D/DFM 与正式投板审查。

## 7. 2026-07-19 独立硬件复核

- `PASS（带条件）`：方案边界和原理图电气门。127 个器件与 340 条导线完成引脚级审计，严格 ERC 为 `fatal=0/error=0/warn=6/info=151`；D4、C36 强制 DNP，6 条 warning 均已归因并保留例外记录。
- `CONCERN（已记录）`：ATGM336H-F8N-76 的本地手册内部把 `10.1×9.7mm` 概述/焊盘范围与 `16.0×12.2×2.4mm` 技术表混写；原厂产品页、LCSC `C46955980` 和 EasyEDA 封装共同支持 `10.1×9.7mm` XY。现有封装 XY 保留，Z 向按 2.4mm 最坏值预留，首批实物测高后关闭。
- `CONCERN（开放）`：板框、槽和连接器机械方向已经按 `112×68mm` 合同进入 EasyEDA；仍缺屏幕实物对 `R1.5mm` 回折假设、6Pin 仅 `1.65mm` 最坏余量、触点面、锁扣、金属背板绝缘、U.FL/SMA 尾线和背光电流的关闭证据。
- `FAIL（投板门）`：PCB 已有最终尺寸板框、安装孔、槽和机械禁布区，127 个器件全部在板内，22 个 Pogo 已置底层；但电气走线、过孔和覆铜仍为 0，严格 DRC 为 `Connection Error (360)`。状态必须保持 `PROVISIONAL / DO NOT FAB`。
- `CONCERN（验证门）`：EVT/DVT/PVT 尚未执行，尤其是 eFuse/TVS 28V 边界、非标准 12V USB-C、D4 替代 RF ESD、温升、湿屏、振动和批次一致性。
- 结论：原理图与 PCB 同步阶段成果可作为后续布局输入，但硬件方案 Gate 5/正式投板门不通过；任务继续保持 `in_progress`，不归档。

## 8. 2026-07-19 自动布线诊断进度

- [x] 创建并校验原生自动布线前 EasyEDA 工程快照；API 返回 84 个网络全部未启动，调用前后 PCB 图元不变。
- [x] 排除比例错误的 PADS 转换路径；使用 Altium ASCII + `pcb-rnd 3.1.6` 恢复 127 个器件、84 个网络及 J2/J3。
- [x] 在临时副本中删除错误恢复的 `Inner3`，补充 `0.6/0.3mm` 过孔原型与 `0.20/0.20mm` 临时普通信号规则，并建立 Top/L2_GND/L3_PWR_SIGNAL/Bottom 四层 DSN。
- [x] 修正 DSN 主边界为唯一 `112×68mm` 外框，并加入两个 FPC 槽、4×M3 和 ESP32 天线区的走线/过孔禁布；输入自检为 4 层、127 器件、84 网络。
- [x] 完成 FreeRouting 首轮诊断并安全写出 SES：第 33 轮、82 个有路线记录的网络、510 段线、161 个过孔，GUI 仍有 1 条待连接；诊断截图为 `preview/pcb-freerouting-provisional-pass33.png`。
- [ ] 重建路由规则，使 `L2_GND` 不接受普通信号；手工优先完成 USB、GNSS RF、双 Buck、电源大电流、I80 时钟和音频关键网络后，再路由普通信号。
- [ ] 定位 GUI 剩余 1 条连接；SES 未列出的 `AMP_GAIN_CH`/`BST_5V_AUDIO` 须分别验证焊盘是否直接接触或真实未连接。
- [ ] 在满足关键网络和层分工后才允许回导 SES；回导后保存、关闭、重开并核对 127 器件、84 网络、J2/J3、槽、孔、天线区与走线/过孔统计。
- [ ] 完成覆铜、飞线清零、严格 DRC、USB/GNSS RF/ESP32 天线/FPC 槽边/大电流回路人工复核；在此之前保持 `PROVISIONAL / DO NOT FAB`。

## 9. 2026-07-19 J6 内部扩展接口续作

- [x] 核对 GPIO 所有权：GPIO1 未分配且在 U5 上为真实 NC；GPIO43/44 已作为 UART0 调试网络接 TP8/TP9，允许并联扩展分支但必须保留启动日志/强驱冲突约束。
- [x] 冻结 J6 合同：`2.0-5P WZ-D / C41361332`、针序 `GND/3V3_EXP/GPIO1/GPIO43_TX/GPIO44_RX`，并增加 R38=0Ω/0805 与 R39～R41=33Ω/0603。
- [x] 保存 6 个原理图分页和 PCB，创建修改前快照 `esp32BmsGps-Hardware-R1.pre-expansion-j6-20260719-155506.eprj2`；SHA-256 `5622b9b3b08d7729af41429aee93f5165884402ae9ab64f821112110aafc7547`，SQLite `integrity_check=ok`。
- [x] 在 P3 创建 J6、R38～R41，解除 U5.GPIO1 的 NC 并完成显式网络连接；保存重开后 P3 为 17 个真实器件、64 条导线，严格 ERC 为 `fatal=0/error=0/warn=9/info=153`。5 条为同名网络重复标记，4 条为器件标准化建议，均已从 UI 面板归因。
- [x] 将原理图变更导入 PCB；导入默认停放在板框外，已移回左下板边内侧。最终 J6 中心 `(16.51,58.42)mm`，R38～R41 位于其上方；保存关闭重开后为 132 个器件、411 个焊盘、89 个网络，严格 DRC 无 Clearance/Keepout 错误。
- [x] 对 7 条低速扩展网络重试嘉立创EDA原生自动布线；两种参数写法均被后端忽略并对全部 89 个网络返回 `success=false、successNetsCount=0、duration=0`，调用前后线段/过孔为 `0/0`，未写入不受控结果。
- [x] 同步更新 `docs/hardware/04-constraints.md`、`05-validation.md`、`06-decisions.md`、`07-schematics.md`；最终快照 `post-expansion-j6-20260719-162657.eprj2` 的 SHA-256 为 `009d746ed937bc157982c8207f342ccad0836a459489d9dd5b46465bfe6b4a4b`、SQLite `integrity_check=ok`。任务继续保持 `PROVISIONAL / DO NOT FAB`。

## 10. 2026-07-19 J6 局部布线与板框修复

- [x] 将四条独立 Layer 11 `LINE` 合并/替换为一个闭合 `POLY`；单纯调整 Line 方向和 `zoomToBoardOutline()=true` 不能作为自动布线板框验收。板框仍为 `112×68mm`。
- [x] 保存关闭重开后，Layer 11 回读为 `LINE=0 / POLY=1`；DSN 成功导出 `88130` 字节且包含 `boundary(path ...)`，标准/JRouter 自动布线 JSON 分别为 `209915/210192` 字节。
- [x] 修复板框后仅对 `DBG_UART_RX_GPIO44` 重试原生自动布线；后端仍忽略白名单并返回 `success=false、totalNetsCount=89、successNetsCount=0、duration=0`，不保留原生自动结果。
- [x] 用单网探针验证 FreeRouting nm 坐标到 EasyEDA mil 坐标的转换；SES 导入会丢失/误解 Bottom 路径，因此最终按网络和图层逐段创建原生铜图元，不直接保留 SES 回导铜线。
- [x] 完成 7 个白名单网络的局部布线：`3V3_EXP / EXP_GPIO1 / EXP_UART_TX_GPIO43 / EXP_UART_RX_GPIO44 / GPIO1_EXP_RAW / DBG_UART_TX_GPIO43 / DBG_UART_RX_GPIO44`。TP8/TP9 按实际 Bottom SMD 层连接，跨层路径均先创建过孔再创建线段。
- [x] 保留 `USB_CONN_DP_DN` 与 `USB_MCU_DP_DN` 两组差分对，建立 `USB_CONN_LENGTH_MATCH` 与 `USB_MCU_LENGTH_MATCH` 两组等长约束，严格按 22Ω 串阻两侧分组。
- [x] 保存、关闭、重开后回读 `132 器件 / 411 焊盘 / 89 网络 / Top 24 线 / Bottom 14 线 / L2 0 线 / L3 0 线 / 6 过孔`；铜网络集合精确等于上述 7 网白名单。
- [x] 严格 DRC 回读仅 `Connection Error (358)`；7 个目标网络连接错误为 0，Clearance/Keepout 为 0。整板其余网络、覆铜和关键高速/电源回路仍未完成，不放行投板。
- [x] 生成快照 `esp32BmsGps-Hardware-R1.post-j6-routing-20260719-200622.eprj2`，SHA-256 `72e448577573d87c51aae087777dd7a26ff369637ae651e431fab1751ffe4d21`，SQLite `integrity_check=ok`。
- [x] 生成板框对象修复前/后快照 `pre-polyline-outline-20260719.eprj2` 和 `post-polyline-outline-20260719.eprj2`，SHA-256 分别为 `72e448577573d87c51aae087777dd7a26ff369637ae651e431fab1751ffe4d21` 与 `1fad6e78b9b2cd365682aad415e6510de7d1904156fd0b2f222853e1164a2e8b`，SQLite `integrity_check=ok`。
