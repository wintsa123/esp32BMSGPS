# 小 FPC 顶部开口加宽 · 2026-11-14

基准为 [上一版](../2026-11-14-fpc2-outline-label/README.md)。使用嘉立创专业版 4.1.78 原生 API 编辑并重铺铜。

开口直壁宽从 11 mm 加至 **14 mm**，以原中心线向左右各扩 1.5 mm；深度仍为 13.8 mm，顶部与底部圆角均为 R1 mm，入口最宽 16 mm。利用原有避铜范围增加实际开口，禁布区域和现有线路无需移动。FPC2 旧版横放丝印、其下方板材及 `wintsa-fuckBms-GPS V1.0` 板名保留。

- `wintsa-fuckBms-GPS-V1.0-20261114.eprj2`：直接打开的最新本地工程。
- `wintsa-fuckBms-GPS-V1.0-20261114.epro2`：完整工程导出包。
- `Gerber-fpc2-wider-opening-20261114.zip`：本版制造文件。
- `PCB1-before.epcb`、`PCB1-final.epcb`：加宽前后 PCB 源文件。
- `P1-final.esch`、`BOM-final-20261114.csv`：原理图与物料清单。

制造文件测得小开口双面最小铜距为 1.561 mm，大槽为 1.553 mm。严格 DRC 和保存重开后的严格 DRC 均为 0 项；独立 Gerber 检查覆盖 51 个网络、197 个有网焊盘，无断线或短路。全部钻孔的中心及孔径保持不变，顶部两角为连续板框的凸四分之一圆弧。

检查记录见 `verification-final.json`、`review-gerber-connectivity.json`、`review-slot-copper.json`、`review-silkscreen-notch.json`；文件校验见 `SHA256SUMS`。

预览：[整板](../../preview/pcb-fpc2-wider-physical-20261114.png)、[开口局部](../../preview/pcb-fpc2-wider-detail-20261114.png)。尚未进行实物排线穿装和外壳装配验证。
