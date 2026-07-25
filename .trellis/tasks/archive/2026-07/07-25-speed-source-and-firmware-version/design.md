# 技术设计

## 边界与数据流

`settings_show_controller_detail()` 已经把详情页的“速度来源”行绑定到 `settings_show_speed_source_picker()`。保持此导航链路，只把三级页的固定标签数组替换为带来源枚举值的、受 `ESP_BMS_FEATURE_GPS` / `ESP_BMS_FEATURE_CONTROLLER` 预处理条件筛选的静态数组。

选择回调从数组元素读取真实 `esp_bms_speed_source_t`，不再假定显示索引等于枚举值；这样 GPS 关闭时唯一的控制器项仍会提交 `ESP_BMS_SPEED_SOURCE_CONTROLLER`，而非错误的索引 `0`。

现有根页条件保持不变：两个 feature 均关闭时不会进入速度仪表详情。运行时 GPS 模块状态仍只控制已编译 GPS 选项的可用性。

## 固件版本结论

不修改实现。版本的唯一来源已经是 profile `FIRMWARE_VERSION`：配置器写入 `firmware.env`，生成器写入 `ESP_BMS_PROFILE_FIRMWARE_VERSION`，runtime 初始化复制到 snapshot，HTTP 状态 API 和 Web 读取该 snapshot。ASCII 的英文/数字不经过 TFT 中文字库。

当前 profile 值为 `1.3.2`。若手机显示其他值，设备烧录的镜像不是这个 profile 的最新产物，验证应比较即将刷写镜像对应的生成头和 `/api/status` 返回值。

## 风险与回退

GitNexus 对 `settings_show_speed_source_picker()` 报告 2 个直接调用方、10 条流程、3 个模块，风险 CRITICAL；对 `settings_show_controller_detail()` 报告 5 个直接调用方、13 条流程、3 个模块，风险 CRITICAL。改动仅限数组和选择映射，并使用现有 simulator feature matrix 覆盖导航与能力组合。

单文件修改可直接回退；不引入新的 API、结构体、NVS 键或构建依赖。
