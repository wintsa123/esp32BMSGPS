# Research: 投屏画面冻结与触摸失效

- Query: 调查全帧投屏后“画面卡在最后一帧、触摸无响应”的原因，并判断晶振或震动是否可能是主因。
- Scope: mixed（固件、Android 发送端、串口证据与硬件配置）
- Date: 2026-08-16

## Findings

### 结论

当前证据最支持“投屏会话仍被软件视为活动状态”，而不是晶振故障：投屏进入时固件会有意禁用 LVGL 触摸输入；当最近一帧已经收到 ACK、Android 后续不再产生新画面时，发送线程会每 2 秒继续发送心跳；固件收到完整 binary payload 后会清零心跳超时。因此 Android 捕获停滞时，ESP32 可以无限期保留最后一帧并继续禁用触摸，看起来正好是“画面冻结 + 触摸死亡”。若发送线程正阻塞等待某帧 ACK，则不会进入心跳分支，不属于这条路径。这是由代码直接支持的最强工作假设，但现有日志没有捕获到该冻结现场，尚不能作为已复现的根因。

次要的软件故障路径是全帧提交永久等待 LCD DMA 完成通知。每个 40 行条带都同步等待，适配器使用 `portMAX_DELAY`；若 I80 完成通知丢失，显示任务会卡在提交中，后续 `EXIT_CAST` 最多等待 1 秒后失败，触摸也不会走到重新启用步骤。现有日志没有出现这条路径的直接证据。

另有一个较窄的触摸状态问题：进入/退出投屏会 `lv_indev_reset()`，但不会清空桥接层的 `s_touch_filter`。GT1151 ready bit 清零时，回调可直接复用过滤器中的最后有效按点并提前返回。若投屏恰好在手指按下时开始，旧的 pressed/accepted point 可能跨越禁用与重新启用边界。该路径更可能造成恢复后的假按下或触摸卡住，不足以单独解释投屏画面也停止更新。

晶振或震动不是当前首要怀疑对象。目标配置明确使用 40 MHz XTAL 并启用 brownout detector。晶振、电源或 MCU 时钟异常通常会同时影响 CPU、Wi-Fi、日志和多个任务，或产生复位/异常头，而不是稳定地只留下投屏最后一帧并让触摸消失。若震动与问题有稳定相关性，应优先检查 GT1151 排线/连接器、GPIO42 中断、GPIO40/41 I2C、地线和触摸供电。当前 profile 没有 GT1151 reset GPIO，控制器真的挂死后没有运行时硬复位恢复能力。

### 已确认的代码路径

1. `esp_bms_lvgl_bridge_enter_cast()` 在进入投屏时先重置 LVGL indev，再明确禁用它；只有 `esp_bms_lvgl_bridge_exit_cast()` 成功执行后才重新启用触摸（`components/esp_bms_lvgl_bridge/esp_bms_lvgl_bridge.c:1125`, `:1140`, `:1161`, `:1169`）。
2. 显示服务进入投屏后设置 `cast_active=true`；退出需要依次恢复 rotation/UI、退出 bridge cast、恢复 adapter，最后才清除 display-service cast 状态（`components/esp_bms_display_service/esp_bms_display_service.c:314`, `:350`, `:384`）。
3. 主循环只有在 runtime `cast_active` 变为 false 后才提交 `EXIT_CAST`；提交超时或恢复失败时会继续保持 `cast_display_active` 并重试，期间模块和触摸均未完成恢复（`main/idf_main.c:420`, `:431`, `:435`）。
4. Android 在没有新 frame 时仍每 2 秒发送 heartbeat（`android-cast/app/src/main/java/com/fuckingbms/cast/CastService.kt:214`, `:224`, `:234`）。
5. 固件读取每个 payload 后先把 `cast_heartbeat_elapsed_ms` 清零，之后才判断它是不是 heartbeat；runtime tick 只有计时达到阈值才停止投屏（`components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:3920`, `:3927`, `:5264`, `:5270`）。这使“连接活着”和“画面仍在推进”使用了同一个存活条件。
6. JPEG 完整帧按条带调用 `esp_lv_adapter_dummy_draw_blit(..., true)`（`components/esp_bms_lvgl_bridge/esp_bms_lvgl_bridge.c:1906`, `:1925`）；适配器对待完成通知使用 `xTaskNotifyWait(..., portMAX_DELAY)`（`managed_components/espressif__esp_lvgl_adapter/src/display/bridge/v9/lvgl_bridge_v9.c:1225`, `:1243`）。
7. bridge 的触摸过滤器有独立的 `touch_filter_reset()`，但 cast enter/exit 没有调用它（`components/esp_bms_lvgl_bridge/esp_bms_lvgl_bridge.c:640`, `:1125`, `:1161`）。GT1151 ready bit 未置位且旧点有效时，回调直接返回旧点（同文件 `:1234`）；正常的无触点路径才会在 `:1290` 清过滤器。
8. 当前 profile 使用 GPIO42 作为 GT1151 IRQ、GPIO40/41 作为 I2C，并把 `pin_touch_reset` 生成为 `GPIO_NUM_NC`（`firmware-builds/cast-s3-build/firmware.env:36`, `firmware-builds/cast-s3-build/generated/esp_bms_profile_hardware.h:47`）。
9. 当前 build 配置是 40 MHz XTAL，brownout detector 已启用（`firmware-builds/cast-s3-build/sdkconfig:1931`, `:1944`）。

