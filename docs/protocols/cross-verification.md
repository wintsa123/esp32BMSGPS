# 保护板 / 控制器协议交叉验证报告

参照实现：Motexa 2.7.0 反编译源码（`/vol1/1000/project/_ref/motexa270-src/sources/`，JADX 输出）。
被测实现：本仓库 `components/esp_bms_bms_ble/protocols/*` 与 `components/esp_fardriver_protocol/`。

方法：先读本项目实现与 `tests/*_selftest.c`，列出已声明的帧格式、偏移与换算；再读 Motexa 对应类，逐项对照
帧头 / 长度 / 校验 / 命令帧 / 字段偏移 / 符号与字节序 / 单位与缩放 / 范围校验 / 半包与粘包行为。
只修改有帧级证据的确定性错误；语义存疑的一律只记录。

## 参照文件映射（已核实）

| 协议 | Motexa 实现 | 作用 |
| --- | --- | --- |
| ANT `ant_ble_new` | `com/motexaqixing/app/k.java` `t()` / `u()`（第 289–406 行） | 新帧拆分、CRC、状态解析 |
| ANT `ant_ble_legacy` | `k.java` `s()` / `u()`（第 143–287 行） | 旧帧校验、状态解析 |
| ANT 轮询 | `k.java` `c()` / `d()` / `e()` / `f()` | `DB DB 00 00 00 00` 与 `7E A1 …` 请求 |
| JK02 / JK04 | `com/motexaqixing/app/q0.java` `r()` / `s()`（第 640–775 行） | 帧拆分、单位换算 |
| JK 命令 | `q0.java` `d(int)` / `c(int)` | `AA 55 90 EB …` 轮询帧 |
| JBD | `q0.java` `p()`（第 576–622 行）、`c(int)` | 载荷解析、`DD A5 …` 请求 |
| 彦阳 | `v6/i1.java` `D()`（341–348）、`u()`（761–764）、`s()`（748–760）、`m()`（620–640）、`enum f`（283–286） | Modbus 请求/校验/页解析 |
| FarDriver | `com/motexaqixing/app/s6.java` `C()`（157–196）、`F()`（258–266）、`z()`（326–338）、`r()`（460–505）；`e8.java` 字段表（21–190） | 16 字节帧、CRC、块字段 |
| DALY | **2.7.0 中不存在** | 仅自洽性检查 |

字段语义取自 `u0.java`（`u0.a/b/c` 把 `q0.e` 字段映射到 `pack.voltage` / `cells.*` / `temps.mosfet` 等键）。

---

## 1. ANT（`ant_ble_new` + `ant_ble_legacy`）

### 结论

