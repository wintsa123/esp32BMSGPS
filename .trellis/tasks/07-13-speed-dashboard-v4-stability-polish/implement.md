# 实施计划：速度仪表 V4 稳定性与绘制优化

## 开始前门槛

- [x] 用户确认 GPS `2/4 km/h` 迟滞，并确认显示与行程距离共用规范化速度。
- [x] GitNexus 索引已更新至当前提交；待改既有函数均已按精确文件 UID 做 upstream impact。
- [x] 已报告 `runtime_update_snapshot_speed` 为 CRITICAL、`runtime_reset_state` 与 `invalidate_dashboard_viewport` 为 HIGH。
- [x] 用户审核本任务最终 `prd.md`、`design.md` 和 `implement.md` 并明确批准开始（2026-07-13：“激活”）。
- [x] `task.py start` 后先加载 `trellis-before-dev`，读取 backend/frontend 相关规范；inline 模式不派发实施/检查子代理。
- [x] 开始每一阶段前检查 `git status`，只增量修改本任务文件及 Trellis 任务元数据，不覆盖其他在途改动。

## 实施顺序

1. 主机回归先行：GPS stream 与速度迟滞
   - 新增纯 C GPS stream framer 和 host selftest，先复现长 GSV 溢出尾部、无 `$` 噪声、缺失换行后新 `$` 恢复、CRLF 与连续 RMC。
   - 在现有速度仪表纯数学模块加入 2/4 km/h 状态滤波及测试，覆盖 `3.9→4.0` 进入、`2.1→2.0` 退出、fix 失效复位和 mph 不改变阈值。
   - 保留现有 UTC+8、电耗和 FarDriver 自测为基线。

2. Runtime 集成与诊断
   - 用 stream framer 替换 runtime 的直接行长度状态；溢出单独计数，不写入 RMC parse error。
   - RMC 解析成功后先规范化 GPS 速度，再同时供 snapshot 现有投影和行程积分使用。
   - RMC timeout 与 runtime reset 显式清除运动态/分帧态。
   - 加入限频 fix transition 和 60 秒 A/V、overflow、parse error、bytes 汇总。
   - 确认 `runtime_update_snapshot_speed()` 保持未修改；若无法保持，暂停执行并重新做风险审查。

3. LVGL 色带与无条件重绘
   - 提取色带活动段、电池活动段和 render signature 计算，`set_gps_dashboard()` 仅在签名变化时 invalidate。
   - 用 32 个可变宽度宽线段替代色带主体的 64 个共享边三角形，保留颜色、外轮廓、刻度和危险区。
   - 保留卫星/电池小图标的现有三角绘制；不把局部问题扩大为全页面图元重写。
   - 诊断 profile 下统计滚动区间 speed-art draw 次数/耗时和堆指标，只在 scroll end 打印。

4. 真实 LVGL 预览
   - 更新 `preview/speed_dashboard_v4_preview.py` 以镜像固件宽线几何。
   - 重渲染横竖屏至少 `0`、低速、`88`、中间值、满量程、无效、GPS 搜星/定位、BMS 离线和 SOC 缺失状态。
   - 图片写入根目录 `preview/`；逐张查看色带内部/相邻段接缝、轮廓、刻度、状态栏、挡位与隐藏逻辑。

5. 滚动性能 A/B
   - Profile A：full viewport invalidate=`y`；Profile B：=`n`，其余配置和固件逻辑相同。
   - 两个 profile 均测试横竖屏电池↔速度↔投屏的手动慢拖、快速甩动、程序化动画和反复往返。
   - 记录切页体感/耗时、speed-art draw 次数/耗时、free/min/largest heap 及是否有残影/分块。
   - 只有 B 无显示回归且性能更好才修改 normal default；否则保留 `y`。

6. 软件质量验证
   - 编译运行 GPS stream、speed dashboard 和 FarDriver host selftest。
   - 运行字体/预览检查、`git diff --check` 和 ESP-IDF 5.5.4 构建，检查固件大小、警告、静态 RAM 与栈风险。
   - 运行 GitNexus `detect_changes --scope all`，核对 runtime 主循环、GPS、行程、页面滚动、快捷面板返回和设置导航流程。
   - 执行 Trellis quality check；发现问题后修复并重跑受影响及最终全量检查。

