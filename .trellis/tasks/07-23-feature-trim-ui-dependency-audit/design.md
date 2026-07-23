# 技术设计：功能裁剪与 UI 依赖一致性

## 目标边界

以生成的 profile feature 宏作为单一编译契约。配置器负责模块和 dashboard 的合法组合，
CMake 负责组件闭包，LVGL UI 负责只创建已启用模块的设置入口；统一 snapshot 保持跨模块
ABI，不因裁剪删除字段。

## 设置页裁剪

将设置入口从固定数组改为由 feature 宏筛选的静态选项表或等价的构建期计数。入口规则：

- `network` 启用时显示热点；否则不显示热点入口和相关动作路径。
- BLE 能力由 `bms` 或 `controller` 任一模块提供；蓝牙总设置仅在至少一个 BLE 模块启用时显示。
- `bms` 启用时显示保护板设置；否则不显示 BMS 绑定、类型和预设范围入口。
- `controller` 或 `gps` 启用时显示速度仪表；否则不显示控制器/速度仪表入口。
- 系统和关于始终保留。

所有索引、标题查找、点击事件和返回导航都使用筛选后的表，避免隐藏项留下空卡片或死索引。

## Dashboard 依赖

在 catalog/配置器层集中声明 dashboard 所需模块：S1000RR、Fireblade 至少需要 `gps` 或
`controller`，controller dashboard 需要 `controller`。生成 profile 时删除不满足依赖的
dashboard，并同步 feature 宏和 UI 源文件闭包。UI 仍可读取统一 snapshot 的兼容字段，但
不得调用未编译模块的函数或动作。

## 验证与回退

生成四个最小 profile（关闭 BMS、GPS、controller、network）并编译，再生成全模块 profile
回归。检查生成 `profile.cmake` 的 requires、预处理后的 UI 源码和设置选项数量。若某个
裁剪组合暴露跨模块 ABI 问题，优先补齐 feature 边界或 catalog 依赖，不恢复全量组件。