| 项目 | 参照依据 | 结论 | 影响 |
| --- | --- | --- | --- |
| 新帧头 `7E A1`、尾 `AA 55`、长度 `10+data[5]` | `k.u()`：`i10=bArr[5]; i11=i10+6; i12=i10+8` | 相同 | — |
| 新帧 CRC16/Modbus，范围 `[1, 6+data[5])` | `k.h()`（初值 `0xFFFF`、poly `0xA001`）；`k.u()` 对 `h(bArr,1,i11)` 比较 | 相同 | — |
| 温度块起点 `34+2*cells`、动态块 `i12=34+2*cells+2*temps` | `k.t()`：`i11=(i10*2)+34; i12=(i9*2)+i11` | 相同 | — |
| 包电压 `i12+4`、电流 `i12+6`、SOC `i12+8` | `k.t()` 第 307、308、310 行 | 相同 | — |
| 设计容量 `i12+16`、剩余 `i12+20`、循环容量 `i12+24`、运行时间 `i12+32` | `k.t()` 第 312–314、317 行（`/1e6`、`/1e6`、`/1000`、秒） | 相同（本项目换算成 mAh 后数值一致） | — |
| 保护位 `n(10)`、告警位 `n(18)`（u64 小端） | `k.t()`：`U=Long(n(10))`、`V=Long(n(18))` | 相同 | — |
| 电芯电压上限 32、温度数量上限 6 | `k.t()`：`i9<=6 && i10<=32` | 相同（本项目只取 4+2 个温度槽） | — |
| 旧帧校验：`sum(data[4..137]) == BE16(data[138..139])` | `k.s()`/`k.u()`：`bArr[138]` 高字节、`bArr[139]` 低字节，再与 `g(bArr,4,138)` 求和比较 | 相同 | — |
| 旧帧包电压 `BE16(4)/10 V` | `k.s()` 第 237 行 `y(bArr,4)/10.0` | 相同（本项目 `×100 mV`） | — |
| 旧帧剩余/设计容量 `BE32(75/79)/1e6 Ah` | `k.s()` 第 242–243 行 | 相同 | — |
| 旧帧循环容量 `BE32(83)/1000 Ah` | `k.s()` 第 244–245 行 | 相同（原始值即 mAh） | — |
| 旧帧运行时间 `BE32(87)` | `k.s()` 第 247 行 | 相同 | — |
| 旧帧 4 路温度 `j(91+2i)` | `k.s()` 第 232–234 行 | 相同；本项目第 5/6 槽取 `99/101`，正是参照的 `temps.mosfet` / `temps.balancer` | 数值正确，槽位命名不同 |
| 旧帧电流符号 | `k.s()` 第 238 行：`l(bArr,70) / (-10.0d)` —— **取反** | **差异，按产品要求保留（不是缺陷）** | 参照显示 `+10.7 A`，本项目按原始符号显示 `-10.7 A`；用户明确要求保留该方向（充电为负更直观），**禁止按参照“修正”** |
| 帧头扫描 / 半包 | `k.u()`：缓冲 512、找 `7E A1` 或 `AA 55 AA FF`、不足长则保留 | 相同（本项目按 140 定长保留） | — |
| 新帧扩展块（`0xFF`/`0xFE` 子块 + 子 CRC、`problemCode`） | `k.u()` 第 470–520 行 | 差异：本项目直接要求尾部 `AA 55` 且 `protocol_len==len` | 带扩展块的新帧会被整帧丢弃（容错能力弱，不产生错误数值） |
| 新帧单芯电压数组（`& 0x1FFF`）与均衡位掩码 `i12+36` | `k.t()` 第 300 行（`& 8191`）、第 318 行 `D=B(i12+36)` | 差异：本项目不解码，改为直接取帧内 `max/min/delta/avg`（`i12+40/44/48/52`） | 参照用单芯数组自算，帧内聚合字段未被参照使用；数值一致性无法证伪，列为存疑 |
| 设备信息帧 | `k.t()` / `k.r()`（`type==18`） | 相同（本项目只置 `device_info` 标志） | — |

### 已修
* 无。

### 有意保留（不要改）
* `esp_bms_ant_protocol.c` `decode_old_status()`：旧帧电流**保持原始符号**（`current_raw`），不采用参照的取反。
  产品反馈「按原始符号显示（充电为负）用户更容易理解」，selftest 里已加注释与 `-107` 断言锁定该约定；
  交叉验证时若再次发现与 `k.s()` 不一致，按本条判定为**预期差异**。

---

## 2. JK（`jk_ble_jk02_24s` / `jk_ble_jk02_32s` / `jk_ble_jk04`）

### 结论

