# 实施计划：统一速度仪表 V4 与速度来源选择

## 风险门槛

- [x] 已按精确文件 UID 对 `settings_show_controller_detail` 运行上游 impact：CRITICAL，19 个上游、5 个直接调用者、6 组流程；已向用户报告。
- [x] 已按精确文件 UID 对 `runtime_update_snapshot_speed` 运行上游 impact：CRITICAL，12 个上游、6 个直接调用者、5 组流程；已向用户报告。
- [ ] 编辑其余既有函数/类型前逐一运行精确 UID upstream impact；HIGH/CRITICAL 必须先报告。
- [ ] 每个阶段都以当前脏工作区为基线审阅差异，不覆盖 GPS/PPS、投屏、控制器或 Trellis 在途改动。

## 实施顺序

1. 契约与纯数学
   - 扩展公共 enum/action/data-source/snapshot 显式字段，保留现有数值。
   - 新建 runtime 内部纯 C 速度仪表数学模块：UTC+8 公历换算、对齐样本、GPS 距离、带回充抵扣的能量积分和单位投影。
   - 新增 host selftest，覆盖跨日/月/闰年、5 km/h 起算、短暂失效、长间隔、0.1 km 门槛、回充与 km/mi。

2. Runtime 状态、迁移与投影
   - 加入速度偏好、BMS 新鲜度时间戳、行程累计和 HTTP pending 来源字段。
   - NVS 加载 `speed_src`，缺失时从 `ctl_page` 迁移并补写；保存新旧键。
   - 重构主速度投影：基于偏好、在线和字段有效性显式设置 active source、`SPEED_VALID`、速度、本地时间和平均电耗。
   - 在 RMC/BMS/控制器连接变化、超时、单位和来源 action/HTTP 变化路径调用统一投影。
   - 组合采集模式同时驱动 BMS poll 与控制器 gather；GPS 继续无条件读取；行程状态不依赖页面。

3. HTTP 与 Web
   - `/api/config` GET/POST 加入来源与在线字段，复用 pending 锁和 NVS 保存。
   - Web 设备设置加入中英文来源选项和离线降级提示，验证部分 POST 与完整表单 POST 都不会覆盖其他值。

4. LVGL 轮播与 V4 仪表
   - 将轮播收敛为三页，集中 Controller/GPS 兼容映射，统一页返回组合 data source。
   - 使用单个自定义绘制对象实现 32 段色带、轮廓、刻度、卫星/状态点和电池；标签实现速度、单位、SOC、电耗、温度、挡位和本地时间。
   - 用一个布局函数切换横竖屏几何；移除来源变化触发的页面结构重建。
   - 数据更新严格执行无效显示、离线隐藏、超量程色带 clamp 和主数字保真。

5. TFT 设置
   - 更名“速度仪表”，加入来源 action 与离线当前来源文案。
   - 基础行始终显示；控制器专属参数仅在线时创建；保留已有编辑与 BLE 绑定回调。
   - 更新设置变化检测，避免来源/在线变化时漏刷新或刷新死循环。

6. 预览与静态验证
   - 在 `preview/` 增加与固件对象树/几何一致的 LVGL preview 模块。
   - 渲染并查看横竖屏 0、88、中间、满量程、无效、GPS 搜星/定位、控制器在线/离线、SOC 缺失状态；不满意则迭代。
   - 运行字体字形检查，确认 TFT 所有新增 dashboard 文本均为 ASCII，设置中文字形已存在。

7. 软件验证
   - 编译运行新增 host selftest。
   - 编译运行 `tests/fardriver_protocol_selftest.c`。
   - 运行 `git diff --check`。
   - 运行 `./scripts/esp-idf-env.sh build`；检查 RAM/Flash 报告与警告。
   - 运行 `node .gitnexus/run.cjs detect-changes -r esp32BMSGPS`，并针对设置导航、BLE 绑定、主循环、HTTP 配置逐项核对。

8. 真机与质量门
   - 按 `esp32-lan-rfc2217-flash` 通过 `rfc2217://192.168.2.10:4000?ign_set_control`、115200 刷写并监视。
   - 最多等待 5 分钟获取 GPS/控制器；验证四方向旋转、三页轮播、来源切换、断连/重连、单位联动、时间、电耗、内存和错误日志。
   - 运行 Trellis quality check，处理发现后重跑完整验证。

## 预期修改范围

- `components/esp_bms_lvgl_ui/include/esp_bms_lvgl_ui.h`
- `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c`
- `components/esp_bms_idf_runtime/include/esp_bms_idf_runtime.h`
- `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c`
- `components/esp_bms_idf_runtime/CMakeLists.txt`
- `components/esp_bms_idf_runtime/` 新增纯数学模块
- `main/idf_main.c`
- `main/web/index.html`
- `tests/` 新增 host selftest
- `preview/` 新增 V4 LVGL preview 代码与图片

## 验证命令

```bash
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
node .gitnexus/run.cjs detect-changes -r esp32BMSGPS
```

## 回滚点

- 契约/数学自测通过后再接 runtime，避免 UI 与采样公式同时调试。
- runtime/API 往返通过后再替换 LVGL 页面，便于区分数据错误与绘制错误。
- 真机刷写前保留已通过的软件验证结果；若硬件异常，只回退本任务增量，不恢复或覆盖工作区其他改动。
