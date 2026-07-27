# 资源占用诊断日志

## Goal

让串口日志能把一次本机 UI 操作与其后的内存和 CPU 占用关联起来，快速定位高占用由哪个操作及哪个任务引起。

## Confirmed Facts

- `main/idf_main.c:248` 是所有显示服务 UI action 的单一消费点，并已有稳定的 `esp_bms_idf_runtime_action_name()` 操作名。
- `main/idf_main.c:89` 已记录启动与热点服务阶段的堆状态，但没有按 UI 操作关联，也没有 CPU 指标。
- 目标为双核 `esp32`；构建配置来自 `config/sdkconfig/sdkconfig.defaults*`，当前没有启用 FreeRTOS trace facility 与 run-time stats。
- ESP-IDF 6.0.2 的 `uxTaskGetSystemState()` 能提供原始任务运行时间；项目应使用 `scripts/esp-idf-env.sh` 进入该工具链。

## Requirements

- R1: 对每个非空 UI 操作，以操作名写一条开始日志和一条结束日志。
- R2: 结束日志必须覆盖操作发起后的 1 秒窗口；若新操作提前到来，先结束前一个窗口，确保日志不丢失归因。
- R3: 开始与结束日志必须包含默认堆、内部 8-bit 堆和 PSRAM 的可用字节；结束日志还必须包含该窗口的局部内存低水位。
- R4: 结束日志必须包含归一化双核系统 CPU 忙碌率，并列出窗口内占用最高的前三个非 idle FreeRTOS 任务及其 CPU 百分比。
- R5: 诊断不得创建任务、阻塞 UI，或改变 UI action 的功能语义。任务快照容量不足或局部堆监控不可用时，保留内存日志并明确写出 CPU/局部低水位不可用。
- R6: 启用 FreeRTOS trace facility 与 run-time stats，使上述 CPU 指标可用；不启用字符串格式化统计 API。

## Acceptance Criteria

- [x] 每个 UI action 都产出带 `action=<稳定操作名>` 的 `begin` 和 `end` 资源日志。
- [x] `end` 日志包含窗口时长、默认堆/内部堆/PSRAM 的当前可用量和局部低水位。
- [x] `end` 日志包含 `cpu_busy_pct` 和至多三个 `task=<名称>:<百分比>` 项；idle task 不进入排名。
- [x] 连续操作会结束前一个采样窗口，再开始新窗口。
- [x] `config/sdkconfig/sdkconfig.defaults*` 启用实现任务 CPU 统计所需的两项 FreeRTOS 配置，且没有启用格式化统计函数。
- [x] 使用项目 ESP-IDF 6.0.2 wrapper 构建成功。

## Scope

- In scope: 本机显示 UI action 的串口诊断日志和其直接触发的异步工作窗口。
- Out of scope: Web API 每个请求的独立归因、持久化性能历史、屏幕诊断页、运行时配置开关。

## Technical Notes

- 使用静态任务快照避免诊断本身在每次操作中分配堆内存。
- 48 个任务是当前诊断快照上限；超过时仅写不可用状态，不输出不完整 CPU 排名。
