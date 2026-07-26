# 执行计划：ESP32-S3 显示校正

1. 以原厂 `11_touch` 例程校验 ST7796 I80 的方向和 GT1151 坐标链路；RGB565 字节序必须以本项目 LVGL 到面板的实机链路验证为准。
2. 为显示桥接配置增加面板专用的 X 镜像字段，配置生成器默认生成关闭值；仅为 ST7796U S3 面板启用它。
3. 将该镜像与运行时旋转组合后仅写入面板；将 GT1151 原始 X 轴镜像与 `SWAP_XY` 组合以修正最终横屏的上下触摸方向。对该开发板使用 `RGB_ORDER=BGR`、`INVERT_COLOR=1`、`I80_SWAP_COLOR_BYTES=0`，并在 `custom_draw_bitmap` 中先交换 RGB565 字节、再提交位图。
4. 更新配置器自检，验证生成的面板镜像、RGB 顺序、字节交换和反相配置；构建、烧录并在该板实机检查颜色、画面和触摸。用独立红、绿、蓝色块记录通道映射，不以“整体颜色改善”代替三原色验收。

## 验证

- `bash tests/configurator_selftest.sh`
- S3 目标构建
- S3 实机烧录、串口日志和触摸/颜色/纵向镜像观察
- `node .gitnexus/run.cjs detect-changes --scope all`，并将同一工作区的其他任务改动排除在本任务范围外

## 风险与回滚

面板方向只能由实机安装方向最终确认。若选错，先比对原厂 MADCTL 标志与当前 `panel_rotation_flags` 输出；不得再以面板 Y 镜像或运行时方向替代硬件参考。颜色问题先确认 `custom_draw_bitmap` 的交换在 `esp_lcd_panel_draw_bitmap()` 之前发生，再调整通道配置；不得依赖 adapter flush 路径推断交换是否生效。

## Bug Analysis: S3 面板镜像补偿轴判断错误

### 1. Root Cause Category
- **Category**: E - Implicit Assumption
- **Specific Cause**: 在未核对同板原厂例程的情况下，将“画面上下镜像”直接映射为原生 `mirror_y`；横屏已交换 X/Y 轴，实际需要补偿 `mirror_x`。

### 2. Why Fixes Failed
1. 首次修复：仅增加 `PANEL_MIRROR_Y=1`，它在横屏状态翻转了错误的逻辑轴，导致画面与既有触摸映射失配。
2. 第二次修复：只复原面板方向，未按 ESP LCD Touch 的“镜像后交换”顺序同步触摸原始 X 轴，导致最终屏幕 Y 坐标反向。

### 3. Prevention Mechanisms
| Priority | Mechanism | Specific Action | Status |
| --- | --- | --- | --- |
| P0 | Hardware reference | 对照同板原厂 LCD、触摸和 I80 初始化参数后再改目录配置 | DONE |
| P1 | Regression test | 断言生成的 S3 头文件包含面板 X 补偿、GT1151 原始 X 镜像和原厂颜色参数 | DONE |

### 4. Systematic Expansion
- **Similar Issues**: 所有带 `swap_xy` 的显示目录都不能从用户描述直接推断原生镜像轴。
- **Design Improvement**: 面板安装补偿必须与触摸变换保持独立，并在配置层显式表达。

### 5. Knowledge Capture
- [x] 已更新硬件构建规范中的面板专用补偿契约。

## Bug Analysis: ESP32-S3 开发板 RGB565 字节交换时机

### 1. Root Cause Category
- **Category**: Integration Boundary
- **Specific Cause**: 将 RGB565 字节交换的正确性寄托在 adapter 的 flush 路径，无法保证交换发生在 `esp_lcd_panel_draw_bitmap()` 之前。

### 2. Verified Milestone
1. 对 `esp32s3-n16r8-st7796u-gt1151` 的 ST7796U 屏幕，`custom_draw_bitmap` 内先调用 `lv_draw_sw_rgb565_swap()`、再调用 `esp_lcd_panel_draw_bitmap()`，已显著改善实机颜色。
2. 字节交换验证时目录参数为 `RGB_ORDER=RGB`、`INVERT_COLOR=1`、`I80_SWAP_COLOR_BYTES=0`；红色显示为蓝色、蓝色显示为黄色，因此当前将色序改为 `BGR` 验证。
3. BGR 镜像已构建并经 RFC2217 烧录；该结论仍需实机三原色验收。

### 3. Prevention Mechanism
| Priority | Mechanism | Specific Action | Status |
| --- | --- | --- | --- |
| P0 | Explicit submit boundary | 把字节交换放在同一回调中，紧邻 `esp_lcd_panel_draw_bitmap()` 调用 | DONE |
| P1 | Board-specific color check | 在此开发板上用红、绿、蓝色块分别确认通道映射 | TODO |

### 4. Scope Guard
- 这不是通用 ESP32-S3 或 I80 面板规范。其他板型必须各自验证其 DMA、面板驱动和 LVGL 缓冲区链路。
