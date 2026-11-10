# 两个 FPC 与排线槽左移 1.8 mm

2026-11-10 开始，2026-11-11 完成。基于 `2026-10-26-hand-solder-slot15` 最终版，通过嘉立创 MCP 修改另存。修改前 PCB 随本目录 `PCB1-before.epcb` 保留；此前完整工程保留在本地历史目录。

用户要求：两个 FPC 和对应开孔一起向左移动 1.8 mm。

同日更新：R5 改为 **2.2 Ω**（厚声 `0805W8F220KT5E`，立创 `C17521`）；R4 按历史选型改为 **453 kΩ**（厚声 `0805W8F4533T5E`，立创 `C17702`）；共补齐 12 个待选型电阻焊位的采购属性。R9 保持不装，36 个装配器件均有采购料号，见 [完整 BOM 与采购说明](BOM.md)。这些属性已同步至原理图、PCB、`.eprj2` 和 `.epro2`；PCB 几何与针脚网络保持左移版一致，因此 Gerber 沿用几何相同的制造输出。

## 修改

- FPC1、FPC2 的 X 坐标减小 1.8 mm，Y 坐标和旋转角保持；对应丝印一起移动。
- 大、小排线槽及各自铜禁区同步左移 1.8 mm，槽的长宽、圆角保持。
- 调整相关引线和附近过孔，重新铺铜。为确保接地连通，增加一个地过孔及两段接地线。全板线段 603 → 719，过孔 255 → 256。
- FPC 左移时，其他 35 个器件的位置和属性、板框、四个安装孔、针脚网络映射、原理图、规则、铺铜边界和优先级保持；随后 R4、R5 与 BOM 更新仅修改器件属性。
- 坐标按编辑器精度落盘，1.8 mm 位移的舍入误差约 0.0000011 mm。

## 验证

- 保存、关闭、从最终 `.eprj2` 重开后，严格 DRC 为 `[]`。
- 重开后的持久化设计图元与最终 PCB 源一致；`.epro2` 包含匹配的当前器件、线段、过孔、槽和禁区几何。
- 独立读取最终 Gerber 铜形状，两个槽到上下层铜的最小净距为 1.55253～1.55254 mm，满足 1.5 mm 要求。圆弧离散误差上限 0.00001 mm。
- 独立按 Gerber 两层铜岛、过孔和通孔焊盘连接关系检查：51 个命名网络全部连通，没有异网短路。
- `.eprj2` 数据库完整性检查为 `ok`。

详细证据：`verification-final.json`、`reopen-check.json`、`review-slot-copper.json`、`review-gerber-connectivity.json`。

R4、R5 / BOM 更新的保存重开和属性一致性检查见 `verification-bom-20261111.json`；`verification-final.json` 中原理图和器件属性保持不变的结论仅描述此前 FPC 左移操作。

## 文件

- 可导入工程：`ESP32-BMS-GPS-fpc-left18-20261111.epro2`
- 本地工程：`ESP32-BMS-GPS-fpc-left18-20261111.eprj2`
- 制造文件：`Gerber-fpc-left18-20261111.zip`
- 最终 PCB 和原理图源：`PCB1-final.epcb`、`P1-final.esch`
- 入场 PCB 备份：`PCB1-before.epcb`
- 采购 BOM：`BOM-final-20261111.csv`（UTF-8，包含 R9 不装记录）；原生 BOM：`BOM-native-final-20261111.csv`（编辑器原始 UTF-16 制表符格式，不含 R9）
- 原生全板预览：[原生全板预览](../../preview/pcb-fpc-left18-native-20261111.webp)
- 源几何局部示意：[局部几何示意](../../preview/pcb-fpc-left18-detail-20261111.png)，不含铺铜，坐标单位 mil。

本目录更新仅涉及硬件设计与采购资料，没有变更固件或热点行为；未做排线、外壳的实物装配验证，也未做电气或 EMC 实测。

## 致谢

感谢初版作者 **luose** 在立创开源硬件平台分享的 [ESP32 BMS GPS 初版项目](https://oshwhub.com/luose/project_dffyscoz)，为后续硬件迭代提供了基础。
