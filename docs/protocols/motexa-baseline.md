# 控制器 / 保护板协议移植基线（参照 Motexa 2.7.0）

本文件记录本项目从 Motexa 2.7.0 Android App 移植/核对设备协议时的参照基线：
设备清单、BLE 绑定、帧格式、字段映射、置信度与复现方式。

- 参照产物：`app-2.7.0.apk`（`https://motexa.top` 稳定版，versionCode 207370252，
  sha256 `79f407726fd04eba24b3d1591f38fedc1d8b01da2c2de3194cad8353501a72c2`）。
- 反编译方式：JADX `JadxDecompiler` 批量导出到 `_ref/motexa270-src/sources`
  （脚本见文末「复现」）。类名被 R8 混淆，但常量、字符串、位运算与字段偏移完整保留。
- 本仓库实现：控制器 → `components/esp_bms_controller_protocol/`；保护板 →
  `components/esp_bms_bms_ble/protocols/`。

> 说明：Motexa 仓库只发布 APK 与仪表 SDK，不含协议源码；本基线全部来自对 APK 的
> 静态反编译与逐字节比对，**没有实车 HCI 抓包**。字段语义在表中标注置信度。

---

## 1. 控制器清单（16 个型号）

| 本项目枚举 | Motexa 协议 id | 标签 | GATT（service / notify / write） | MTU | 轮询周期 | 实现 |
| --- | --- | --- | --- | --- | --- | --- |
| `ESP_BMS_CTRL_TYPE_BLUESTONE` | `bluestone_300_ble` | 蓝石 BlueStone | `00002760-08c2-11e1-9073-0e8ac72e1001` / `…0002` / `…0001`（128 位） | 247 | 5000 ms | `protocols/bluestone` |
| `ESP_BMS_CTRL_TYPE_FARDRIVER` | `fardriver_ble` | 远驱 FarDriver | `FFE0` / `FFEC` / `FFEC` | 23 | 200 ms（保活 1 s） | `protocols/fardriver`（封装既有 `esp_fardriver_protocol`） |
| `ESP_BMS_CTRL_TYPE_YUANQU_FOC` | `yuanqu_foc_ble` | 远驱 YuanQu FOC | 全服务探测 | 23 | 200 ms | `protocols/yuanqu_foc` |
| `ESP_BMS_CTRL_TYPE_LINGBO` | `lingbo_ble` | 凌博 Lingbo | `FFF0` / `FFF1` / `FFF2` | 247 | 500 ms | `protocols/lingbo` |
| `ESP_BMS_CTRL_TYPE_DATAI` | `datai_ble` | 大泰 Datai | 全服务探测 | 247 | 200 ms | `protocols/datai` |
| `ESP_BMS_CTRL_TYPE_SUNIU` | `suniu_ble` | 速牛 Suniu | 全服务探测 | 512 | 200 ms | `protocols/suniu` |
| `ESP_BMS_CTRL_TYPE_YANXIN` | `yanxin_vitmc_ble` | 雁鑫 Yanxin VITMC | 全服务探测 | 23 | 200 ms | `protocols/yanxin` |
| `ESP_BMS_CTRL_TYPE_APT_A8` | `apt_a8_ble` | APT A8 | 全服务探测 | 23 | 200 ms | `protocols/apt_a8` |
| `ESP_BMS_CTRL_TYPE_APT_LEGACY` | `apt_legacy_ble` | APT 安培旧版 | 全服务探测 | 23 | 200 ms | `protocols/apt_legacy` |
| `ESP_BMS_CTRL_TYPE_DUNEN` | `dn_dunen_ble` | 顿恩 DN | 全服务探测 | 23 | 200 ms | `protocols/dunen` |
| `ESP_BMS_CTRL_TYPE_ZHIKE` | `zhike_ble` | 智科 GEKOO | 全服务探测 | 23 | 200 ms | `protocols/zhike` |
| `ESP_BMS_CTRL_TYPE_JH` | `jh_ble` | 晶汇 JH | 全服务探测 | 128 | 200 ms | `protocols/jh` |
| `ESP_BMS_CTRL_TYPE_AUSI` | `ausi_ble` | AUSI 玉铖 | 全服务探测 | 23 | 200 ms | `protocols/ausi` |
| `ESP_BMS_CTRL_TYPE_JIAZHOU` | `jiazhou_bridge_ble` | 甲胄动力 DS11/DS15 | 全服务探测 | 500 | 500 ms | `protocols/jiazhou` |
| `ESP_BMS_CTRL_TYPE_XINDA` | `xinda_ble` | 昕达动能 | 全服务探测 | 23 | 200 ms | `protocols/xinda` |
| `ESP_BMS_CTRL_TYPE_HOBBYWING` | `hobbywing_spd_ble` | 好盈 SPD | 全服务探测 | 247 | 200 ms | `protocols/hobbywing` |

