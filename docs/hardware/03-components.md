# esp32BmsGps 车机硬件方案

- 日期：2026-07-19
- 阶段：4 - 关键器件

## 关键器件

| 模块 | R1 选择 | 采购参考 | Datasheet | 封装状态 |
|---|---|---|---|---|
| MCU/无线 | ESP32-S3-WROOM-1-N16R8 | [LCSC 搜索](https://www.lcsc.com/search?q=ESP32-S3-WROOM-1-N16R8) | [乐鑫 PDF](https://documentation.espressif.com/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf) | 使用模组原厂 land pattern |
| eFuse | SGM2536FRXTSP10G/TR | [LCSC C48384692](https://www.lcsc.com/product-detail/C48384692.html) | [原厂 PDF](https://www.sg-micro.com/rect/assets/48779d0e-8f4f-4e63-8f85-b8c6c4f43862/SGM2536.pdf) | `VQFN-10_L2.0-W2.0-P0.45-TL`，实例已核对 pin3=`SPGD`、pin4=`nFAULT` |
| 3.3V/5V Buck | SY8113IADC ×2 | [LCSC C479075](https://www.lcsc.com/product-detail/C479075.html) | [立创附件](https://atta.szlcsc.com/upload/public/pdf/source/20200117/C479075_749CE19A0276D274B25CFED6D9E6F64F.pdf) | TSOT23-6 |
| 数字功放 | HT517SPER | [世强](https://www.sekorm.com/product/578573847.html) | [HT517 PDF](https://onwaytech.com/static/upload/file/20260518/1779083844232451.pdf) | ESOP8，必须含 EP |
| GNSS | ATGM336H-F8N-76 | [LCSC C46955980](https://www.lcsc.com/product-detail/C46955980.html) | [立创附件](https://datasheet.lcsc.com/datasheet/pdf/634811cc75352d43fbcb4d09e6284933.pdf?productCode=C46955980) | 10.1×9.7mm 18Pin |
| LCD/CTP | 40001 对应 ILI9488+FT6336U 屏总成 | 供应商样品渠道 | 仓库 40001 PDF + [ILI9488 PDF](https://www.mouser.com/pdfDocs/ILI9488_Data_Sheet_100.pdf) | 40Pin 上接 + 6Pin 下接 |

### 连接器、保护与制造资源

| 模块 | R1 选择 | 采购参考 | Datasheet | 封装状态 |
|---|---|---|---|---|
| LCD FPC 座 | FPC-0.5HF-40PWBH20 | [LCSC C3446044](https://www.lcsc.com/product-detail/C3446044.html) | [本地 PDF](datasheets/FPC-0.5HF-40PWBH20.pdf) | `FPC-SMD_40P-P0.50_FPC-0.5HF-40PWBH20`，41/42 为机械脚且标 NC |
| CTP FPC 座 | FH12-6S-0.5SH(55) | [LCSC C202118](https://www.lcsc.com/product-detail/C202118.html) | [本地 PDF](datasheets/FH12-6S-0.5SH-55.pdf) | `FPC-SMD_FH12-6S-0.5SH-55`，7/8 为机械脚且标 NC |
| 壳内扩展座 | 2.0-5P WZ-D | [LCSC C41361332](https://www.lcsc.com/product-detail/C41361332.html) | [立创资料页](https://item.szlcsc.com/datasheet/2.0-5P%20WZ-D/43105552.html) | `CONN-TH_5P-P2.00_2.0-5PWZ-D`；PH2.0 卧式弯插通孔，标注不耐回流，只允许回流后手工/选择焊 |
| USB-C | KH-TYPE-C-16P | [LCSC C709357](https://www.lcsc.com/product-detail/C709357.html) | [立创附件](https://datasheet.lcsc.com/datasheet/pdf/f81b4927a4ee2be0830446b63c7a6631.pdf?productCode=C709357) | 按原厂槽孔图核对 |
| USB ESD | UMW USBLC6-2SC6 | [LCSC C2687116](https://www.lcsc.com/product-detail/C2687116.html) | [立创附件](https://datasheet.lcsc.com/datasheet/pdf/cb339b3dae40ca0f3986b4e9f6690755.pdf?productCode=C2687116) | SOT-23-6 |
| CTP ESD | D3=`USBLC6-4SC6-ES` | [LCSC C5180279](https://www.lcsc.com/product-detail/C5180279.html) | [本地 ElecSuper Rev-1.5](datasheets/D3_ElecSuper_USBLC6-4SC6-ES_Rev-1.5.pdf) | SOT-23-6；默认装配 |
| GNSS RF ESD 调试位 | D4=`PESD5V0R1BSFYL` | [LCSC C461221](https://www.lcsc.com/product-detail/C461221.html) | [本地 Nexperia v3](datasheets/D4_Nexperia_PESD5V0R1BSF_v3_2023-09-06.pdf) | SOD-962；强制 DNP，不作为当前可装 RF ESD |
| 保险丝 | JFC1206-1300FS 3A | [LCSC C136347](https://www.lcsc.com/product-detail/C136347.html) | [立创附件](https://datasheet.lcsc.com/datasheet/pdf/73904579fd0e8ff82c89a7d3f737acbc.pdf?productCode=C136347) | 1206 |
| TVS | SMBJ17A | [LCSC C284003](https://www.lcsc.com/product-detail/C284003.html) | [立创附件](https://datasheet.lcsc.com/datasheet/pdf/2459a58947adc1597b4c1dcdf9422ca7.pdf?productCode=C284003) | SMB/DO-214AA |
| 低漏电二极管 | BAV199-7-F | [LCSC C155248](https://www.lcsc.com/product-detail/C155248.html) | [Diodes PDF](https://www.diodes.com/datasheet/download/BAV199.pdf) | SOT-23 |
| GNSS 后备电容 | CHP5R5L105R-TW，5.5V/1F | [LCSC C2925920](https://www.lcsc.com/product-detail/C2925920.html) | 无独立 PDF；参数以采购页和实例属性为准 | `CAP-TH_L16.0-W8.0-P11.5-D0.6`，横卧 H 型、11.5mm 脚距 |
| Pogo PCB 触点 | POGO_PAD_2V0_ENIG_R1 | PCB 工艺特征，不采购器件 | 不适用 | `TESTPOINT-SMD_BD1.5_P70-5000045R`；实测库焊盘约 2.00mm、无孔、BOM 排除 |
| GNSS 同轴座 | U.FL-R-SMT 类 | [LCSC 搜索](https://www.lcsc.com/search?q=U.FL-R-SMT) | 原厂图纸随最终料号 | 需确认线束配套与插拔寿命 |

## 资料下载状态

除 FT6336U 独立裸 IC 手册、C16 独立 PDF、J6 独立可下载 PDF 和最终 U.FL 精确型号外，R1 所需关键手册已下载到 `docs/hardware/datasheets/`。J2/J3/J6 的连接器资料与封装已经核对；J6 的库条目和采购页共同确认 2.0mm、1×5、弯插通孔、2A、`-25～85°C`，但“不耐温”意味着不能进入回流炉。LCD/CTP 是外购成品总成，主板只连接其 FPC，不把 FT6336U 裸芯片作为 PCB 物料。C16 的容量、耐压、漏电、ESR、温度和机械参数已写入 EasyEDA 实例，来料时仍需按采购规格复验。

2026-07-18 新增资料均经 `%PDF`、页数、文本和首页渲染校验：

- `D3_ElecSuper_USBLC6-4SC6-ES_Rev-1.5.pdf`：8 页，SHA-256 `1d3a759ffaf60022154d620b21eeb952f35c15d8cdf211efc82bad8ef49ebaf0`；已确认是 ElecSuper `-ES` 版本，不是 ST 同名资料。
- `D4_Nexperia_PESD5V0R1BSF_v3_2023-09-06.pdf`：11 页，SHA-256 `dc985524e3485c70dc7f777452709b499939c13ce0e0c464432206bfadf179f5`。
- `D4_Nexperia_PESD5V0R1BSF_v2_2015-05-07_LCSC-backup.pdf`：12 页，SHA-256 `1c24a0cccc0750da19d1989fce00f3f1dc3887c4b8c764fa94f7d27f157c08b1`。

ATGM336H-F8N-76 的机械尺寸已于 2026-07-19 交叉核验：原厂[产品页](https://www.hzzkw.com/daohang/duopin/2513.html)标称 `10.1×9.7×2.1mm`，LCSC `C46955980` 标为 `SMD,10.1×9.7mm`，本地手册的概述与 PCB 焊盘图也支持 `10.1×9.7mm`。但同一手册技术规格表又出现与前文矛盾的 `16.0×12.2×2.4mm`，EasyEDA 3D 模型则为 `L10.1-W9.7-H2.4`。因此 R1 保留现有 `10.1×9.7mm` 焊盘与本体 XY，Z 向按 `2.4mm` 最坏值预留；首批来料必须实测高度并复核 3D/结构间隙，不把手册中的矛盾行当作本体 XY 依据。

## 强制 DNP 决策

| 位号 | 当前状态 | 原因 | 放行条件 |
|---|---|---|---|
| D4 `PESD5V0R1BSFYL` | `AddIntoPcb=true`、`AddIntoBom=false`，强制 DNP | v2 明确排除连接 DC supply 的线路；v3 警告不得连接无限流 DC 源；`GNSS_RF_ANT` 带 3.3V 偏置，适用边界未关闭 | 换成明确支持 DC-biased RF、`Cj≤0.2pF` 的器件并完成 RF/定位/天线状态实测 |
| C36 `470µF/16V` | `AddIntoPcb=true`、`AddIntoBom=false`，强制 DNP | 约 524µF 总负载在 1ms 软启动下理想充电约 2.62A，可能触发 2.38A eFuse 限流 | 完成 5V/12V 启动、反复通断、掉压和 `nFAULT` 测试后再决定试装 |

## 替代与风险

| 当前选择 | 替代路径 | 影响 |
|---|---|---|
| SGM2536FR | PW1558（本版作废；未来仅可作经完整资料和样品验证的候选） | 仅替换输入保护页，不做兼容焊盘 |
| SY8113IADC | 热能力更高的 18V 同步 Buck | 可能重画电源布局和补偿 |
| HT517SPER | 重新评审 NS4168/MAX98357A | 引脚、驱动和功率均不兼容 |
| ATGM336H-F8N-76 | 本版不设兼容模块 | 需新 PCB 与天线验证 |
| 4.0 寸屏 | 相同 40+6Pin、ILI9488/FT6336U 兼容 SKU | 必须先核对 FPC 和背光 |