| 项目 | 参照依据 | 结论 | 影响 |
| --- | --- | --- | --- |
| 轮询帧 `AA 55 90 EB 96/97 00…` + 前 19 字节和 | `q0.d(int)`、`q0.h()` | 相同 | — |
| 帧头 `55 AA EB 90`、300 字节、`sum(0..298)==frame[299]` | `q0` 帧拆分（`j(bArr,{85,-86,-21,-112})`、`h(bArrI,299)`） | 相同 | — |
| 电芯 `LE16(6+2i)/1000 V` | `q0.r()` 第 664 行 `D(bArr,i*2+6)*0.001` | 相同 | — |
| 包电压 `LE32(118+dynamic)/1000 V` | `q0.r()` 第 684 行 | 相同 | — |
| 电流 `signed LE32(126+dynamic)/1000 A` | `q0.r()` 第 685–686 行 `B()`+`*0.001` | 相同（本项目 `/100` 得 0.1A） | — |
| SOC `frame[141+dynamic]`、剩余/总容量 `LE32(142/146+dynamic)` | `q0.r()` 第 706–708 行 | 相同（参照 `*0.001 Ah` = 原始 mAh） | — |
| **循环容量 `LE32(154+dynamic)`** | `q0.r()` 第 710 行：`F(...)*0.001d`（Ah）→ mAh 即原值 | **本项目错误（已修）** | 旧实现 `×100`，同一帧放大 100 倍（1234 mAh 被报成 123.4 Ah） |
| 保护字 `LE32(166)`（32S）/ `LE16(136)`（24S） | `q0.r()` 第 702 行 `z8 ? F(i11+134) : D(i11+136)`（`i11=32`） | 相同 | — |
| 均衡判据 `frame[140+dynamic]` | `q0.r()` 第 703–705 行：`K!=0` 或 `bArr[i11+169]!=0` | 差异：本项目只看 140 | 可能漏报均衡（参照多一个 `169` 字节判据），语义未明，未改 |
| 温度（24S）`130/132/134` | `q0.r()` 第 691–699 行：温度=`130,132`；`134` 是 `temps.mosfet` | 数值均为温度，槽位含义不同 | 显示顺序/标签差异，未改 |
| 温度（32S）本项目取 `144/162/164` | 参照温度=`162,164,258,256,254`；`144`(=i11+112) 是 `temps.mosfet` | 差异：本项目取 1 路 MOS + 2 路温度，且顺序与 24S 不一致 | 未改（语义存疑，见存疑清单） |
| 循环次数 `LE32(150)`、运行时间 `LE32(162)`、SOH `frame[158]` | `q0.r()` 第 709/714/712 行 | 差异：本项目未解码 | 功能缺失，不产生错误数值 |
| JK04 单芯 `LE float(6+4i)` 求和得包电压 | `q0.s()`：`y(4i+6)`，判 `>0 && <10` 后求和 | 相同（本项目额外约束 1.0–6.0V，更保守） | — |
| JK04 均衡 `frame[220]`、运行时间 `LE32(286)` | `q0.s()` 第 759–766 行 | 差异：本项目未解码均衡 | `balancing_supported` 对 JK04 为 false，属功能缺失 |
| 设备信息字符串宽度 | `q0.u()`：厂商 6/16、硬件 22/8、软件 30/16、名称 46/40 | 差异：本项目软件版本取 8、名称取 16 | 超长字符串被截断，属呈现差异，未改 |

### 已修
* `esp_bms_jk_protocol.c` `decode_jk02_cell_info()`：`total_cycle_mah` 改为偏移 154 的原值（mAh），删除为 `×100`
  服务的溢出闸门。行为变化：循环容量回到真值（与原 selftest 的 123400 对应改为 1234）。

---

## 3. JBD（`jbd_ble`）

### 结论

| 项目 | 参照依据 | 结论 | 影响 |
| --- | --- | --- | --- |
| 轮询 `DD A5 03/04 00 CRC16 77`，CRC 为「0x10000−sum」 | `q0.c(int)` + `q0.m(2,2)` | 相同 | — |
| 帧 `DD / type / status / len / payload / CRC / 77`，CRC 覆盖 `[2, len+4)` | JBD 标准帧；参照 `q0.p()` 入参即 payload | 相同 | — |
| 包电压 `BE16(0)*10 mV`、电流 `signed BE16(2)/100 A` | `q0.p()` 第 581–583 行（`C`=BE16、`z`=signed BE16） | 相同 | — |
| 剩余/总容量 `BE16(4/6)*10 mAh` | `q0.p()` 第 585–586 行 `*0.01 Ah` | 相同 | — |
| 温度 `(BE16(23+2i)−2731)/10` | `q0.p()` 第 617–620 行 | 相同 | — |
| 告警/保护字 `BE16(16)`、SOC `payload[19]` | `q0.p()` 第 596/599 行 | 相同 | — |
| 均衡判据 `payload[12..15]` | `q0.p()`：`D=E(12)`（BE32），`N=D>0` | 相同（本项目按任一字节非零，等价） | — |
| 循环次数 `BE16(8)` | `q0.p()` 第 587 行 | 差异：本项目未解码 | 功能缺失 |
| `FF AA` 认证帧吞噬、错帧重同步、半包 | 参照由共享状态机处理 | 本项目自实现，逻辑自洽（CRC 正确才消费，错帧滑 1 字节） | — |

未发现确定性错误，未改代码。

---