`ESP_BMS_CTRL_TYPE_AUTO`（0）保留原有 FarDriver 自动探测链路（NUS → FFE0 → 全服务探测），
用于未指定型号的旧配置。

### 1.1 各型号帧摘要（详细偏移见各协议源文件头注释）

| 型号 | 请求帧 | 应答/通知帧 | 校验 | 主要字段 |
| --- | --- | --- | --- | --- |
| 蓝石 | `55 70 06 00 00 00 08 40 01 …`（读实时页 0x40）；`0x42`/`0x15 0x19` 开录像机 | `55 E0 02 …`（56B）/`55 70 02 …`（16B） | SUM8 | float32：rpm@11、V@15、I@19、相电流@23、温度@31、转把@39 |
| 远驱 FarDriver | `AA 13 EC 07 01 5F`、`AA 17 E8 F1 13 51`、读包 `AA <addr> …`、保活 | `AA …` 16B | CRC16 表 或 16 位求和（`frame[1].bit7` 区分） | 0xE2 rpm、0xE8 电压/电流、0xD2..D4 轮胎/齿比 |
| 远驱 FOC | `AA 17 E8 F1 13 51 FE 01` 发现；`AA 13 EC 07 5F 5F 6E 91` 保活 | `AA <page> 12B … SUM16` | SUM16 大端 | page0 rpm/档位/故障，page1 电压/电流，page4 MOS 温度，page0x0D 转把 |
| 凌博 | `AA 55 02 20 21` 标定、`AA 55 02 22 23` 实时 | `AA 55 <len> 60/62 …` | SUM8 | 标定分母 @8/@10；实时 rpm@4、电压@10、电流@12、温度@14/16、故障@18、转把@22、档位@27 |
| 大泰 | `05 01 01 00 00 00 07`（RTD 块） | `01 05 02 <type> <sub> <len> payload sum8` | SUM8 | speed@0、V@2、I@4、电机温@8、控制器温@10、故障@12、rpm@24、转把@26 |
| 速牛 | `28 01 D0 D1 29`、`28 01 E0 E1 29`、`28 02 D3 F3 22 29` | `28 <len> <cmd> … xor 29` | XOR + 尾 `0x29` | F3：V@4、I@6、相电流@8、温度@10/12、rpm@14、转把@16；F1 档位@5；0x94 轮胎周长/齿比 |
| 雁鑫 | `AA 0C 00 00 01 <scheme> <pw0..3> <cust> CRC` 握手；`AA 0C 00 00 01 01 …` 轮询 | `AA <len> <seq> <flags> … CRC` | CRC16/Modbus 低字节在前 | 故障@4、V@8、I@10、rpm@12、温度@18/20、转把@22、chip_io bit22 门控电流 |
| APT A8 | `A8 02 01 24 01 1F 3E 4F` | `A8 <dir> <marker> <cmd> <sub> <len> … sumLo sumHi` | 双 256 字节映射表（已内置） | V@0、I@2、rpm@4、温度@6/7、档位@12 高/低半字节、故障@15 |
| APT 旧版 | `FD 48 55`（回退 `FD 48 60`） | 32B 定长推送，首字节白名单 `DF/FB/F0/F2/D2/F6/FE/D5/D6/D7` | 无 | V@2、I@4、rpm@6、控制器温@8、电机温@12（实验）、故障@19、档位@30（实验） |
| 顿恩 | Modbus `01 03 03 FE 00 1C CRC`（实时）；身份 `FFEC/FFEE/FFED` | `01 03 <cnt> … CRC` | CRC16/Modbus | 组0 转速、组2 电压、组3 油门、组4/5 温度、组9 母线电流、组13 故障 |
| 智科 | `01 02` 开启；`AA 13 FF 01 AA 13 00 01` 轮询 | 67B `AA 13 00 …` | 32 个 LE16 字求和 == 0xFFFF | w2 转把、w6 rpm/5.46、w8 电压、w9 电流、w18 温度、w21 方向/档位、w22 故障 |
| 晶汇 | JN 标识帧 `4A 4E 27 03 22 F1 <id> … 4B 4A`；Modbus `04 03 A1 00 00 07 CRC` | `04 03 …` | CRC16/Modbus | 故障@+0、rpm@+2、V@+4、I@+6、相电流@+8、MOS 温@+10、电机温@+12 |
| AUSI | 握手 `AA 0C 00 31 01 1F 01 41 55 02 00 07 83 38`；心跳 `AA 0C 00 31 01 01 …` | `AA <len> <seq> <flags> … CRC` | CRC16/Modbus | 故障@+4、V@+8、I@+10、rpm@+12、温度@+18/+20 |
| 甲胄 | `60 A2 …` 发现 → `60 A0 …` 设备信息 → `60 A3 …` DCF → `60 A4 00 20 …` Monitor 读 | `60 <cmd> <len> <MessagePack>` | 无（仅长度） | Monitor 8 项：V、I、rpm、电机温、相电流、MOS 温、故障、档位 |
| 昕达 | `96`（单字节请求） | 25B `EC 9F <故障> <MOS+40> V I rpm … XOR` | XOR 前 24B | 故障@2、MOS 温@3、V@4、I@6、rpm@8（/极对数）、电机温@15 |
| 好盈 SPD | `0A 55 01 01 02 <fn> <cnt> CRC` | `0A 55 10 <b3> <b4> <fn> … 2B 尾`（无 CRC） | 请求 CRC16/Modbus；应答仅结构校验 | fn1：rpm/电压/电流/相电流/MOS 温；fn7：电机温/故障；fn12：档位/油门 |