7. LAN RFC2217 真机完成门
   - 使用 `rfc2217://192.168.2.10:4000?ign_set_control`、115200 刷写一次最终固件并监视。
   - 最多等待 5 分钟定位：验证 RMC `A/V`、PPS、A/V/overflow/parse 汇总和橙/绿状态一致。
   - 静止观察原始 1–3 km/h 时主速度/距离为 0；移动达到 4 km/h 退出静止，降到 2 km/h 回零。
   - 反复切页与旋转，确认无明显卡顿、接缝、残影、持续堆下降、panic 或 WDT。
   - 回归 GPS/控制器来源、控制器断连/重连、km/h↔mph、无 BMS 隐藏电池/电耗、默认 1 挡。

## 预期修改范围

- `components/esp_bms_idf_runtime/esp_bms_gps_stream.c`（新增）
- `components/esp_bms_idf_runtime/include/esp_bms_gps_stream.h`（新增）
- `components/esp_bms_idf_runtime/esp_bms_speed_dashboard.c`
- `components/esp_bms_idf_runtime/include/esp_bms_speed_dashboard.h`
- `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c`
- `components/esp_bms_idf_runtime/include/esp_bms_idf_runtime.h`
- `components/esp_bms_idf_runtime/CMakeLists.txt`
- `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c`
- `components/esp_bms_lvgl_ui/Kconfig`、`sdkconfig.defaults*`（仅诊断/A-B 结果需要时）
- `tests/gps_stream_selftest.c`（新增）
- `tests/speed_dashboard_selftest.c`
- `preview/speed_dashboard_v4_preview.py` 及根目录 `preview/` 中更新图片
- 本任务 Trellis 文档/任务状态；不修改 Web/NVS/公共 snapshot 契约。

## 验证命令

```bash
gcc -std=c11 -Wall -Wextra -Werror \
  -Icomponents/esp_bms_idf_runtime/include \
  tests/gps_stream_selftest.c \
  components/esp_bms_idf_runtime/esp_bms_gps_stream.c \
  -o /tmp/gps_stream_selftest
/tmp/gps_stream_selftest

gcc -std=c11 -Wall -Wextra -Werror \
  -Icomponents/esp_bms_idf_runtime/include \
  tests/speed_dashboard_selftest.c \
  components/esp_bms_idf_runtime/esp_bms_speed_dashboard.c \
  -o /tmp/speed_dashboard_selftest
/tmp/speed_dashboard_selftest

gcc -std=c11 -Wall -Wextra -Werror \
  -Icomponents/esp_fardriver_protocol/include \
  tests/fardriver_protocol_selftest.c \
  components/esp_fardriver_protocol/esp_fardriver_protocol.c \
  -lm -o /tmp/fardriver_protocol_selftest
/tmp/fardriver_protocol_selftest

git diff --check
./scripts/esp-idf-env.sh build
node .gitnexus/run.cjs detect-changes --repo esp32BMSGPS --scope all
```

最终刷写：

```bash
./scripts/esp-idf-env.sh \
  -p "rfc2217://192.168.2.10:4000?ign_set_control" \
  -b 115200 flash
./scripts/esp-idf-env.sh \
  -p "rfc2217://192.168.2.10:4000?ign_set_control" \
  -b 115200 monitor --no-reset
```

## 回滚点

- 纯模块自测通过后再接 runtime；失败时只回退新模块，不碰 UI。
- runtime 真机日志语义稳定后再评估 UI；失败时保留旧 snapshot 契约并恢复原始入站速度。
- 宽线预览通过后再刷真机；视觉不合格只回滚色带主体，不回滚 GPS 修复。
- full-invalidate `n` 若出现一次残影即回退到 `y`，不以偶发/难复现为通过。
- 所有回滚仅针对本任务增量，不使用 `git reset --hard`、`git checkout --` 或覆盖用户文件。

## 2026-07-13 实施与验证结果

