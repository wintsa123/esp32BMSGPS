# FPC2 开口、外圆角与板名丝印 · 2026-11-14

基准为 [上一版](../2026-11-14-rounded-cutout-clearance/README.md)。通过嘉立创专业版 4.1.78 原生 API 编辑、重铺铜、保存并重新打开验证。

- `wintsa-fuckBms-GPS-V1.0-20261114.eprj2`：可直接打开的本地工程。
- `wintsa-fuckBms-GPS-V1.0-20261114.epro2`：包含原理图、PCB 与封装的工程导出包。
- `Gerber-fpc2-outline-label-20261114.zip`：本次对应的制造文件。
- `PCB1-final.epcb`、`PCB1-before.epcb`：修改后及修改前的 PCB 源文件。

小 FPC 顶部贯通开口的直壁宽由 13 mm 缩至 11 mm，深度由 15.4 mm 缩至 13.8 mm，入口最宽 13 mm。入口左右顶部各使用 R1 mm 凸外圆角，与顶边及槽壁相切，属于连续板框；底部保留 R1 mm。钻孔文件的所有孔位和孔径均与上一版相同，没有增加角部圆孔。

FPC2 位号恢复为旧版的横放位置、字号与角度，整块文字范围都位于板材上，文字包围框距开口至少 1.691 mm，实际丝印制造图形距开口至少 1.815 mm。右上空白区域新增板名 `wintsa-fuckBms-GPS V1.0`，与用户要求大小写一致。

本次没有改变元器件位置、焊盘网络、线路、过孔、大 FPC 内槽或原理图。FPC2 及相邻触摸元件的走线保留此前从大 FPC 右侧向下的方案。

严格 DRC 为 0 项，保存重开后仍为 0，功能图元一致。独立 Gerber 检查覆盖 51 个网络、197 个有网焊盘，无断线或短路；小开口双面最小铜距 2.970 mm，大槽为 1.553 mm。检查记录见同目录 `verification-final.json` 与 `review-*.json`；文件校验见 `SHA256SUMS`。

预览：[整板制造层](../../preview/pcb-fpc2-final-physical-20261114.png)、[开口与 FPC2 局部](../../preview/pcb-fpc2-final-detail-20261114.png)。图像来自本次 Gerber，不包含机械辅助圈。

尚未进行实物排线穿装、打样或外壳装配验证。