### 1.2 传输层约定（`esp_bms_controller_ble`）

- `controller_type != AUTO` 时，连接后按注册表 UUID 直接发现服务（`CONTROLLER_PROFILE_CUSTOM`）；
  注册表 UUID 为空的型号走全服务探测（`CONTROLLER_PROFILE_DISCOVERED`）。
- 订阅完成后进入 `build_start(attempt)` 循环：重发周期 = `start_retry_ms`。`attempt` 每拍 +1，
  协议模块用它轮换固件变体或推进握手阶段（甲胄/雁鑫/速联链）。
- 收到第一帧有效遥测后切 `ONLINE`，改为按 `poll_period_ms` 调用 `build_poll(step, elapsed_ms)`。
  `elapsed_ms` 语义 = **距上一帧有效数据的毫秒数**（蓝石据此补发录像机请求）。
- `build_poll` 返回 `false` 表示“本拍不发”，不是错误。
- `feed()` 返回 `true` 表示消费了一个帧级合法的包（可能只是握手/短应答），只有
  `telemetry()` 里有有效位时才认为链路在线。
- 控制器原生车速优先：`controller_apply_engine_telemetry()` 只有在引擎没有给出速度时才用
  本地轮径/齿比回算（避免覆盖大泰等型号的原生车速）。