## 4. 彦阳（`yanyang_bms_modbus`）

### 结论

| 项目 | 参照依据 | 结论 | 影响 |
| --- | --- | --- | --- |
| 轮询寄存器 `{0x3F,4} {0x01,47} {0x30,42} {0x5A,22}` | `v6/i1.enum f`：`LAYOUT_PROBE(63,4) A1(1,47) A2(48,42) A3(90,22)` | 相同 | — |
| 请求帧 `01 03 startH startL cntH cntL CRC` | `v6/i1.D()` | 相同（帧结构） | — |
| **请求 CRC 字节序** | `v6/i1.D()` 第 345–346 行：`bArrH[6]=crc&255; bArrH[7]=crc>>8`（低字节在前） | **本项目错误（已修）** | 修复前发出的 4 条轮询全是 CRC 错序，真实从站会全部丢弃 → 拿不到任何数据 |
| **响应 CRC 字节序** | `v6/i1.u()` 第 762–763 行：`[len-2]` 为低字节、`[len-1]` 为高字节 | **本项目错误（已修）** | 修复前真实响应全部被判 CRC 错，遥测永远为空 |
| CRC 算法（0xFFFF / 0xA001） | `v6/i1.k()` | 相同 | — |
| 帧长 `data[2]+5`、功能码 `0x03` | `v6/i1.s()` / `i()` | 相同 | — |
| 单体数 `frame[15]`（=payload[12]）、温度数 `frame[16]`（=payload[13]） | `v6/i1.m()`：`W(bArr,12)`、`W(bArr,13)`（已扣 3 字节头） | 相同 | — |
| 单体 `BE16(47+2i)/1000 V` | `m()`：`T(bArr, 2i+44)/1000` | 相同 | — |
| 包电压 `BE32(25)/1000 V` | `m()`：`H(bArr,22)/1000` | 相同（本项目存 mV 原值） | — |
| 电流 `signed BE16(29)/100 A` | `m()`：`v(bArr,26)/100` | 相同（本项目 `/10` 得 0.1A） | — |
| SOC `BE16(41)*0.4 %` | `m()`：`T(bArr,38)*0.4` | 相同 | — |
| 容量 `BE16(43/45)*100 mAh` | `m()`：`T(40)/10 Ah`、`T(42)/10 Ah` | 相同 | — |
| 温度 4 字节 `83/84/85/86` | `m()`：`iArr={81,80,83,82}` + 3 → `84,83,86,85` | 取值集合相同，槽位顺序不同（见存疑） | 显示顺序差异 |
| 温度值变换 `byte−40`、哨兵 `0/166`、范围 `[−60,150]` | `v6/i1.Q()` | 差异：本项目只做 `−40`，不过滤哨兵 | 可能显示 `−40℃` 的假温度，未改 |
| 仅解析 94 字节页 | 参照另有 `A2(84)/A3(44)/B1(158)/B2(160)` 布局 | 差异：本项目只实现 A1 页 | 功能缺失，不影响 A1 数值 |

### 已修
* `esp_bms_yanyang_protocol.c` `esp_bms_yanyang_poll_request()`：CRC 改为低字节在前。
* `esp_bms_yanyang_protocol.c` `decode_frame()`：响应 CRC 改为低字节在前。
  行为变化：发出的请求字节 `… 05 74` → `… 74 05`（其余三条同理）；同一响应帧现在能通过校验。
  校验样例：`01 03 00 3F 00 04` 的 CRC 为 `0x0574`，线上应为 `74 05`；参照的设备信息请求
  `00 03 00 00 00 01 85 DB` 亦是 CRC `0xDB85` 的低字节在前，可交叉印证。
  同仓库内其它协议的 CRC 也都是低字节在前（ANT 请求 `… BE 18 55` 的 CRC 为 `0x5518`、
  设备信息 `… 20 58 C4` 的 CRC 为 `0xC458`；DALY 写 `[6]=低、[7]=高`），彦阳此前是唯一的反序实现。

---

## 5. DALY（2.7.0 中**无参照**）

Motexa 2.7.0 没有 Daly 协议类（`grep -rl daly` 无命中），因此只做自洽性检查：

