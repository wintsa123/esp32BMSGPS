# 配置器驱动选择与 profile 依赖设计

## Scope

本次实现聚焦于现有 ESP-IDF 彩屏路径：自定义开发板先选择 MCU，再从静态显示/触摸 catalog 中选择兼容控制器；配置器生成 profile 专属的硬件头文件、组件 manifest 和 CMake 依赖闭包。OLED、RGB/MIPI-DSI 和按键/编码器后端留给后续任务，不在本次菜单中暴露。

## Catalog contract

`firmware/catalog/display/*.env` 和 `firmware/catalog/input/*.env` 保持 ASCII `KEY=VALUE`，每个驱动记录声明 `TARGETS`、`BUS`、`COMPONENT`、固定 `VERSION`、`HEADER`、`INIT` 和 GPIO 角色。MCU 记录的 `DISPLAY_BUSES` 是总线能力边界。

显示选项满足 `TARGETS` 包含 MCU 且 `BUS` 包含 MCU 总线；触摸选项满足 `TARGETS` 包含 MCU，且非 `none` 的 `BUS` 与显示所选总线兼容当前桥接路径。`none.env` 是唯一 `none` 记录，菜单由专用排序函数保证它最后出现。

## Profile flow

配置校验读取选定记录，拒绝不支持的 MCU、总线、数据宽度、组件元数据或 GPIO 角色。`write_profile` 在 profile 目录中生成：

- `generated/profile.cmake`：只包含基础组件和选定显示/触摸组件；
- `generated/idf_component.yml`：只包含当前 profile 的驱动依赖和公共 LVGL 依赖；
- `generated/dependencies.lock`：由 profile 构建目录独立保存，绝不使用仓库根 manifest/lock；
- `generated/esp_bms_profile_hardware.h`：只启用当前控制器宏和 GPIO 角色。

构建以 profile 目录作为临时 ESP-IDF project 输入，仓库现有组件通过 `EXTRA_COMPONENT_DIRS` 复用。组件管理器缺失依赖时正常下载，缓存存在时复用；无网络且无缓存的失败在生成/构建前明确报告。

## Firmware bridge contract

桥接代码根据生成的 panel/touch 枚举进行条件编译。首批已存在的 ST7789、ST7796、ILI9488、XPT2046、FT5X06、GT1151 保持现有初始化；新 catalog 记录只有在对应 Component Registry 包和桥接初始化适配完成后才进入可构建选项。`none` 不要求触摸 GPIO，也不包含触摸组件依赖。

## Compatibility

已有固定 board profile 的配置格式继续有效。旧的 `custom` 设备名称、显示总线、输入总线字段不再由交互流程生成；非交互配置若仍使用 custom 设备记录则校验失败并提示选择 catalog 驱动。Bash 与 PowerShell 必须共享相同筛选、排序和输出字段。