---

## 2. 保护板清单（本项目 8 种）

| 本项目枚举 | Motexa 协议 id | 标签 | BLE | 模块 |
| --- | --- | --- | --- | --- |
| `ESP_BMS_IDF_BMS_TYPE_ANT` | `ant_ble_new` / `ant_ble_legacy` | 蚂蚁 ANT | `FFE0`/`FFE1` | `protocols/ant` |
| `ESP_BMS_IDF_BMS_TYPE_JK` | `jk_ble`（jk04 / jk02_24s / jk02_32s） | 极空 JK | `FFE0`/`FFE1` | `protocols/jk` |
| `ESP_BMS_IDF_BMS_TYPE_JBD` | `jbd_ble` | 嘉佰达 JBD | `FF00` | `protocols/jbd` |
| `ESP_BMS_IDF_BMS_TYPE_DALY` | 2.7.0 **无** | 达锂 Daly | `FFF0` | `protocols/daly`（自洽校验） |
| `ESP_BMS_IDF_BMS_TYPE_YANYANG` | `yanyang_bms_modbus` | 彦阳 BMS | 128 位自定义 | `protocols/yanyang` |
| `ESP_BMS_IDF_BMS_TYPE_BLUEBABE` | `bluebabe_meter` | 蓝宝电量模块 | `FFE0`/`FFE1`/`FFE2` | `protocols/bluebabe` |
| `ESP_BMS_IDF_BMS_TYPE_MINGTANG` | `mingtang_bms` | 明唐保护板 | `FA00`，特征按属性发现 | `protocols/mingtang` |
| `ESP_BMS_IDF_BMS_TYPE_EM02` | `luxing-em02` | 鹿行 EM02 | `FFE0`/`FFE1`/`FFE2` | `protocols/em02` |

**鹿行 EM02（仅只读）**

- GATT 服务 `FFE0`，通知 `FFE1`，带响应写入 `FFE2`。
- 轮询请求固定为 `01 03 0F A0 00 59 86 C6`、`01 03 0F FC 00 36 06 F8`、
  `01 03 10 68 00 2D 00 CB`、`01 03 10 38 00 30 C0 D3`。
- 响应使用 Modbus RTU CRC16，`frame_len = byte_count + 5`；byte-count 为
  `178/108/90/96`，对应完整帧 `183/113/95/101`。身份响应偏移 67 的型号字段必须包含
  `EM02` 或 `LX-EM02`，否则不发布遥测。实时页按源码取总压、电流、单体和 SOC；额定容量在
  `105`，剩余容量按额定容量×SOC 计算；MOS/BAL 标志和值在 `5..8`，计数温度从 `11` 开始。
  字段偏移来自 Motexa 2.7.0 静态源码，EM02 实体设备仍待真机验证。

### 2.1 新移植的保护板

**蓝宝电量模块（bluebabe）**

- 请求 19B：`FA 50 28 A0 15 | 00 09 | code[6] | table | 00 | len | CRC_HI CRC_LO | E5`，
  表 1 = 状态（payload 111B），表 2 = 容量/版本（134B）。
- 校验 CRC16 反射（poly `0xC328`，seed `0xFFFF`）。
- 需要设备六位配对码；本项目存在 NVS `bb_code`（默认空），
  通过 Web 配置 `bluebabe_code` 下发，未配置时轮询直接返回失败并保持“未连接数据”。
- 字段：总压 ×0.01V、电流 ×0.01A（二补码，推断）、SOC ×0.1%、剩余容量 ×1e-5Ah、
  最高/最低/压差单体、单路温度、额定容量（表 2）。

**明唐保护板（mingtang）**

