# 协议与配置调查

## EM02 参考证据

- 本地 Motexa 2.7.0 反编译代码：`/vol1/1000/project/_ref/motexa270-src/sources/v6/l0.java`、`com/motexaqixing/app/c9.java`。
- GATT：主服务 `FFE0`，通知 `FFE1`，写 `FFE2`，写入要求 acknowledged write。
- 四个 Modbus 03 请求依次为：
  - `01 03 0F A0 00 59 86 C6`
  - `01 03 0F FC 00 36 06 F8`
  - `01 03 10 68 00 2D 00 CB`
  - `01 03 10 38 00 30 C0 D3`
- 响应地址/功能码为 `01 03`，字节数位于 `frame[2]`，完整帧长为 `frame[2] + 5`。源码的 `{178,108,90,96}` 是期望 byte-count，不是完整帧长；对应完整长度分别为 183、113、95、101 字节。CRC16/Modbus 在线上低字节先行。
- 身份响应从 Modbus payload 的偏移 67 处读取 26 字节型号字段；只有包含 `EM02` 或 `LX-EM02` 才确认设备。运行、绑定状态、循环状态偏移来自 `l0.java` 的 `D/E/B/C` 解码函数。
- 官方参考只声明只读请求；EM02 实体设备在 Motexa 源码中仍标记待验证。本任务应按只读做实现，真机结果独立报告。

## 已有配置和 BLE 接入证据

- BMS 枚举在 `components/esp_bms_idf_runtime/include/esp_bms_idf_runtime.h`，现有持久化值为 ANT=0、JK=1、JBD=2、Daly=3、彦阳=4、蓝宝=5、明唐=6；新类型只能追加。
- BMS 协议接口在 `components/esp_bms_bms_ble/protocols/esp_bms_bms_protocol.h`；通知处理上限为运行时 320 字节。组件调度/轮询位于 `components/esp_bms_bms_ble/esp_bms_bms_ble.c`。
- 蓝宝运行时六位码已从 NVS 加载并在通用配置持久化函数中写回；但 Web `/api/config` POST 解析不接受该字段，GET `/api/config` 当前把 `bluebabe_code` 明文返回。Web 页面也没有输入控件。
- Web 型号 select 和运行时设置清单都列出蓝宝但没有 EM02。LVGL 的 `SETTINGS_BMS_TYPE_LABELS`、`..._ACTIONS` 与选项绘制/回调也包含可点的蓝宝选项。
- 控制器 BLE 层通知本地数组固定 128 字节；BMS 通知接收上限 320 字节。控制器协议接口 `ESP_BMS_CTRL_FRAME_MAX_LEN` 为 128；甲胄 MessagePack envelope 可接受 192 字节 payload，完整帧最大 196 字节，而接收层在解析前拒绝 129+ 字节通知。

## 现有协议复核范围和证据等级

- BMS 现有实现包含 ANT、JK、JBD、Daly、彦阳、蓝宝、明唐；控制器注册表包含 16 种协议，见 `docs/protocols/motexa-baseline.md`。
- `docs/protocols/cross-verification.md` 当前按帧级表格覆盖 ANT、JK、JBD、彦阳、Daly（Motexa 参考中没有该协议）和 FarDriver。其余控制器虽有基线和各自 selftest，但不能仅凭 selftest 视为对照完成。
- 自测入口：`scripts/run-host-selftests.sh`。当前已通过，验证的是构造帧/主机端协议逻辑，不覆盖 BLE 控制器连接、真实 MTU、通知切片行为或 EM02 真机行为。
- 已知待核实差异保留在交叉验证报告的存疑清单。只有可复现且有可信参考数据的确定性错误才纳入代码修复；其它差异应记录为待设备验证。
