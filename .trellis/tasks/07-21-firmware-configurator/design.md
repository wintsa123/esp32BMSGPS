# 配置器技术设计

## 边界

`start.sh` 和 `start.ps1` 是两个独立实现，读取同一份只含
`KEY=VALUE` 的 catalog 与用户配置。`start.cmd` 只负责转发到
`start.ps1`。所有生成结果位于被忽略的
`firmware-builds/<profile>/`，根 `sdkconfig`、`partitions.csv` 和现有
默认构建入口保持不变。

catalog 使用目录发现：`firmware/catalog/{mcu,board,display,input,module}`。
描述文件不含 shell/PowerShell 代码；解析器只接受已知键、单行值和受限
标识符。用户配置也是同一格式，经过重复键、未知键、枚举、依赖、冲突、
能力及 GPIO 校验后才会写入 profile。

## 数据流

`configure` 接收显式参数或交互菜单 -> 生成临时用户配置 -> `validate`
解析 catalog 与配置 -> 按固定键序写 `normalized.env` -> 生成
`sdkconfig.defaults`、`generated/profile.cmake`、模块注册表、分区副本和
`report.txt`。`build-local` 只接收已验证 profile，使用独立 build
目录调用 ESP-IDF。

`generated/profile.cmake` 通过项目私有的 `ESP_BMS_PROFILE_MAIN_REQUIRES`
向 CMake 提供组件闭包，避免与 ESP-IDF 的内部变量冲突。它是组件裁剪的唯一
CMake 输入。当前单体
`esp_bms_idf_runtime` 尚未拆分时，生成器把其标记为 `legacy-runtime`
而不是伪造模块排除成功；下一子任务将该标记替换为真实的 selected
component closure，再以 map/symbol 证明未选模块缺席。

## 初始 catalog

- `esp32-wroom-32e-legacy`：当前 WROOM-32E、ST7789、XPT2046、4 MB
  双 OTA 分区，作为可构建控制配置。
- `esp32s3-wroom-1-n16r8-i80`：记录 S3 N16R8 默认引脚和 16 MB 分区，
  但在板级驱动子任务完成前只允许生成与校验，不允许声称可构建。
- 模块包含 BMS、GPS、控制器、音频、网络、OTA、投屏，声明其依赖与冲突。

## 安全与回滚

解析期间不执行配置内容、不得 `source`、`eval`、dot-source 或动态调用。
profile 名称只允许 `[A-Za-z0-9][A-Za-z0-9_-]{0,63}`，所有输出路径都必须
位于 `firmware-builds/` 下。生成先写临时目录，再原子替换 profile；失败
不修改已有 profile。删除 profile 是用户的手工文件操作，不提供递归删除
命令。
