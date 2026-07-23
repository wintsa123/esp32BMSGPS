# 功能裁剪与 UI 依赖审计

## Goal

审计编译脚本裁剪模块后，设置页选项、仪表 UI、运行时快照和 CMake 组件闭包是否仍保持一致；发现违反裁剪边界的问题后，修复并用最小功能矩阵验证。

## Confirmed Facts

- `start.sh:882-954` 根据 `MODULES` 生成 `ESP_BMS_FEATURE_*`、`ESP_BMS_PROFILE_MAIN_REQUIRES` 和模块清单；未选模块的组件通常不会进入 profile 的 `REQUIRES`。
- `main/esp_bms_module_registry.c.in` 对 audio、BMS、controller、GPS、network 使用条件编译，未选模块的注册、启动和 tick 调用会被排除。
- `components/esp_bms_lvgl_ui/CMakeLists.txt` 只按 dashboard feature 裁剪 dashboard 源文件，并向 UI 组件传入 dashboard 宏，没有按 BMS、GPS、network 等模块裁剪设置页选项。
- `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:2299-2306` 固定定义热点、蓝牙、BMS、控制器、系统、关于六个设置入口；当前没有基于模块 feature 的过滤。
- `start.sh:544-568` 会根据 GPS/controller 模块决定 dashboard 可用性，但 dashboard catalog 文件本身没有声明所需模块；依赖规则分散在脚本逻辑中。
- 仪表 UI 的数据输入是统一的 `esp_bms_dashboard_snapshot_t`；`speed_page_sync`、`set_dashboard`、`set_gps_dashboard` 等路径可能读取 BMS/GPS/controller 字段，即使对应生产模块未编译。

## Requirements

1. 对每个可裁剪模块和 dashboard，明确其配置器选项、生成的 feature 宏、CMake 组件、设置页入口和运行时数据依赖。
2. 未选模块不得留下不可用的设置入口、不可达动作，或导致编译时仍链接该模块组件的隐式依赖。
3. dashboard 在依赖模块未选时必须被自动移除、降级为可用 dashboard，或明确保留并证明只依赖统一快照而不需要该模块；不能出现空白页、死按钮或未定义引用。
4. 对 BMS 未选、GPS 未选、controller 未选、network 未选以及全模块最小配置执行配置生成、源码编译和 UI/运行时矩阵检查。
5. 保持 profile 默认行为兼容；不通过关闭编译器警告或添加无依据的 GPIO/模块回退掩盖依赖错误。

## Acceptance Criteria

- [ ] 形成模块 → feature → CMake requires → UI/settings → snapshot/action 的依赖矩阵。
- [ ] 明确列出当前发现的每个缺陷及文件/行号，或证明其行为符合裁剪契约。
- [ ] 最小配置不显示未启用模块的设置项，且未产生对应动作调用。
- [ ] dashboard 与其模块依赖一致；未满足依赖时配置器和生成 profile 不再包含该 dashboard。
- [ ] 至少四个裁剪 profile 成功生成并编译：BMS-only-off、GPS-only-off、controller-only-off、network-only-off；全模块 profile 保持成功。
- [ ] 配置器自测、主机自测、静态依赖检查通过；若修改固件源码，再完成 ESP32 构建和必要的硬件回归。

## Out of Scope

- 不重新设计 dashboard 视觉或运行时数据结构，除非依赖审计证明现有结构无法安全裁剪。
- 不删除统一快照中仅因兼容 ABI 而保留的字段；重点是编译链接、入口可见性和动作可达性。

- 本轮发现问题后直接实施修复，并完成四组裁剪 profile 编译验证。

## Notes

- Keep `prd.md` focused on requirements, constraints, and acceptance criteria.
- Lightweight tasks can remain PRD-only.
- For complex tasks, add `design.md` for technical design and `implement.md` for execution planning before `task.py start`.
