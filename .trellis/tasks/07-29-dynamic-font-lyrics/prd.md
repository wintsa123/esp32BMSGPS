# 音乐页动态中文字库（延期）

## Decision

- 用户选择不安装或运行 Android companion app，并且暂不显示歌名。
- 因此本任务不实现 SD 卡、中文字库、LVGL binfont、歌词、音乐标题 BLE 协议或 board profile 改动。
- 无 companion app 时，BLE 连接本身没有 Android 正在播放歌曲的元数据通道；BLE HID Consumer Control 只可发送下一首和音量等媒体按键，不能传递歌名。

## Deferred Hardware Notes

- 经典 ESP32 的 TF SPI：MOSI=GPIO23、MISO=GPIO19、SCK=GPIO18、CS=GPIO5。GPIO18 原与 GPS TX 冲突；若恢复本任务，已确认改线方案为 ESP GPIO1 接 GPS RX，并使 profile 使用 `GPS_TX=1`。
- 慧勤智远 ESP32-S3 N16R8 V1.0 的资料来源：`/vol1/1000/project/慧勤智远 ESP32-S3 N16R8 V1.0-3.5寸电容屏开发套件`。TF SPI2 为 MOSI=GPIO21、SCK=GPIO47、MISO=GPIO48、CS=XL9555 P0.2；XL9555 I2C 为 SCL=GPIO1、SDA=GPIO2。
- 当前项目的 LVGL 9.5 有 `lv_binfont_create()` 和 FATFS 文件系统驱动，但 FATFS 目前关闭。恢复歌名显示时，应采用预生成的 24 px LVGL binfont，而非运行时生成中文字形。

## Out Of Scope

- Android companion app、网易云歌名读取、歌名显示、中文字体、SD 字库、歌词及 BLE 歌名协议。

## Open Question

- 是否另建独立任务，使用 BLE HID Consumer Control 实现免 App 的下一首和音量控制？