### 串口证据

`logs/serial-2026-08-16_00-16-54-cast-fix.log` 记录的是一次可正常恢复的会话，而不是所报告的持续冻结：

- GT1151 初始化和 LVGL touch 注册成功（`:98`-`:105`）。
- 投屏前触摸采样和 release 正常（`:115`-`:128`）。
- 投屏连接、模块暂停和触摸 rotation 发生（`:179`-`:182`）。
- 会话最终因 heartbeat timeout 停止并恢复 rotation（`:185`-`:186`）。
- UI 与模块恢复完成（`:187`-`:194`）。
- 恢复后立即又有连续触摸采样和 release（`:195`-`:219`）。
- 文件中没有 panic、watchdog、brownout、内存分配失败或意外重启证据。

其余 `logs/serial-2026-08-16*` 文件只有 111 行并在启动后结束，没有包含投屏会话，不能用于判断冻结现场。

`logs/serial-2026-08-15_23-28-25-reset.log` 提供了另一类明确的软件故障证据：投屏连接与 rotation 后立刻出现 `LoadProhibited`，`EXCVADDR=0x00000004`，随后以 `RTC_SW_CPU_RST` 重启（`:198`-`:224`）。这符合空指针附近访问，不符合“晶振让触摸单独失效”。该日志的 ELF SHA 为 `37f6a5b9a`，仓库中没有匹配 ELF，不能可靠地把 PC/backtrace 指到具体符号。

### 最小诊断流程

1. 从开始投屏前持续采集串口，覆盖冻结现场、主动停止投屏及停止后至少 10 秒；不要只保留启动日志。
2. 冻结时观察 GPS summary 等周期日志是否继续，并访问 `/api/cast/info`。若二者正常，CPU、主循环、Wi-Fi 和 HTTP 仍存活，晶振/整机掉电概率很低。
3. 记录冻结期间 Android 是否仍显示 streaming、是否继续发 heartbeat，以及 JPEG sequence/ACK 是否继续推进。只有 heartbeat、没有新 sequence/ACK，直接支持“capture 停滞但 cast 会话存活”。
4. 停止投屏后查找 `[cast] stopped`、`restore cast display failed`、display command timeout/error 和 touch rotation 日志。
5. 停止后立即点击屏幕。出现 `touch sample accepted` 说明 I2C/GT1151 正常，问题是 cast/input-disabled 状态；没有触摸读日志说明 LVGL indev/adapter 未恢复；持续出现 `touch read failed` 才把优先级转向 I2C、IRQ、供电或排线。
6. 若串口在冻结处完全停止，继续区分 panic/WDT/brownout/reset header 与单任务阻塞；有 reset header 再检查电源和晶振，无 reset 且 HTTP/GPS 停止则需要任务栈和调度层证据。

| 现场证据 | 优先判断 |
| --- | --- |
| GPS/API 正常，画面 sequence 不增长，heartbeat 继续 | Android capture 停滞，软件仍保持 cast active |
| GPS/API 正常，停止后出现 `restore cast display failed` | 显示命令/退出链路阻塞 |
| 停止后恢复 touch sample | GT1151 硬件正常，投屏期间禁用是直接原因 |
| 停止后持续 `touch read failed` | GT1151/I2C/IRQ/供电/连接器 |
| panic 或 `RTC_SW_CPU_RST` | 软件异常；保存 ELF 后符号化 |
| brownout/reset、Wi-Fi/日志/GPS 同时异常 | 才提高电源、晶振或整板连接优先级 |

### 后续实现方向（未修改代码）

