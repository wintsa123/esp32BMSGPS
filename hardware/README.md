# 硬件设计文件

当前版本：[2026-11-11：两个 FPC 与排线槽左移 1.8 mm](2026-11-11-fpc-left18/README.md)。

| 文件 | 用途 |
| --- | --- |
| [嘉立创工程 epro2](2026-11-11-fpc-left18/ESP32-BMS-GPS-fpc-left18-20261111.epro2) | 导入嘉立创 EDA 专业版，包含原理图与 PCB |
| [本地工程 eprj2](2026-11-11-fpc-left18/ESP32-BMS-GPS-fpc-left18-20261111.eprj2) | 嘉立创 EDA 专业版本地工程 |
| [原理图源文件](2026-11-11-fpc-left18/P1-final.esch) | 最终原理图 |
| [PCB 源文件](2026-11-11-fpc-left18/PCB1-final.epcb) | 最终 PCB |
| [Gerber 制造文件](2026-11-11-fpc-left18/Gerber-fpc-left18-20261111.zip) | PCB 制板输出 |
| [全板预览](../preview/pcb-fpc-left18-native-20261111.webp) | 查看板图 |

同目录保存 DRC、连通性、槽周铜距检查记录及 SHA-256 校验清单。保存重开后严格 DRC 为零，51 个命名网络连通，槽周最小铜距约 1.553 mm。

尚未进行排线和外壳的实物装配验证。历史中间版本、自动备份与临时文件未纳入本次归档。