- 新增纯 C NMEA stream framer、RMC 句型分类和 GPS 速度字段 parser；host
  selftest 覆盖无 `$` 噪声、长 GSV 溢出整句丢弃、断句恢复、CRLF、连续
  RMC、损坏非 RMC 句忽略，以及合法 `A` RMC 空速度字段按 0 处理。
- 2/4 km/h GPS 迟滞 host selftest 覆盖 `3.9→4.0`、`2.1→2.0` 和 fix
  失效复位；runtime 同一规范化 knot-milli 同时供现有 snapshot 投影和行程
  距离积分，`runtime_update_snapshot_speed()` 零 diff。
- 真机发现 336H 静止时可输出 `status=A` 且速度字段为空，已修复为保持 fix
  有效并输出 0；最终另一组样本原始速度为 `1.24 kn≈2.30 km/h`，处于静止
  抑制区。60 秒汇总：`fix=1 pps=1 A=81 V=0 overflow=0 parse_errors=0
  bytes=60033 rmc=81`，无 panic/WDT。
- `speed_art` 仅在离散 render signature 变化时失效；色带主体由 64 个三角形
  改为 32 条可变宽度、相邻 1 px 重叠的宽线。真实 LVGL 9 预览已重渲染并
  检查横竖屏 0/低速/88/满量程/超量程/无效/GPS 搜星/BMS 离线/控制器离线/
  SOC 缺失状态，无内部透黑接缝、裁切或离线挡位回归。
- drag diagnostics 新增 draw count/elapsed/max 与 free/min/largest heap 汇总；
  full-invalidate `y/n` 两个 profile 均构建通过。远端无法观察 TFT 残影，未满足
  `n` 的零残影证明门槛，因此 normal 和最终刷写保持 `y`。
- GPS stream、speed dashboard、FarDriver host selftest，Web 内联脚本、三套
  中文字体、drag 脚本语法、`git diff --check`、ESP-IDF 5.5.4 build 均通过；
  最终固件 `0x15b710`，最小 OTA 槽余量 `0x848f0`（28%）。
- GitNexus 重建索引后 `detect-changes --scope all` 仍为 CRITICAL（14 个已跟踪
  文件、50 个符号、20 条流程）；报告混入任务前 `AGENTS.md`/`CLAUDE.md`
  改动且将新增函数相邻 hunk 归到 `is_leap_year` 等未改符号。逐符号审计确认
  本任务既有编辑点仅 `runtime_reset_state()` 为 HIGH，其余为 LOW；未修改
  CRITICAL 的 snapshot 速度投影函数。

### 尚需现场确认

- 在可目视 TFT 的情况下执行 full-invalidate `y/n` 真机慢拖、快速甩动和程序化
  动画 A/B；`n` 任一残影即继续保留 `y`。
- 实车移动跨过 4 km/h、再降到 2 km/h，确认 TFT 与行程距离同步退出/进入静止态。
- 现场目视确认横竖屏色带、切页体感和橙/绿状态点；回归控制器断连/重连与
  GPS/Controller 来源切换。这些分支未从远端串口冒充为已通过。

## 2026-07-14 横屏曲线平滑度跟进

- [x] 用户确认“不平滑”指弧形轮廓折线感，而不是页面滑动或速度刷新卡顿。
- [x] 将固件和 preview 的色带段数由 32 提高到 48；危险区、次刻度、主刻度改为
  按总段数的 `7/8`、`1/16`、`1/4` 计算，并增加编译期断言确保活动段数仍适配
  render signature 的低 6 位。
- [x] 重渲染根目录 `preview/` 中横屏 0/88/180/超量程状态并逐张检查弧线、颜色
  边界和刻度位置；横竖屏至少各检查一张。
- [ ] 运行 drag diagnostics 对比 32/48 段 draw elapsed 和切页表现；若明显回退，
  保留 32 段色带，只对外轮廓加密采样。
- [x] 通过 `git diff --check`、ESP-IDF build、GitNexus `detect_changes()` 和 LAN
  RFC2217 真机刷写监控后，再记录现场目视结论。

### 2026-07-14 跟进结果