- 把 transport heartbeat 与“新画面 freshness”分开计时；长时间没有成功 JPEG frame/sequence 时退出独占投屏或让 Android 明确重启 capture。
- 为同步 I80 完成等待增加可观测且有限的超时，并确保退出命令不会永久排在卡住的 present 后面。
- 在 cast enter/exit 和失败回滚路径显式清理 bridge touch filter，再验证“按住屏幕开始投屏”的边界场景。
- 增加冻结现场需要的低频日志：最后成功 frame sequence/时间、heartbeat-only 持续时间、present 开始/完成、退出阶段、indev enabled 状态。
- 若硬件复现支持 GT1151 控制器挂死，再评估为触摸 reset 引脚建立 profile/hardware contract；当前配置无法执行硬复位。

### Files Found

| Path | Description |
| --- | --- |
| `.trellis/tasks/08-16-full-frame-casting-color/prd.md` | 全帧 JPEG 投屏需求、独占显示与退出恢复验收标准。 |
| `.trellis/tasks/08-16-full-frame-casting-color/design.md` | v3 数据流、ACK 背压、同步完整帧提交和恢复设计。 |
| `.trellis/tasks/08-16-full-frame-casting-color/implement.md` | 已实施项及尚未完成的真机持续投屏/恢复验证。 |
| `android-cast/app/src/main/java/com/fuckingbms/cast/CastService.kt` | Android latest-frame、ACK 和 heartbeat 发送循环。 |
| `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c` | WebSocket 接收、heartbeat timeout 和 runtime cast 状态。 |
| `components/esp_bms_display_service/esp_bms_display_service.c` | 独占投屏进入、退出、UI 恢复和同步命令队列。 |
| `components/esp_bms_lvgl_bridge/esp_bms_lvgl_bridge.c` | LVGL indev 禁用/恢复、GT1151 过滤和 JPEG 条带提交。 |
| `main/idf_main.c` | runtime 状态到显示退出及业务模块恢复的主循环协调。 |
| `managed_components/espressif__esp_lvgl_adapter/src/display/bridge/v9/lvgl_bridge_v9.c` | dummy draw 同步等待 LCD 完成通知的实现。 |
| `firmware/catalog/input/gt1151-i2c.env` | GT1151 1.1.0、IRQ/I2C 及无 reset 角色的输入目录定义。 |
| `firmware-builds/cast-s3-build/firmware.env` | 当前真机 profile 的 touch GPIO。 |
| `firmware-builds/cast-s3-build/generated/esp_bms_profile_hardware.h` | 生成的 touch IRQ/I2C/reset 配置。 |
| `firmware-builds/cast-s3-build/sdkconfig` | 当前 40 MHz XTAL 与 brownout 配置。 |
| `logs/serial-2026-08-16_00-16-54-cast-fix.log` | 一次正常 heartbeat timeout、UI/模块/触摸恢复的会话。 |
| `logs/serial-2026-08-15_23-28-25-reset.log` | 旧固件在 cast 连接后发生 LoadProhibited 并软件复位的证据。 |

### External References

- 未使用外部网页或文档；判断基于仓库源码、生成配置和串口日志。
- 仓库内版本证据：ESP-IDF `6.0.2`（硬件构建规范及启动日志），`espressif/esp_lcd_touch_gt1151` `1.1.0`（`firmware/catalog/input/gt1151-i2c.env:6`）。

### Related Specs

- `.trellis/spec/backend/logging-guidelines.md`：要求保留运行时里程碑、错误和受限资源诊断，禁止输出敏感凭据。
- `.trellis/spec/backend/hardware-build-flash.md`：profile 是 GPIO 与硬件配置权威；固件行为变更需要构建、烧录和真机验证。
- `.trellis/tasks/08-16-full-frame-casting-color/design.md`：投屏期间独占显示、ACK 背压以及退出后恢复 LVGL/业务模块的既定契约。

## Caveats / Not Found

- 当前串口材料没有捕获用户所说的持续冻结现场，因此无法在 Android capture 停滞、LCD 完成通知丢失和硬件触摸故障之间做最终判定。
- GitNexus index 状态在调查时为 current（commit `7785af2`），但 CLI `query`/`context` 调用均超时且没有返回流程结果；以上调用链由源码直接确认。
- 没有找到 SHA `37f6a5b9a` 对应的 ELF，旧 panic 不能做可信符号化。
- 没有示波器、逻辑分析仪或冻结时 I2C/IRQ 电平记录，不能完全排除震动导致的接触不良；只能根据现有软件状态与日志把它降为次要假设。
- 本研究未修改代码、配置或硬件。