| 检查项 | 结论 |
| --- | --- |
| 轮询 CRC | `crc16_modbus(前 6 字节)` 写入 `[6]=低、[7]=高`，与 `feed` 的「`[len-2]` 低字节、`[len-1]` 高字节」一致；标准 Modbus 顺序，自洽 |
| 帧长 | `frame_len = data[2]+5`；`D2` 页接受 129/165，`P81` 页要求 133；与各自字段最大偏移（126 / 130）吻合 |
| 字段重叠 | `D2`：单体 3–66、温度 67–78、电压 83、电流 85、SOC 87、极值 89/91、剩余 99、均值 113、差压 115、保护 119–125，无重叠 |
| 范围钳制 | 单体数/温度数均钳到 32/6；`cells==0`、`pack_voltage==0`、无有效单体时返回 false |
| **0x81 响应识别** | **不自洽（已修）**：`poll_request` 在 `poll_index&2` 时发 `81 03 …`，但 `feed` 只认 `D2/51`，该响应永远解析不到 |
| `0x51` 分支 | 扫描接受 `0x51` 却路由到 `P81` 解码器，语义未验证；仅记录 |

另：`esp_bms_daly_poll_request()` 中 `out[2] = status ? 0x00U : 0x00U;` 是恒等三元（两分支同为 0），属代码异味，
不改变字节，未修改。

### 已修
* `esp_bms_daly_protocol.c` `esp_bms_daly_feed()`：帧头扫描加入 `0x81`。
  行为变化：`poll_index=2/3` 的响应现在能被消费并交给 `P81` 解码器（此前被当噪声丢弃，且可能拖住后续帧）。
  仅长度 133（128 字节载荷）的 `0x81` 帧能解码；`poll_index=3` 请求返回的 129 字节帧会被校验消费后丢弃，
  不会产生错误数值。

---

## 6. FarDriver（`fardriver_ble`，FFE0/FFEC）

### 结论

| 项目 | 参照依据 | 结论 | 影响 |
| --- | --- | --- | --- |
| 帧 `AA / 0x80+index / 12B data / CRC16`，16 字节 | `s6.C()`：`length==16`、`bArr[0]==0xAA`、`(bArr[1]&0xC0)==0x80`、index=`bArr[1]&127` | 相同 | — |
| CRC 算法 | `s6.z()`：初值 `0x7F3C`、poly `0xA001`；`s6.F()` 与「`[14]` 低字节、`[15]` 高字节」比较 | 相同（本项目表驱动实现返回字节交换值，与「线上低字节在前」等价；已用逐字节对照验证 `ref=0x0A09` ↔ `proj=0x090A`） | — |
| 读请求 `addr addr 0x80 CRC` | `s6` 的 5 字节读请求 | 相同（CRC 同上一行口径） | — |
| 开流/保活命令 `AA 13 EC 07 01 F1 A2 5D` / `… 5F 5F 6E 91` | 参照 `I = {AA,46,A0,A0,06,88,A0,D7}` 为另一条命令；开流命令未在本类 | 本项目自证（selftest 固定字节序列） | 无法交叉验证，按 selftest 保留 |
| 索引→寄存器表 | `s6.H`：`E2,E8,EE,00,06,0C,12, …`（55 项） | 除 **index 3** 外全部相同 | 见下行 |
| index 3 映射 | 参照 `H[3]=0x00`；本项目 `0xE4` | 差异 | 两者都不参与 `parse_device_block()`，只影响 `blocks/block_valid` 记账，无可见数值影响；未改 |
| 索引越界 | `s6.C()`：`i9 >= H.length` → `id` 错误 | 相同（本项目 `index >= sizeof(table)` 拒绝，即 ≥55） | — |
| `0xE2` 转速 `signed LE16(data+6)` | `e8`：`rotation-raw = c0(data,6)`（`c0`=signed LE16） | 相同 | — |
| `0xE2` 挡位 | `e8`：`stage-normal-raw=(data[0]>>2)&3`、`drive-state=data[0]&3` | 差异：本项目 `((data[0]>>2)&3)+1` 得 1..4 | 呈现偏移 +1，语义存疑，未改 |
| `0xE8` 电压 `LE16/10 V`、电流 `signed LE16/4 A` | `e8`：`voltage-v=c0(0)/10`、`bus-current-a=c0(4)/4` | 相同（本项目电流 ×25 得 0.01A） | — |
| `0xD6` 控制器温度 `signed LE16(data+10)` | `e8`：`controller-temperature-c = Z(c0(10), -40, 140)` | 偏移相同；本项目范围过滤为 `(-20,120)`，比参照窄 | 极端温度会被丢弃（更保守） |
| `0xF4` 电机温度 `signed LE16(data+0)` | `e8`：`motor-temperature-raw = c0(0)` | 相同 | — |
| `0xF4` 电池容量 `data[3]` | `e8`：`battery-capacity-raw = data[3]` | 差异：本项目未解码 | 功能缺失 |
| 轮胎参数 `0xD2/0xD3/0xD4` → 周长/传动比 → 车速 | 参照无该块字段；车速由 `s3.c(rpm, 标定系数)` 的 legacy 标定scale 得出 | 无参照 | 本项目方案不可交叉验证，保留 |
| 求和校验帧（bit7=0） | 参照 `s6.C()` 直接以 `id` 拒绝 bit7=0 帧 | 差异：本项目额外支持 16 位求和 | 更宽容；16 位和误判概率 1/65536，未改 |
| 一帧多帧/前缀噪声 | `s6.r()`：逐字节组帧、CRC 失败后滑动找下一个 `0xAA` | 相同（本项目 16 字节滑窗 + CRC） | — |