- 命令为 ASCII：`stateread` / `settingread` / `readhis` / `readevent`（写特征，带响应）。
- 应答 `E1` 帧：`E1 11 00 xx | len(BE16) | TLV… | 尾字节(未校验)`，TLV = `tag(BE16) + len + value`，
  必须严格铺满 `[6, len)`；`E2` 后跟 `[END]`，`F1..F3` 历史/事件整块跳过。
- 已实现实时子集：tag10 总压、13/15 充/放电流（充电为正）、38 SOC、36/37 容量、
  4/5/6/7 单体、27..31 温度、17/19/21 保护、24/25 均衡、170 告警位图。
- 未实现：设置项读写（`settingset`）、历史与事件解析、保护计数、身份串。

---

## 3. 交叉验证（既有协议）

完整报告：[`cross-verification.md`](cross-verification.md)。已修复的确定性错误：

| 协议 | 问题 | 处理 |
| --- | --- | --- |
| 彦阳 | 请求/响应 CRC16 字节序反了（参照 `v6/i1.D()`、`u()` 低字节在前） | 改为低字节在前；修复前真实设备所有响应都会被拒 |
| JK | 循环容量多乘 100（参照 `q0.r()` 原始值即 mAh） | 去掉 ×100 |
| ANT | 旧协议电流方向与参照相反（`k.s()` 取反） | **有意保留原始符号**（产品要求：充电为负更直观），未按参照修改 |
| DALY | 请求会发 `0x81` 帧，但解析只认 `0xD2/0x51` | 帧头加入 `0x81` |

存疑项（未改代码，见报告）：ANT 新帧扩展块与单芯数组、JK 32S 温度槽位与均衡判据、
彦阳温度槽位顺序、DALY `0x51` 语义、FarDriver 挡位 +1 与轮胎公式。

---

## 4. 配置与界面

- 控制器型号：LVGL 设置 →「控制器」→「控制器型号」（17 项：自动识别 + 16 型号），
  Web 设置 →「设备」→「控制器型号」；持久化 NVS `ctl_type`，快照字段
  `controller_type`，JSON `controller_type`。
- 保护板类型：支持「蓝宝电量模块」「明唐保护板」「鹿行 EM02」（LVGL + Web + 清单接口）。
- 蓝宝配对码：NVS `bb_code`，Web 配置项 `bluebabe_code`（6 位数字）。
- 切换控制器型号会断开当前连接并按新型号重连已绑定设备（`runtime_select_controller_type`）。

---

## 5. 复现（重新导出参照源码）

```bash
# 1) 下载稳定版 APK（sha256 见文件头）
curl -L -o app-2.7.0.apk \
  https://download.motexa.top/downloads/stable/com.motexaqixing.app/207370252/app-stable-release-207370252.apk

# 2) 用 JADX API 批量导出（JDK 21 + jadx-headless-mcp jar）
cat > /tmp/DumpApk.java <<'JAVA'
import jadx.api.*;
import java.io.File;
public class DumpApk {
  public static void main(String[] a) throws Exception {
    JadxArgs args = new JadxArgs();
    args.setInputFile(new File(a[0]));
    args.setOutDir(new File(a[1]));
    args.setSkipResources(true);
    args.setShowInconsistentCode(true);
    JadxDecompiler d = new JadxDecompiler(args);
    d.load();
    for (JavaClass c : d.getClasses()) { try { c.decompile(); } catch (Throwable ignored) {} }
    d.save();
  }
}
JAVA
javac -cp /vol1/rea-toolchain/jadx/jadx-headless-mcp-0.7.1-all.jar -d /tmp /tmp/DumpApk.java
java -cp /tmp:/vol1/rea-toolchain/jadx/jadx-headless-mcp-0.7.1-all.jar DumpApk app-2.7.0.apk _ref/motexa270-src

# 3) 设备注册表入口
#    控制器：sources/com/motexaqixing/app/g3.java
#    保护板：sources/c7/n.java
```

也可以直接使用本地已导出的副本：`/vol1/1000/project/_ref/motexa270-src/sources`。
