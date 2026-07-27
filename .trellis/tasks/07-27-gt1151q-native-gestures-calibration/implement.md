# 实施计划

1. 进入实施前读取 `trellis-before-dev`、相关 backend/frontend 规范，并对每个待改符号执行
   GitNexus upstream impact；若为 HIGH 或 CRITICAL，先报告风险。
2. 在 `esp_bms_lvgl_bridge` 增加 GT1151Q 私有手势解码、去重和取走接口。读取 `0x814C` 必须在
   第三方坐标读取之前，保持 `0x814E` 的现有无效帧保持和确认逻辑；不改 `managed_components/`。
3. 将 bridge 校准状态按 `touch_driver == ESP_BMS_LVGL_TOUCH_GT1151` 限定：GT1151Q 不加载、
   应用、保存或清除校准 NVS；其他 driver 不变。
4. 在显示服务的现有 LVGL 锁和 `lv_timer_handler()` 之后取走一个语义手势，并调用新的 UI
   分派入口；向 UI 初始化显式传递校准和原生手势能力。更新模拟器调用点。
5. 在 `esp_bms_lvgl_ui` 中隐藏 GT1151Q 的屏幕校准行和残留入口；为快捷面板、设置行及下拉
   选项注册按键焦点，复用原有 `LV_EVENT_CLICKED` 和返回回调。
6. 对 GT1151Q 禁用首页横向坐标轮播和竞争的坐标快捷面板开关，接入左右翻页、下开、上关、
   双击诊断及四键焦点语义；保留设置滚动、边缘返回、滑条和普通坐标点击。
7. 增加最小模拟器自检，覆盖语义手势、双击无副作用、焦点确认/返回和 GT1151Q 无校准入口；
   保持测试使用固定容量，不新增依赖或独立任务。
8. 运行格式、`git diff --check`、模拟器 headless 自检、S3 profile 构建和 GitNexus
   `detect-changes`。随后按 `esp32-lan-rfc2217-flash` 刷写并监控实机日志，验证所有原始码、
   一次性派发、`0x814E` 按住兼容和触控坐标回归。

## 风险文件

- `components/esp_bms_lvgl_bridge/esp_bms_lvgl_bridge.c`：所有 GT1151 输入经过此处；读取顺序或
  去重错误会导致丢手势、重复动作或按住状态退化。
- `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c`：当前工作区已有大量未提交 UI 变更；焦点注册
  必须贴合现有创建路径，不能回退其他任务内容。
- `components/esp_bms_display_service/esp_bms_display_service.c`：必须只在服务任务和 bridge 锁内
  访问 UI，不可在 I2C 回调内跨线程调用。

## Validation Matrix

| 场景 | 预期 |
| --- | --- |
| GT1151Q 左/右滑 | 一次且仅一次切换相邻仪表页 |
| GT1151Q 下/上滑 | 下滑打开快捷面板；上滑仅关闭已打开面板 |
| GT1151Q 双击 | 有诊断记录，无 UI 或运行时副作用 |
| GT1151Q `C1/C2/C4/C8` | 可见焦点上一/下一、确认、返回取消，覆盖设置与快捷面板 |
| GT1151Q 坐标控件 | 设置滚动、边缘返回、滑条和轻触保持可用 |
| GT1151Q 校准 | 入口不可见，NVS 不被加载、应用或写入 |
| 非 GT1151 profile | 原有校准和坐标交互无回归 |

## Rollback

回退本任务触及的 bridge、display service、UI、模拟器和测试文件即可。校准 NVS 内容、GT1151
固件配置、I2C 速率和第三方驱动均不在变更集内。
