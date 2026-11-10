# 硬件设计文件

当前版本：[2026-11-11：两个 FPC 与排线槽左移 1.8 mm](2026-11-11-fpc-left18/README.md)。

| 文件 | 用途 |
| --- | --- |
| [嘉立创工程 epro2](2026-11-11-fpc-left18/ESP32-BMS-GPS-fpc-left18-20261111.epro2) | 导入嘉立创 EDA 专业版，包含原理图与 PCB |
| [本地工程 eprj2](2026-11-11-fpc-left18/ESP32-BMS-GPS-fpc-left18-20261111.eprj2) | 嘉立创 EDA 专业版本地工程 |
| [原理图源文件](2026-11-11-fpc-left18/P1-final.esch) | 最终原理图 |
| [PCB 源文件](2026-11-11-fpc-left18/PCB1-final.epcb) | 最终 PCB |
| [Gerber 制造文件](2026-11-11-fpc-left18/Gerber-fpc-left18-20261111.zip) | PCB 制板输出 |
| [BOM 与采购说明](2026-11-11-fpc-left18/BOM.md) | 型号、料号、装配状态及选型说明 |
| [采购 BOM CSV](2026-11-11-fpc-left18/BOM-final-20261111.csv) | UTF-8 CSV，含 R9 不装记录 |
| [全板预览](../preview/pcb-fpc-left18-native-20261111.webp) | 查看板图 |

同目录保存 DRC、连通性、槽周铜距检查记录及 SHA-256 校验清单。保存重开后严格 DRC 为零，51 个命名网络连通，槽周最小铜距约 1.553 mm。

尚未进行排线和外壳的实物装配验证。历史中间版本、自动备份与临时文件未纳入本次归档。

R5 已改为 2.2 Ω，其余已选型电阻的采购属性已同步至原理图、PCB 和完整工程。R4 已按历史选型改为 453 kΩ（厚声 `0805W8F4533T5E` / `C17702`），36 个装配器件均已填写采购料号；R9 不装。

感谢初版作者 **luose** 分享的 [ESP32 BMS GPS 初版项目](https://oshwhub.com/luose/project_dffyscoz)。

## 外接屏幕采购地址

用户提供的淘宝商品：[4寸液晶屏3.98寸IPS显示屏模块串口7796电阻电容触摸屏并口40pin](https://e.tb.cn/h.jjmM3tuvX07IMUY?tk=oQdn62FEg42)。淘口令：`oQdn62FEg42`。

链接包含不同接口、触摸选项。下单时请与本项目 40Pin 显示 FPC、6Pin 触摸 FPC 的定义、排线方向和屏幕尺寸核对；商品标题不能保证每个选项都适配。此处记录购买地址，未核验店铺实时价格、库存或具体选项。
