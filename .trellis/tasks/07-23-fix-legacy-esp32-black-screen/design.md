# 技术设计

## Root Cause And Boundary

硬件 catalog 已声明 legacy TFT 背光接在 GPIO 21，但 profile 生成阶段只收集普通 SPI 的
MOSI、SCLK、CS、DC 四个角色。`TFT_BACKLIGHT` 被遗漏后，生成器将其作为缺失的可选值，输出
`GPIO_NUM_NC`。显示桥接层按既有约定跳过 NC 背光，不应在该层引入板级硬编码。

## Change Design

1. 在 `start.sh` 和 `start.ps1` 的普通 SPI 角色收集分支中，仅当板卡 catalog 声明
   `TFT_BACKLIGHT` 时，将其加入收集列表。
2. 保持 `generate-hardware-config.py` 的可选背光解析行为：收集到的值生成 GPIO，未收集的值
   仍生成 `GPIO_NUM_NC`。
3. 扩展配置器自测，断言 legacy profile 的环境文件和生成头包含 GPIO 21；保留无背光 profile
   的 NC 回退断言。
4. 重新生成 legacy profile、构建并直接经 COM3 烧录。设备启动日志是运行时验收依据。

## Compatibility And Rollback

- 变更只影响 catalog 明确声明背光 GPIO 的普通 SPI profile。
- 其他板卡没有 `TFT_BACKLIGHT` 时仍走原有 NC 路径。
- 若实机回归失败，回滚这三个配置器相关文件即可恢复原行为；不擦除 NVS 或 bootloader。
