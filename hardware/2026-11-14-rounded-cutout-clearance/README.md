# 顶部外圆角与器件避让调整 · 2026-11-14

基准：[上一版](../2026-11-14-slot-open-hand-solder/README.md)。使用嘉立创专业版 4.1.78 原生 API/MCP 编辑、重铺铜并导出。

## 文件

- `ESP32-BMS-GPS-rounded-cutout-clearance-20261114.eprj2`：本地工程，直接打开。
- `ESP32-BMS-GPS-rounded-cutout-clearance-20261114.epro2`：完整工程导出包。
- `PCB1-final.epcb`：最终 PCB；`PCB1-before.epcb`：本次改动前基准。
- `Gerber-rounded-cutout-clearance-20261114.zip`：对应最终 PCB 的制造文件。
- `P1-final.esch`、`BOM-final-20261114.csv`：原理图和物料清单。
- `component-moves.json`：器件坐标变化，单位 mil。

## 板框和位置

小 FPC 上方开口保持原来的 13 mm 直壁宽度、15.4 mm 深度和 R1 mm 底角，仅将顶部左右入口做成 R0.5 mm 凸外圆角。入口与顶边及竖直槽壁相切；顶部最宽 14 mm。它属于连续板框，没有附加圆形钻孔，也没有扩大槽底。FPC2 丝印移到开口左下方。

按用户最后确认，FPC1、大 FPC 对应的 29 × 3 mm 内槽及其周边铜距规则沿用基准，不再扩大其禁布范围。小 FPC 顶部开口铜距另行增大。两座 FPC 本体、安装孔与 ESP32/USB 位置不变。

R6 上移 2.54 mm，并右移 1.016 mm 以避开 ESP32。RF2、L1、U2、C1 整组下移 8.89 mm，保留组内天线匹配走线的宽度与形状。四个支撑柱按安装孔圆心分别预留 R4 mm；左下安装孔旁实际器件位号为 R19，将其上移 3.048 mm。R9 保持不装。

## 验证与预览

具体数值和检查结果见同目录 JSON：原生严格 DRC、保存重开、独立 Gerber 连通性、槽边铜距、过孔钻孔、器件焊盘净距和四个安装孔周围的器件外形避让。板框单独验证入口是两个相切的凸四分之一圆弧，且小口通外、只有一个内部大槽。

- [制造层整体预览](../../preview/pcb-rounded-physical-20261114.png)
- [实际 Gerber 入口外圆角放大](../../preview/pcb-rounded-outline-detail-20261114.png)
- [原生 PCB 画布](../../preview/pcb-rounded-native-20261114.png)
- [支撑柱 R4 辅助范围](../../preview/pcb-rounded-pillar-guides-20261114.png)

制造层预览不显示禁布区和机械辅助圈。支撑柱图里的蓝圈仅表示检查范围，不是新增钻孔。

本次新增补线采用 45° 路由；基准中已有的其他角度线段不做全板重布。USB 信号线路逐段核对，GNSS 天线局部走线随器件刚性平移。几何、间距与连通性检查不能替代信号完整性测量；尚未打样、实焊或完成射频、眼图、EMC 与外壳 3D 装配验证。

本次导出检查结果：严格 DRC 为 0；51 个命名网络、197 个有网焊盘的独立 Gerber 检查无断线或短路；R6 与 C6 的焊盘净距为 2.783 mm；小 FPC 顶部开口两面最小铜距为 2.545 mm，大槽为 1.553 mm。222 个过孔在钻孔文件中的中心和孔径全部匹配。保存重开后功能图元一致、DRC 仍为 0。
