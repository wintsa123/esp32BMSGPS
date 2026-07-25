# 实施计划

1. 阅读 runtime、BMS BLE、控制器 BLE、LVGL bridge 与配置器层级规范；对将修改的每个符号执行 GitNexus upstream 影响分析。
2. 在 runtime 中建立唯一待扫描所有者/启动入口，并将 BMS、控制器的扫描发起、GAP 完成和 host sync 统一接入该仲裁规则。
3. 为扫描交接补充窄范围自检或可测试状态覆盖，确保没有依赖 BMS tick 优先级。
4. 用与各驱动调用一致的预处理开关包裹 bridge 的配置对象声明。
5. 在配置器自检中加入旧 ESP32 + `ili9341-2p8-spi` + XPT2046 的成功配置与生成结果检查；必要时修复 Bash/PowerShell 的展示或筛选逻辑。
6. 运行格式/差异检查、配置器自检、目标 ESP-IDF 构建及相关组件自检；检查编译日志中两项告警均消失。
7. 运行 GitNexus 变更检测，审查受影响的流程；保留用户既有的未提交改动。
8. 从 ESP-IDF `flasher_args.json` 发布完整烧录包，覆盖 Bash 与 PowerShell 构建路径；回归验证应用、bootloader、分区表和合并镜像的地址。