CRC 已逐字节验证等价，未改代码。

---

## 修复清单

| 文件 | 函数 | 行为变化 | 证据 |
| --- | --- | --- | --- |
| `components/esp_bms_bms_ble/protocols/yanyang/esp_bms_yanyang_protocol.c` | `esp_bms_yanyang_poll_request()` | 请求 CRC 由高字节在前改为低字节在前（`… 05 74` → `… 74 05`） | `v6/i1.D():345-346` |
| 同上 | `decode_frame()` | 响应 CRC 由高字节在前改为低字节在前，真实帧才能通过 | `v6/i1.u():762-763` |
| `components/esp_bms_bms_ble/protocols/jk/esp_bms_jk_protocol.c` | `decode_jk02_cell_info()` | 循环容量 `total_cycle_mah` 由 `raw×100` 改为 `raw`（去掉 100 倍放大） | `q0.java:710` |
| `components/esp_bms_bms_ble/protocols/ant/esp_bms_ant_protocol.c` | `decode_old_status()` | **未改**：旧帧电流保留原始符号（产品要求，非缺陷） | `k.java:238` 对照后有意保留差异 |
| `components/esp_bms_bms_ble/protocols/daly/esp_bms_daly_protocol.c` | `esp_bms_daly_feed()` | 帧头扫描接受 `0x81`，扩展请求的响应可被消费 | 本模块 `poll_request()` 会发 `0x81` |

对应 selftest 同步更新：
`tests/yanyang_bms_protocol_selftest.c`（CRC 字节序、期望请求字节）、
`tests/jk_bms_protocol_selftest.c`（循环容量 1234，新增「原值透传」断言）、
`tests/ant_bms_protocol_selftest.c`（旧帧电流 -107，锁定产品符号约定）、
`tests/daly_bms_protocol_selftest.c`（新增 0x81 帧解码用例）。

## 存疑清单（只记录，未改代码）

1. **ANT 新帧聚合单体电压**：本项目从 `i12+40/44/48/52` 取 max/min/delta/avg，参照从单芯数组自算，从不读这 4 个字段。
   字段含义无法用参照证伪；若设备实际填的是告警阈值而非实时极值，显示会错。建议真机比对一次。
2. **ANT 新帧扩展块**：参照支持 `0xFF/0xFE` 子块（含子 CRC 与 `problemCode`），本项目整帧拒绝。会影响个别固件的告警码，不影响电压/电流。
3. **ANT 旧帧极值字段 `116/119/121`**：selftest 帧里三者恰好等于单芯数组的 max/min/avg（3340/3338/3339），
   但参照不使用这些偏移，无法独立确认。
4. **JK 32S 温度槽位**：本项目取 `144(MOS)/162/164`，参照温度为 `162/164/258/256/254`、MOS 在 144。
   数值都是温度，但槽位顺序与 24S（`130/132/134(MOS)`）不一致，显示顺序可能不符合预期。