- 48 段配 1-2 px 方端重叠仍有黑色尖缝，圆端会形成明显胶囊段；最终采用 4 px
  切线重叠，横竖屏及 13 个状态预览无裂缝、裁切或颜色边界错位。
- 未启用 `LV_USE_VECTOR_GRAPHIC`/ThorVG：`lv_bezier3()` 只生成坐标，软件
  `cubic_to` 需要额外矢量引擎并把 LVGL draw-thread stack 提高到至少 32 KB。
- GPS stream、speed dashboard、FarDriver host selftest 和 `git diff --check`
  通过；ESP-IDF 5.5.4 build 通过，镜像 `0x15ef60`，最小 OTA 槽剩余
  `0x810a0`（27%）。
- GitNexus compare `origin/main` 为 MEDIUM，实际受影响只有两条
  `speed_dashboard_draw_event_cb` 绘制流程；其余报告项为同文件相邻 hunk 误归属。
- RFC2217 刷写、哈希校验和启动监控通过；首屏后 free heap 约 109 KB、最大连续块
  约 106 KB，无 panic/WDT。远程串口无法目视 TFT，现场曲线观感与拖动耗时仍需
  用户在设备前确认。

## 2026-07-14 真实 UI 桌面模拟器

- [x] 新增 `simulator/` CMake 工程，复用仓库 LVGL 9.5.0、SDL2、真实
  `esp_bms_lvgl_ui.c`、固件字体和 LVGL contract。
- [x] 添加最小 ESP-IDF 主机兼容头/实现；不得复制页面、绘制或事件逻辑。
- [x] 主机入口提供横竖屏启动、鼠标输入、快照快捷键、页面快捷键和 action 回写。
- [x] 提供 `scripts/run-lvgl-simulator.sh`，支持交互运行、`--portrait` 和
  `--headless` 冒烟测试。
- [ ] 构建并运行横竖屏无头测试；在有桌面会话时启动 SDL 窗口完成鼠标交互检查。
  当前环境只有 TTY，已完成前半项；桌面窗口鼠标目视仍需在图形会话中确认。
- [x] 运行 `git diff --check`、ESP-IDF build 和 GitNexus `detect_changes()`；主机工具
  不改变固件镜像时不重复刷写。

### 2026-07-14 桌面模拟器结果

- `simulator/` 直接编译真实 `esp_bms_lvgl_ui.c`、六个固件字体源、`wlanJZ.c`、
  LVGL 9.5.0 和 SDL2 2.26.5；ESP-IDF 兼容层只实现 UI 已引用的错误、日志、定时器、
  堆诊断和配置宏。
- 桌面快照使用生产 `esp_bms_dashboard_snapshot_t`；鼠标进入 LVGL SDL pointer driver，
  键盘支持速度、四页、GPS/BMS/控制器在线状态、单位和旋转，UI action 会回写主机快照。
- 干净构建后，320x240 与 240x320 均通过 120 帧 SDL dummy 冒烟；测试通过 SDL
  event queue 注入速度、GPS、页面和旋转快捷键，覆盖真实键盘入口及 UI 重建。
- GPS stream、speed dashboard、FarDriver host selftest、脚本语法、`git diff --check`
  和 ESP-IDF 5.5.4 build 通过；固件仍为 `0x15ef60`，最小 OTA 槽剩余 `0x810a0`
  （27%），模拟器没有进入固件组件列表。
- GitNexus 重建索引后 staged `detect-changes` 为 CRITICAL（16 个文件、85 个符号、
  19 条流程），原因是新增主机 `main()` 下游调用真实 UI，并把兼容层同名 `ESP_*`
  宏计为变化。逐符号 upstream impact 中 `main()`、`apply_command()` 和
  `apply_action_event()` 均为 LOW，且 staged 范围没有固件 `components/`、`main/`、
  根 CMake 或 `sdkconfig` 修改。
- 当前 `DISPLAY`/`WAYLAND_DISPLAY` 均为空、`XDG_SESSION_TYPE=tty`，不能在本会话冒充
  完成鼠标目视。图形桌面终端运行 `./scripts/run-lvgl-simulator.sh` 即可交互；物理面板
  色序、触摸校准和刷新时序仍以真机为最终依据。
