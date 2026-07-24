# 执行清单

1. [ ] 扩展显示/触摸 catalog 元数据，加入 MCU、总线、组件包、固定版本、头文件、初始化函数和 GPIO 角色；校验唯一 `none`。
2. [ ] 统一 Bash/PowerShell 的 MCU、总线和目标芯片筛选；移除 custom 流程中的设备名称/总线手填；保证触摸 `none` 最后。
3. [ ] 扩展配置校验与硬件头生成，按驱动选择输出宏、头文件和 GPIO 要求；`none` 不生成触摸 GPIO。
4. [ ] 生成 profile 专属 manifest、依赖锁文件位置和 CMake 依赖闭包；保留仓库现有组件复用路径。
5. [ ] 按首批已适配驱动更新 LVGL bridge 条件编译；对未完成桥接的 catalog 项目在校验阶段拒绝构建。
6. [ ] 补充 Bash/PowerShell 菜单排序、MCU 筛选、生成宏、profile manifest 和跨 profile 隔离测试。
7. [ ] 运行 `tests/configurator_selftest.sh`、Python 编译检查、CMake 早期依赖检查和 GitNexus `detect-changes`；可构建固件改动完成后尝试 RFC2217 烧录。

## Rollback points

- catalog/schema 校验失败时只回滚新增记录，不影响既有 board profile；
- profile 生成失败时保留旧 profile 备份，删除临时目录；
- bridge 编译失败时恢复对应控制器选择为未适配状态，不回退无关配置器改动。
