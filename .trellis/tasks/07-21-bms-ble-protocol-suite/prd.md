# BMS 多品牌 BLE 协议套件

## Goal

在单个可选 BMS 模块中完整支持 ANT、JK、JBD 和 Daly 的指定 BLE 协议，并用可复现帧测试证明流处理和字段解析正确。

## Requirements

- ANT 新/旧 BLE；JK JK02_24S/JK02_32S/JK04；JBD FF00/FF01/FF02 与认证变体；Daly D2/P81/A5 全部可选。
- 实现品牌级服务发现、认证、轮询、通知、流重组、校验和解析分派。
- 协议 parser 为与 NimBLE 解耦的纯 C；覆盖分包、粘包、损坏帧、CRC/校验、字段边界和运行时协议切换。
- 排除 UART、RS485、CAN 和 Heltec/NEEY；不做品牌级镜像裁剪。
- 只使用许可证明确的参考，Apache-2.0 来源保留 NOTICE；无许可证 JK 代码不可复制；Daly A5 没有公开帧或合法抓包时保持阻塞而非猜测。

## Acceptance Criteria

- [ ] 四品牌均出现在 TFT/Web 能力选项并能走完整 BLE 状态机。
- [ ] 所有指定协议有黄金帧和字段边界断言，损坏/分包/粘包测试通过。
- [ ] 认证、轮询和通知序列有传输层单元测试或可复现 trace。
- [ ] 来源、许可证和 NOTICE 审计无缺项，JK/A5 清洁实现证据可追踪。
- [ ] 构建报告区分 ANT 已实机、其他协议兼容未验证及后续新增实机结果。
