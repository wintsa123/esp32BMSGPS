# 实施计划

1. 在改动任何函数前执行 GitNexus upstream impact，重点检查 runtime HTTP 分发、网络
   驱动转发、模块注册表和 Setup AP 调度；完成 network/OTA 的实现、CMake 和 catalog
   对齐，并保留 cast 的 legacy 标记。
2. 对齐 Bash/PowerShell 的全部 feature 和 profile closure 生成规则；修复 Windows 入口
   与包装脚本的命令/语言传递差异，不改变非本任务的 schema 或退出码。
3. 增加 `zh`/`en` 进程内文案映射、`--lang` 校验和无参数语言菜单。每次交互均重新选择，
   不添加持久化文件；所有生成输出保持 ASCII 与字节稳定。
4. 扩展 `tests/configurator_selftest.sh`，覆盖中文默认、英文覆盖、交互语言选择、非法语言、
   语言不写入 profile，以及 Bash/PowerShell 在 network/OTA on/off 的内容一致性。
5. 执行配置器自检、legacy/network-off/ota-off 的 ESP-IDF 构建与闭包、archive/map/ELF/
   资源检查；运行 `git diff --check`、GitNexus `detect-changes`。固件行为变更后按
   RFC2217 固定流程尝试刷写并检查 Setup AP、二维码凭据和 OTA 路径。

## 风险检查

| 风险 | 证据/回退 |
| --- | --- |
| 网络迁移遗漏 HTTP 或首页资源 | legacy profile 路由和嵌入资源检查；回退组件边界改动 |
| OTA off 仍可达更新代码 | component 闭包与 map/ELF/archive 搜索；回退 OTA feature 条件 |
| Bash/PowerShell 漂移 | golden profile 内容比较；回退对应生成规则 |
| 本地化破坏自动化 | `--lang en` 兼容断言与固定退出码；回退文案/解析层 |
| 语言意外持久化 | profile 与工作树中无偏好文件；仅保留进程变量 |
