# Implementation

1. 新增 FarDriver 纯 C 协议组件和单一可运行自检，先锁定 CRC、地址映射和公式。
2. 扩展 LVGL 公共合同：页面、action event、snapshot 字段、有效位和扫描结果。
3. 扩展 runtime 设置/NVS，保持新键可选加载和恢复默认保留绑定。
4. 在现有 NimBLE host 中增加 FarDriver central 状态，复用串行扫描并支持三连接配置。
5. 增加稳定页面/设置可见状态 API，由主循环向 runtime 设置主动数据源。
6. 实现控制器设置页、步进详情页和独立蓝牙选择。
7. 实现动态两页/三页映射与横竖控制器页面，固定缓冲更新遥测。
8. 按 `controller_display_v2_dual_hero.png` 重建控制器页对象树：外框、带 `SPEED`/`GEAR` 标题的主区、蓝色挡位区、横屏四列/竖屏 2×2 辅助区、独立数字/标题/单位标签，并校准横竖屏字号与位置。
   - 辅助单位与数值同排；ST7789 使用 RGB 元素顺序，确保真机蓝色不发生红蓝互换。
9. 同步控制器预览渲染器并生成横竖正常/无效预览到 `preview/`，逐张视觉检查。
10. 运行协议自检、格式检查、构建、GitNexus `detect_changes --scope compare --base-ref main`。
11. 使用 RFC2217 技能刷写并监控启动；真实 FarDriver/BMS/手机并发与 10 分钟稳定性仅在设备均可用时验收。

## Rollback Points

- 协议组件独立，可先保留解析自检并回退 BLE 接入。
- 动态分页失败时恢复固定两页，不影响设置持久化。
- FarDriver 连接失败不得影响 BMS central 或手机 peripheral。

## Verification

- FarDriver 主机自检通过：compact/extended、CRC、RPM、挡位、功率、温度、内部参数和无效帧。
- `git diff --check` 与 `./scripts/esp-idf-env.sh build` 通过；最终组合镜像 `0x147c50`，最小 app 分区剩余 `0x983b0`（32%）。
- GitNexus compare 检测到 22 个文件、119 个符号、54 条流程，整体风险 `CRITICAL`；变更集中在预期 runtime/app/dashboard 核心路径，同时包含工作区的其他并行任务。
- RFC2217 首次尝试在 stub 启动阶段失败，第二次刷写成功且所有写入哈希校验通过；启动日志确认 LVGL、触摸、NimBLE 和本地广播正常，无 panic/watchdog。
- 已生成并检查 controller 正常/离线横竖的 Pillow 清晰参考图和 LVGL headless 预览图；固件仅对主数字使用 `controller_digits_72`，辅助 ASCII 改用内置 Montserrat 14，避免生成稀疏字体在真机乱码。
- 未完成：真实 FarDriver、BMS、手机三连接，控制器重绑定触摸流程，以及连续 10 分钟堆/重连稳定性验证。
- 追加修复：控制器页面显示改为首项主开关；关闭时隐藏连接/蓝牙选择并阻止扫描、当前连接及连接中竞态，保留绑定；开启时自动连接已绑定设备。
- 字体从 `esp_bms_lvgl_ui.c` 的 151 个实际汉字重生成，缺字审计为 0；构建镜像 `0x159680`，RFC2217 刷写与启动验证通过。
- `dual_hero` 视觉修订后主数字使用 72px 精简字体，辅助区使用内置 Montserrat 14；横屏四列与竖屏 2×2 无裁切。RFC2217 最终启动日志的 ELF SHA `fae8453c6…` 与本地一致，ST7789/LVGL/XPT2046 正常，无 panic/watchdog。
- 乱码/布局回归：删除 `controller_aux_12`，`CTRL` / `MOTOR` 移到单元格左上角，温度换到下方，车速单位左移并加宽。带值横竖 Pillow/LVGL 预览已目视检查。
- 轮播回归：`LV_EVENT_SCROLL_END` 按稳定页夹到相邻一页；控制器页开/关的所有起点→原始目标组合表均满足最多一页。ESP-IDF 构建镜像 `0x147910`，分区剩余 32%。
