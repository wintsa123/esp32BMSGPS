# 执行计划：功能裁剪与 UI 依赖一致性

1. [x] 对配置器依赖解析、模块 registry、UI 初始化/设置入口和 dashboard 选择函数运行 GitNexus impact/context，记录影响范围。
2. [x] 完善 dashboard catalog 的模块依赖声明及配置器校验/自动降级规则。
3. [x] 按 feature 宏裁剪设置页入口、快捷入口和对应动作；保证表索引、导航和返回路径一致。
4. [x] 检查仪表 UI 对 snapshot 字段的使用，移除未启用模块才会触发的不可达调用或补齐明确的兼容边界。
5. [x] 为 BMS/GPS/controller/network 分别关闭的 profile 生成配置并编译，检查组件闭包和预处理符号。
6. [x] 运行配置器自测、主机自测、LVGL headless smoke、静态依赖扫描和 GitNexus detect_changes。
7. [x] 若固件源码改变，运行 ESP-IDF 构建；必要时通过 RFC2217 刷写并做启动回归。

## 验收结果

- 配置器根据 dashboard catalog 的 `REQUIRES_MODULES` / `REQUIRES_MODULES_ANY` 自动移除不满足依赖的 dashboard；BMS、GPS、controller、network 关闭矩阵均生成成功。
- LVGL 设置根页、快捷面板和详情页按 feature 宏裁剪；`settings_show_detail()` 与 runtime action dispatcher 对旧事件再做拒绝，避免 stale UI/event 穿透。
- GPS-only 速度仪表详情不创建控制器绑定、轮胎和传动比操作；统一 snapshot ABI 保留，不产生未启用模块的链接引用。
- 五组 ESP-IDF profile（`bms-off`、`gps-off`、`controller-off`、`network-off`、`all-modules`）均成功编译；生成 CMake 闭包分别排除对应组件。
- `./tests/configurator_selftest.sh`、`./scripts/run-host-selftests.sh`、`git diff --check` 通过；RFC2217 全模块镜像刷写成功。无 TTY 条件下 monitor 未执行，保留为硬件启动日志缺口。

## 风险与回退

- 风险：静态数组改为筛选后可能影响页面高度和返回索引；使用实际可见项计数并复用现有布局函数。
- 风险：dashboard 依赖收紧可能改变旧 profile 的自动选择；保留自动选择并在无可用 dashboard 时生成空值。
- 回退点：配置器依赖规则、UI 选项筛选和 dashboard 依赖声明分别可独立回退。
