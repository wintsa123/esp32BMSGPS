# 硬件设计文件

当前版本：[2026-11-14：小 FPC 顶部开口加宽至 14 mm](2026-11-14-fpc2-wider-opening/README.md)。

| 文件 | 用途 |
| --- | --- |
| [嘉立创工程 epro2](2026-11-14-fpc2-wider-opening/wintsa-fuckBms-GPS-V1.0-20261114.epro2) | 导入专业版，包含原理图与 PCB |
| [本地工程 eprj2](2026-11-14-fpc2-wider-opening/wintsa-fuckBms-GPS-V1.0-20261114.eprj2) | 直接打开的本地工程 |
| [PCB 源文件](2026-11-14-fpc2-wider-opening/PCB1-final.epcb) | 最终 PCB |
| [原理图](2026-11-14-fpc2-wider-opening/P1-final.esch) | 原理图源文档 |
| [Gerber](2026-11-14-fpc2-wider-opening/Gerber-fpc2-wider-opening-20261114.zip) | 制造文件 |
| [BOM](2026-11-14-fpc2-wider-opening/BOM-final-20261114.csv) | 采购清单，R9 不装 |
| [制造层预览](../preview/pcb-fpc2-wider-physical-20261114.png) | 成品板框、铜层和丝印 |
| [顶部外圆角放大](../preview/pcb-fpc2-wider-detail-20261114.png) | 14 mm 开口与 FPC2 横向丝印 |
| [支撑柱避让](../preview/pcb-fpc2-wider-pillar-guides-20261114.png) | 四个安装孔为圆心，R4 mm 辅助范围 |

小 FPC 顶部开口加宽至 14 mm 直壁宽、13.8 mm 深，顶部为连续板框的 R1 mm 凸外圆角，底角 R1 mm，入口最宽 16 mm。FPC2 丝印恢复旧版横向位置，保留文字下方板材；右上板名为 `wintsa-fuckBms-GPS V1.0`。钻孔、大槽和现有走线保持上一版设计。

R6 上移并与 C6 拉开距离；RF2/L1/U2/C1 整组下移；左下安装孔旁的 R19 上移。四个安装孔分别按孔心 R4 mm 检查元件焊盘与封装外形。保留此前三脚、六脚器件和 L2 周围的手焊间距调整。

同目录保存原生 DRC、独立 Gerber 连通性、铜距、重开和 SHA-256 记录。尚未进行打样实焊、信号完整性实测或外壳装配验证。

历史版本：[FPC2 丝印恢复与板名](2026-11-14-fpc2-outline-label/README.md)；[顶部凸外圆角与器件避让](2026-11-14-rounded-cutout-clearance/README.md)；[顶部贯通及手焊间距调整](2026-11-14-slot-open-hand-solder/README.md)；[FPC 左移 1.8 mm](2026-11-11-fpc-left18/README.md)。

R5 已改为 2.2 Ω，其余已选型电阻的采购属性已同步至原理图、PCB 和完整工程。R4 已按历史选型改为 453 kΩ（厚声 `0805W8F4533T5E` / `C17702`），36 个装配器件均已填写采购料号；R9 不装。

感谢初版作者 **luose** 分享的 [ESP32 BMS GPS 初版项目](https://oshwhub.com/luose/project_dffyscoz)。

## 外接屏幕采购地址

用户提供的淘宝商品：[4寸液晶屏3.98寸IPS显示屏模块串口7796电阻电容触摸屏并口40pin](https://e.tb.cn/h.jjmM3tuvX07IMUY?tk=oQdn62FEg42)。淘口令：`oQdn62FEg42`。

链接包含不同接口、触摸选项。下单时请与本项目 40Pin 显示 FPC、6Pin 触摸 FPC 的定义、排线方向和屏幕尺寸核对；商品标题不能保证每个选项都适配。此处记录购买地址，未核验店铺实时价格、库存或具体选项。
