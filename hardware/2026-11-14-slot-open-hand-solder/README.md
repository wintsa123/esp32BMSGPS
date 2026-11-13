# 顶部贯通开口与手焊间距调整 · 2026-11-14

基准：`../2026-11-11-fpc-left18/PCB1-final.epcb`。通过嘉立创专业版 4.1.78 原生 API/MCP 编辑、重铺铜及导出。

## 打开与文件

- `ESP32-BMS-GPS-slot-open-hand-solder-20261114.eprj2`：可直接打开的本地工程，已关闭再重新打开验证。
- `ESP32-BMS-GPS-slot-open-hand-solder-20261114.epro2`：专业版工程导出包。
- `PCB1-final.epcb`：最终 PCB 源文档；`PCB1-before.epcb`：改动前基准。
- `Gerber-slot-open-hand-solder-20261114.zip`：与最终 PCB 同次导出的制造文件。
- `P1-final.esch`、`BOM-final-20261114.csv`：原理图和物料清单，与基准一致。
- `component-moves.json`：实际器件位置变更，坐标单位 mil。

## 变化

小 FPC 上方的闭合开孔已贯通 PCB 顶边，成为宽 13 mm、深 15.4 mm、底角 R1 mm 的开口。大槽仍为 29 × 3 mm。大、小 FPC 接口、ESP32、射频模块、USB 及固定孔的位置保持原设计。

触摸 FPC 附近的 SDA_TH、SCL_TH、INT、RST 和供电分支从大槽右侧向下绕行。顶部开口及大槽设有铜皮、平面、走线和过孔禁布区域，Gerber 两面实测最小槽边铜净距约 1.55254 mm。

移动 Q1 及周边电阻、U6 与上拉和电容、U3 与 L2 和输入输出电容、U4 周边器件，以及 U5 附近的 R6/C7，增加焊接空间。同步调整受影响走线和 3.3V 输出铺铜。

以下数值为每个器件焊盘与其他器件焊盘的最近铜边净距，不包含器件自身引脚之间的间距，单位 mm：

| 器件 | 调整前 | 调整后 |
| --- | ---: | ---: |
| Q1 | 1.48 | 2.15 |
| U3 | 0.44 | 2.39 |
| U4 | 0.65 | 2.55 |
| U5 | 0.51 | 0.98 |
| U6 | 0.26 | 1.53 |
| L2 | 0.44 | 2.46 |

U5 当前最近邻为 USB 屏蔽壳焊盘，净距约 0.98 mm。器件数量、封装、料号和网络分配均已逐项核对：37 个器件、213 个物理焊盘。R4 仍为 453kΩ，R5 仍为 2.2Ω，R9 保持原 NC/DNP 策略。

## 验证

- `drc-final.json`：原生严格 DRC 为 0 项。
- `reopen-check.json`：保存并关闭工程后重新打开，全部功能图元一致，严格 DRC 仍为 0 项。
- `verification-final.json`：元器件、封装、焊盘网络、原理图、开槽拓扑与右侧绕线位置核对。
- `review-gerber-connectivity.json`：独立合并 Gerber 两面铜，按过孔及金属化通孔连接；51 个网络、197 个有网络焊盘，无断线或短路。
- `review-slot-copper.json`：独立测量实际 Gerber 铜与两个开口的间距，两面均不小于 1.55 mm。
- `review-via-drills.json`：226 个过孔在钻孔文件中的中心与孔径均匹配。
- Gerber 板框为有效单板轮廓，顶部缺口连通外部，仅保留一个内部大槽。

## 预览

- [整体原生画布](../../preview/pcb-slot-open-native-20261114.png)
- [顶部开口与右侧绕线](../../preview/pcb-slot-open-fpc-20261114.png)
- [手焊区域](../../preview/pcb-hand-solder-20261114.png)

尚未打样、实焊或进行上电、温升和 EMC 实测。封装自身的引脚间距保持原规格。
