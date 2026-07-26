# 技术设计：ESP32-S3 显示校正

## 边界与数据流

`firmware/catalog/display/st7796u-i80.env` 是此 S3 板型唯一的面板配置来源。配置生成器将其宽高、色序和默认旋转写入 `esp_bms_profile_hardware.h`；`main/idf_main.c` 先用默认版本处理 NVS 迁移，再把配置传给 `esp_bms_lvgl_bridge`。

HAL 在横屏模式调用面板 `swap_xy` 和 `mirror`，并同步设置 LVGL 分辨率及 GT1151 的坐标变换。原厂例程证明 ST7796 的横屏面板标志是 `swap_xy=1`、`mirror_x=0`、`mirror_y=0`；通用 `LANDSCAPE` 的 `mirror_x=1` 因此需要独立的面板 X 轴补偿，不能通过修改运行时 `ROTATION` 实现。

## 决策

- 保持物理坐标 `WIDTH=320`、`HEIGHT=480`。在横屏模式下 HAL 已产生正确的 `480x320` 逻辑分辨率。
- `esp32s3-n16r8-st7796u-gt1151` 的 ST7796U 当前实机配置为 `RGB_ORDER=BGR`、`I80_SWAP_COLOR_BYTES=0`、`INVERT_COLOR=1`。不通过目录配置交换 RGB565 字节；由已注册的 `custom_draw_bitmap` 在调用 `esp_lcd_panel_draw_bitmap()` 前执行 `lv_draw_sw_rgb565_swap()`。
- 在显示目录加入仅供面板使用的 `PANEL_MIRROR_X`，由配置生成器写入桥接配置。桥接层在初始化和运行时方向切换时仅异或面板 `mirror_x`；GT1151 以 `SWAP_XY=1`、`MIRROR_X=1` 补偿最终横屏画面的上下方向。

## 实机颜色里程碑（2026-07-26）

`custom_draw_bitmap` 是该开发板的字节交换边界：回调直接对本次 `color_map` 的 RGB565 像素执行 `lv_draw_sw_rgb565_swap()`，随后立刻调用 `esp_lcd_panel_draw_bitmap()`。因此交换一定发生在面板提交前，不再依赖 adapter 内部 flush 路径是否处理 I80 字节序。

该做法已让颜色显著改善。此前 `RGB_ORDER=RGB` 时，红色实机显示为蓝色、蓝色实机显示为黄色；因此保留该字节交换边界并将面板色序改为 `BGR`。BGR 镜像已构建并经 RFC2217 烧录，后续以该板的实机 RGB 色块确认通道映射；不得把此回调或上述颜色参数当作所有 S3 / I80 面板的通用默认值。

## 兼容与回滚

显示补偿和本次颜色结论只面向 `esp32s3-n16r8-st7796u-gt1151` 的 ST7796U 屏幕。若实机结果异常，先以 RGB 色块确认通道映射，再回退对应面板配置；不更改 NVS 格式。静态仪表的 `320x240` 布局问题不在本次修改范围。
