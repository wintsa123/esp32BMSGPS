# 旧 ESP32 基线与兼容构建

## Goal

在任何组件拆分或多目标生成生效前，建立可重复比较的经典 ESP32-WROOM-32E 控制基线，并保证最终默认构建入口、GPIO、4 MB 分区和关键运行行为兼容。

## Requirements

- 记录基准 commit、工作区差异指纹、ESP-IDF/工具链版本和依赖锁哈希，避免把脏工作区误当成某个提交的干净产物。
- 保存 `sdkconfig.defaults`、当前实际 sdkconfig 关键项、`partitions.csv`、组件图、map、size 和最终镜像哈希。
- 用现有 simulator 固定关键设置页/仪表状态；新增预览只能放根 `preview/` 且不进入版本控制。
- 使用固定 RFC2217 链路记录冷启动、显示、触摸、ANT BMS、GPS、Setup AP/Web 和 OTA 基线；无法接入的外设必须明确标为未验证。
- 原 `./scripts/esp-idf-env.sh build` 在迁移期间和完成后默认构建 legacy profile，不修改根 `sdkconfig` 作为生成器输出。
- 保持当前 GPIO、4 MB 双 OTA 分区、Setup AP 凭据策略和二维码字段。

## Acceptance Criteria

- [ ] 基线目录包含来源身份、工具版本、配置/分区哈希、组件图、size/map/镜像哈希和测试结果。
- [ ] 默认构建在任务开始状态成功；失败时有可复现日志且不会伪造基线。
- [ ] RFC2217 日志覆盖可用的启动与外设检查，未连接硬件均有准确状态。
- [ ] 最终 legacy profile 与基线的 GPIO/分区/关键 UI/API/启动状态差异经过逐项审查。
- [ ] 用户在任务前已有的未提交文件未被重置或覆盖。
