# 技术设计：ESP32-S3 显示校正

## 边界与数据流

`firmware/catalog/display/st7796u-i80.env` 是此 S3 板型唯一的面板配置来源。配置生成器将其宽高、色序和默认旋转写入 `esp_bms_profile_hardware.h`；`main/idf_main.c` 先用默认版本处理 NVS 迁移，再把配置传给 `esp_bms_lvgl_bridge`。

HAL 在横屏模式调用面板 `swap_xy` 和 `mirror`，并同步设置 LVGL 分辨率及 GT1151 的坐标变换。原厂例程证明 ST7796 的横屏面板标志是 `swap_xy=1`、`mirror_x=0`、`mirror_y=0`；通用 `LANDSCAPE` 的 `mirror_x=1` 因此需要独立的面板 X 轴补偿，不能通过修改运行时 `ROTATION` 实现。

## 决策

- 保持物理坐标 `WIDTH=320`、`HEIGHT=480`。在横屏模式下 HAL 已产生正确的 `480x320` 逻辑分辨率。
- 将 ST7796U 设为原厂值：`RGB_ORDER=BGR`、`I80_SWAP_COLOR_BYTES=0`、`INVERT_COLOR=1`。
- 在显示目录加入仅供面板使用的 `PANEL_MIRROR_X`，由配置生成器写入桥接配置。桥接层在初始化和运行时方向切换时仅异或面板 `mirror_x`；GT1151 以 `SWAP_XY=1`、`MIRROR_X=1` 补偿最终横屏画面的上下方向。

## 兼容与回滚

显示补偿只对 `st7796u-i80` 生效。若实机结果异常，回退对应面板镜像项即可；不更改 NVS 格式。静态仪表的 `320x240` 布局问题不在本次修改范围。
