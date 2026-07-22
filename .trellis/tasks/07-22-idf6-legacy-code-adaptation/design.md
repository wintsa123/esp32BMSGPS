# IDF 6 配置驱动适配设计

## Boundary

本任务固定使用 ESP-IDF v6.0.2。它把硬件选择从 LVGL bridge 和 `idf_main.c` 移到
现有的 catalog/profile 配置链，同时保留 bridge 对显示和触摸的通用生命周期、LVGL
注册、旋转和校准职责。

不新建第二种用户配置格式：`firmware/catalog/**/*.env` 是受版本控制的硬件事实，
`firmware-builds/<profile>/firmware.env` 是用户保存的完整选择，`generated/` 是该选择的
构建输入。所有 profile 目录保持在已忽略的 `firmware-builds/` 中。

## Hardware Data Flow

```text
catalog board/display/input/module .env
  -> Bash / PowerShell validation and missing-role collection
  -> firmware-builds/<profile>/firmware.env
  -> generated/profile.cmake + generated/esp_bms_hardware_config.h
  -> esp_bms_hardware_config component
  -> idf_main.c -> generic LVGL bridge driver dispatch
```

Catalog 扩展为显式声明控制器、分辨率、像素时钟、默认旋转、触摸变换、背光电平和
必要 GPIO 角色。board 仅拥有真实电路连接；display/input 仅拥有控制器和协议属性；
module 仅拥有按功能启用的 GPIO 角色。缺少的可选角色不会以某块板的引脚作为代码默认值。
生成器将缺失但允许为 NC 的角色写成 `GPIO_NUM_NC`，将已选功能的必需角色视为配置错误。

`esp_bms_hardware_config` 是唯一共享 profile 配置组件：它公开配置结构并包含 profile
生成的头。`idf_main.c` 从该组件取得初始显示/触摸配置，只叠加 NVS 保存的用户旋转；
LVGL bridge 接受该结构，不再声明默认配置宏、板型 GPIO 或控制器名称。

bridge 按配置中的 bus 和 driver 枚举分派 SPI ST7789/XPT2046、I80 ST7796U/GT1151、
I80 ILI9488/FT6336U。任何新驱动均在 ESP-IDF 6.0.2 Component Manager 中解析并纳入
目标专用锁定；不通过 target 条件编译或全局关闭警告来掩盖兼容性问题。

## GPIO Collection

配置器先完成 MCU/board/display/input/module 选择，再计算必需角色。

- 已选模块才贡献自己的角色，例如 GPS 贡献 `GPS_RX`、`GPS_TX`、`GPS_PPS`。
- 显示和输入设备贡献其 bus/driver 的必需角色；board 已声明的角色直接采用。
- 缺失角色只在交互模式下按角色逐项要求十进制 GPIO；CLI 通过现有 `--gpio ROLE=PIN`
  提供相同输入。
- 每次输入后执行 MCU 范围、方向、input-only、危险引脚确认和跨角色重复占用检查。
- 未选模块不询问、不输出其 GPIO。非交互命令缺少必需角色时失败并给出角色名。

该规则同时适用于静态 board 和 `custom` board，避免将“自定义”作为固定 board 配置缺口
的唯一补救方式。

## Saved Profile UX

交互启动时，Bash 和 PowerShell 只扫描 `firmware-builds/<valid-id>/firmware.env`。
隐藏目录、`.previous.*` 备份、临时目录、路径越界和无法通过严格 parser/validation 的文件
均不显示。Board 菜单中将有效保存项以清晰的“已保存配置”标记列出，不写入 catalog。

选择保存项后直接导入、重新验证并进入已有本地构建入口：Bash 复用
`scripts/build-profile.sh --config ...`，PowerShell 复用 `Invoke-LocalBuild`。不重复显示
硬件、模块或 GPIO 提示；失效 profile 必须在菜单扫描阶段排除或在构建前明确失败。

## Compatibility And Rollback

经典 ESP32 profile 的 catalog 值须精确复现现有 ST7789/XPT2046 wiring 和 4 MB 分区。
两个 S3 profile 使用仓库原理图的 I80、触摸、PSRAM、Flash 和分区契约；S3 ST7796 profile
若未选 GPS 则不要求 GPS 角色，选中后由配置器收集并验证用户输入。

每个 profile 使用自己的 build 目录和生成文件。回滚只恢复本任务拥有的 catalog、配置器、
配置组件、bridge 和受管依赖变更；不删除其他任务的 build/profile 目录，不重置工作树。

## Ownership

`07-21-display-input-drivers` 与 `07-21-multi-mcu-s3-board-support` 已经拥有通用显示/输入
矩阵和 S3 板级扩展。本任务只整合其已验证的 driver/config 合同以满足 IDF 6.0；发现未解决
的协议或引脚事实时记录到其任务，不复制或猜测其实现。