5. **JK 均衡判据**：参照为 `frame[140+dynamic]!=0 || frame[169+dynamic]!=0`，本项目只判前者；`169` 字节语义未明。
6. **JK 设备信息字符串宽度**：参照软件版本 16 字节、设备名 40 字节，本项目 8/16 字节。
7. **彦阳温度槽位**：参照按 `84,83,86,85` 顺序输出 NTC1..N，本项目把它们放进 `[5],[0],[1],[4]` 槽，显示顺序会被打乱。
8. **彦阳温度哨兵/范围**：参照 `Q()` 丢弃 `0/166` 且限定 `[-60,150]`，本项目只减 40。
9. **彦阳其他页**：参照有 A2/A3/B1/B2 四种布局，本项目只解 A1（94 字节页）。
10. **彦阳 `0x51`**：DALY 扫描接受 `0x51` 但按 `P81` 解析，语义未验证。
11. **DALY `0x81` 第 3 条请求**（起始 0x41、62 寄存器）的响应布局未验证；目前该帧被消费后丢弃。
12. **FarDriver index 3**：参照 `0x00`、本项目 `0xE4`；当前解析路径不使用该块。
13. **FarDriver 挡位 +1**：参照 `stage-normal-raw` 为 0..3，本项目 `+1` 成 1..4。
14. **FarDriver 轮胎/传动比 → 车速**：参照用 legacy 标定系数 `s3.c()`，本项目用轮胎周长+传动比推算，无参照可比。
15. **ANT 新帧轮询请求载荷长度**：参照 `k.f()` 为 `d(1,0,245)`（`data[5]=0xF5`），
    `esp_bms_bms_ble.c` 中 `status_request` 为 `data[5]=0xBE`。后者属 runtime 文件（本次禁止修改），仅记录。

## 自测命令与结果

```bash
cd /vol1/1000/project/esp32BmsGps
P=components/esp_bms_bms_ble/protocols
gcc -std=c11 -Wall -Wextra -I $P -I $P/ant     tests/ant_bms_protocol_selftest.c     $P/ant/esp_bms_ant_protocol.c     -o /tmp/ant_t -lm && /tmp/ant_t
gcc -std=c11 -Wall -Wextra -I $P -I $P/jk      tests/jk_bms_protocol_selftest.c      $P/jk/esp_bms_jk_protocol.c       -o /tmp/jk_t  -lm && /tmp/jk_t
gcc -std=c11 -Wall -Wextra -I $P -I $P/jbd     tests/jbd_bms_protocol_selftest.c     $P/jbd/esp_bms_jbd_protocol.c     -o /tmp/jbd_t -lm && /tmp/jbd_t
gcc -std=c11 -Wall -Wextra -I $P -I $P/daly    tests/daly_bms_protocol_selftest.c    $P/daly/esp_bms_daly_protocol.c   -o /tmp/daly_t -lm && /tmp/daly_t
gcc -std=c11 -Wall -Wextra -I $P -I $P/yanyang tests/yanyang_bms_protocol_selftest.c $P/yanyang/esp_bms_yanyang_protocol.c -o /tmp/yy_t -lm && /tmp/yy_t
gcc -std=c11 -Wall -Wextra -I components/esp_fardriver_protocol/include \
    tests/fardriver_protocol_selftest.c components/esp_fardriver_protocol/esp_fardriver_protocol.c -o /tmp/fd_t -lm && /tmp/fd_t
```

结果（2026-10-25 本机 gcc，`-Wall -Wextra` 无警告）：

```
ant: PASS
jk: PASS
jbd: PASS
daly: PASS
yanyang: PASS
fardriver: PASS
```

## 方法与限制

* GitNexus 索引与当前工作树不匹配（`impact` 报 `Repository "." not found. Available: yunPu`），改用文本检索确认调用面：
  5 个协议模块只被 `components/esp_bms_bms_ble/esp_bms_bms_ble.c` 与对应 selftest 引用，
  FarDriver 只被自身 selftest 与 runtime 引用，改动面收敛。
* 未上真机。所有结论基于「参照实现的帧级证据 + 构造帧自测」；存疑项需要抓包或实机确认。
* 未验证 ESP-IDF 目标编译（本次仅跑主机自测），未运行 `git`。
