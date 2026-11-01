# 全范围检查报告

审查范围：本任务的 BLE 连接恢复、GATT 属性/描述符选择、ANT 新协议流解析与只读诊断、partial 遥测字段合并、EM02 小端/响应分类、ANT 通道配置与 Web 设置。保留任务前已存在的无关脏文件。

## Findings (fixed)

- 本检查代理未发现需要另行修复的确定性代码缺陷，未改业务代码。
- 主会话与 projection 代理已处理 snapshot ABI 静态断言及真实头文件回归测试；检查代理未重复修改。

## Findings (not fixed)

1. **现有 BLE API 白名单测试与 HEAD 行为不符，属于任务前问题。**
   - 文件：`tests/test_ble_device_api_contract.py:34`。
   - 测试 `test_ble_dispatch_whitelists_only_profile_config_and_status` 禁止响应分支出现 `/api/history`；当前 HEAD `22cd1e336041fcee356b67d8406824e6e8eb8ef5` 的 runtime 源码 4711/4713/4715 行已经包含 history sessions/samples/faults 分支。
   - 已用 `git show HEAD:components/esp_bms_idf_runtime/esp_bms_idf_runtime.c` 写入临时文件，替换该测试模块的 RUNTIME 输入后运行同一测试，复现 1 failure、0 errors；工作区运行该文件 3 项中同样仅此项失败。
   - 本任务未改变 BLE API 白名单。调整历史接口约定/过期测试属于另外的接口决策，不在本次检查中更改。

2. **现有 Android 扫描源码断言在 HEAD 已失败，属于任务前问题。**
   - 文件：`tests/test_ble_scan_source_contract.py:96`。
   - 测试 `test_android_bms_and_controller_scans_use_phone_ble` 要求 `MainActivity.kt` 直接包含 `requestBleScan(BleScanTarget.CONTROLLER)`，而 HEAD 该文件已经没有这段字面量。准确说这是源码位置断言失败，并非本任务新增中文/英文文案问题。
   - 已用 `git show HEAD:android-cast/app/src/main/java/com/fuckingbms/cast/MainActivity.kt` 构建临时 ROOT，并运行同一测试，复现 1 failure、0 errors；HEAD 文件与工作区文件逐字节相同。工作区该测试文件 7 项中仅此项失败。
   - 本任务未修改 Android 扫描页面；没有为了满足旧字符串断言改变产品行为。

3. **EM02 transport 门控与 drain 直接行为回归已补齐。**
   - 已走读 `bms_frame_push`：page0 独立更新身份；未知型号清遥测；只有身份已确认的 page1 调用遥测投影；以 NULL/0 排空剩余响应，辅助页不刷新心跳。未发现实际错误。
   - 后续 projection 代理在真实 bms_frame_push + EM02 parser 联测中补齐：未确认型号不投影/不续心跳；identity+realtime 同通知 drain；错误型号清空且拒绝后续实时页；两种辅助页不刷新心跳。最终完整 host suite 通过。

4. **真机验证待恢复硬件连接。**
   - 固件构建与刷机由主会话负责，检查代理未另起构建/刷机。主会话报告固件构建成功，RFC2217 端点 TCP 超时，未建立会话；因此不能把本报告的主机验证当作真机连接稳定性或协议实测成功。

## Verification

- **Lint：通过（本任务检查范围）。** 对任务涉及的 components、Web、测试与 host runner 执行 `git diff --check` 无错误；提取 Web 内联脚本执行 `node --check` 通过。C 回归编译均带 `-Wall -Wextra -Werror`。
- **TypeCheck：通过（主机实际 C 代码编译）。** 三个新 BMS 行为测试提取实际函数并严格编译；五个受影响协议模块直接编译通过。完整 ESP-IDF 编译由主会话验证，检查代理未重复执行。
- **Tests：本任务回归通过；额外已有契约测试有上述两项历史失败。**
  - `python3 -m unittest discover -s tests -p 'test_bms_*.py' -v`：3/3 通过，覆盖 BLE 异步失败、配置读写队列/NVS、字段合并与诊断 JSON 极值。
  - ANT、EM02、Bluebabe、Mingtang、JK 五套 C selftest：逐项以 `-std=c11 -Wall -Wextra -Werror -fsanitize=undefined,address -fno-omit-frame-pointer` 编译并执行，全部通过。
  - `test_ble_device_api_contract.py`：2/3 通过，历史失败已对 HEAD 重现。
  - `test_ble_scan_source_contract.py`：6/7 通过，历史失败已对 HEAD 重现。

## 跨层与边界结论

- BLE 建立阶段有有限 deadline；CCCD 失败进入终止/重连路径；token、phase、conn handle 共同拒绝过期 GATT 回调；stop/reset 失效旧 token，终止失败保留连接句柄供 tick 重试。
- 写入选择遵循实际属性，EM02 继续要求 acknowledged WRITE；CCCD 搜索止于下一 characteristic declaration 之前；ANT A/B 仅由用户显式选择，属性不符时失败，不隐式抢占外设通道。
- ANT 长度解析逐段验证 CRC，主 payload 边界不会包含 CRC/trailer；payload 内 `AA55` 不作为帧尾；扩展逐字段 valid；旧协议独立保留。保护 bit1..16 与 maibms BitDict 对应，均衡状态3不作为活动均衡。
- partial 所有生产者（Bluebabe、JK04、Mingtang）均设置 presence mask；显式零值更新；缺失字段保留；身份容量表不建立在线遥测或续心跳。
- EM02 字节序与 Motexa w0.e/f 及 maibms 一致；响应由 byte count 分类而非发送索引，CRC 与 EM02 型号门控保留。
- 状态 JSON 测试读取实际 `HTTP_JSON_MAX_LEN=2048`，含32串和极值时通过，BLE 4096字节响应包装容量足够。对诊断扩展缺失输出 null。
- snapshot 使用者是同源构建的 runtime/UI/display-service/simulator；display queue、缓冲区通过 `sizeof(snapshot_item_t)` 分配，simulator BLE bridge接收文本并逐字段投影。未找到固定大小外部二进制 snapshot 消费者或 raw snapshot 网络/磁盘序列化。
- 当前验证固件 ATT preferred MTU 为128，BMS receive buffer为320；本报告不宣称支持大于320字节的单条通知或无限长单个 compound frame。纯 parser 的大于320字节连续多帧回归只证明流消费能力。
- 新跨层契约（presence mask、ANT diagnostics、session/deadline、显式 ANT 通道）应由主会话同步到 `.trellis/spec/`；图变化检查由主会话统一完成。
