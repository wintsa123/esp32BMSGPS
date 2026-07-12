# Design

## Boundaries

- `esp_fardriver_protocol`: 纯 C、无 ESP-IDF/LVGL 依赖，唯一负责帧校验、地址映射和遥测解码。
- `esp_bms_idf_runtime`: 拥有 NVS、NimBLE 连接/扫描、页面数据源门控和 snapshot 投影。
- `esp_bms_lvgl_ui`: 只拥有设置交互、动态页结构、稳定页面只读状态和标签渲染。
- `idf_main`: 每 50 ms 在 LVGL 锁内取 action 与稳定页面状态，在锁外驱动 runtime；不从滚动回调进入 BLE。

## Data Flow

`FFEC notify -> protocol parser -> runtime cached telemetry/valid bits -> dashboard snapshot -> LVGL fixed buffers`

`LVGL action -> runtime settings -> NVS -> snapshot -> dynamic page rebuild on dashboard return`

`LVGL stable page/settings visibility -> main loop -> runtime active source -> BMS poll / FarDriver projection gate`

## Protocol Contract

- 帧长固定 16 字节，`B0=0xAA`，`B14..15` 为 CRC。
- 紧凑布局直接使用 EKSR 消息索引；扩展布局使用索引低 6 位和 55 项地址表解析 12 字节数据块。
- 多字节状态字段按参考实现的高字节优先布局解码；`0xD0` 参数块按 FarDriver 数据结构的小端字字段解释。
- 解析失败不修改上次缓存；断线统一清除 snapshot 有效位。

## Page Lifecycle

- UI 保存 `page`、`scrolling` 和 `settings_visible`，通过只读 API 返回稳定数据源枚举。
- 页面滚动结束只更新 `page`；main loop 下一拍调用 runtime 切换门控。
- 用户滚动结束时，先将原始吸附页夹到当前稳定 `page` 的相邻一页，必要时动画回到限幅目标，再刷新延迟快照。`move_to_page()` 在动画前更新 `page`，因此程序化跳页不会被手势限幅。
- 页面显示开关改变时不在设置详情中重建首页，返回 dashboard 时使用最新 snapshot 创建两页或三页。
- 页面显示开关同时约束控制器连接生命周期：关闭时停止扫描并断连，开启时按保留的绑定 MAC 自动重连；UI 仅在主开关开启时显示连接和蓝牙选择行。

## Controller Dashboard Layout

- 控制器页使用固定像素几何以匹配 320x240 与 240x320 两种 TFT 方向，不使用会重排主视觉的 Flex/Grid。
- 页面外框内分为主数据区和辅助区。横屏主数据区按约 2:1 分为车速与蓝色挡位，辅助区为一行四列；竖屏按上下分为车速与蓝色挡位，辅助区为 2×2。
- 主数字、单位、辅助标题、辅助值和辅助单位使用独立标签，避免同一多行标签无法表达字号、颜色和垂直间距层级。
- `SPEED` 和 `GEAR` 是主区域固定标题；车速按整数显示并与单位组成同一水平行。
- 辅助标题、数值和单位组成同一水平行；竖屏使用 2×2 提供足够的可读字号。
- 辅助 ASCII 统一使用内置 Montserrat 14；`CTRL` / `MOTOR` 是温度单元格左上角标题，值与单位位于下方。车速单位标签按 Montserrat 24 实际字形宽度保留至少 65px，不依赖裁切隐藏越界。
- 动态对象仍只保存车速、挡位和四个辅助数值标签；静态标题、单位与边框不进入快照更新路径。
- 在线、离线和字段无效状态只替换动态数值缓冲，不创建/删除对象，因此布局不随文本变化。
- ST7789 面板使用 RGB 元素顺序；BGR 会把设计蓝色在当前真机上显示成橙色。

## Compatibility

- 新 NVS 字段逐键读取；只有已存在字段取值非法时回退该字段，不让旧配置整体失效。
- BMS 现有绑定、手机广播和 Setup AP 行为保持原样。
- 最大连接数同步更新 defaults 与当前 sdkconfig，避免本地构建配置漂移。

## Risk Control

- CRITICAL 滚动收尾只增加稳定状态投影，不直接调用 runtime。
- HIGH 页面映射集中在两个索引函数，所有跳页继续复用 `move_to_page`。
- BLE 复用现有 host，不引入第二 NimBLE 初始化路径；扫描由单一 owner 串行化。
