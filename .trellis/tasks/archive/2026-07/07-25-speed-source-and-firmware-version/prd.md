# 速度来源裁剪与固件版本展示

## 目标

让速度仪表设置中的“速度来源”始终通过现有三级选择页设置，并只显示当前 firmware profile 实际编译启用的来源；同时确认手机 Web 页的固件版本准确来自用户配置的构建版本。

## 已确认事实

- 设置根页仅在 `ESP_BMS_FEATURE_GPS || ESP_BMS_FEATURE_CONTROLLER` 时显示“速度仪表”（`components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:2311`），因此两个模块都关闭时一级入口已正确隐藏。
- 三级速度来源页当前无条件列出 GPS 和控制器（`components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:4359`），导致 GPS 已编译关闭的 profile 仍显示 GPS。
- 当前 `esp32-wroom-32e-legacy` profile 的 GPS 为关闭、控制器为开启（`firmware-builds/esp32-wroom-32e-legacy/generated/profile.cmake:9-10`）。
- 构建版本由 `FIRMWARE_VERSION` 生成 `ESP_BMS_PROFILE_FIRMWARE_VERSION`，运行时复制至 snapshot，`/api/status.version` 和手机 Web 页读取该字段（`scripts/generate-hardware-config.py:82-84,213`；`components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:3353-3356,2017-2021`；`main/web/index.html:339`）。当前 profile 中三处均为 `1.3.2`。
- 数字与英文为 ASCII，不需要中文点阵字库。

## 需求

### R1. 速度来源的三级选择

- 从“速度仪表”详情点击“速度来源”时，进入现有“速度来源”三级列表页。
- 列表只显示由 `ESP_BMS_FEATURE_GPS` 和 `ESP_BMS_FEATURE_CONTROLLER` 启用的来源。
- 仅启用一个来源时，保留该唯一来源的三级页和现有选中状态；不新增自动保存、默认值或 NVS 迁移规则。
- 两个来源都关闭时，不显示设置一级的“速度仪表”入口，且不得产生空白行或不可达点击路径。
- 运行时 GPS 可用性继续只影响 GPS 项是否可点击；编译期裁剪优先决定该项是否存在。

### R2. 手机中的固件版本

- 不新增字库或版本常量。保留现有 `FIRMWARE_VERSION -> generated header -> runtime snapshot -> /api/status -> Web` 数据链。
- 对当前配置执行可重复的静态/构建验证，确认用户填写的 ASCII 版本在产物中一致；若设备显示不同，结论应指出刷写镜像与当前 profile 版本不一致，而非字库问题。

## 验收标准

- [ ] GPS 和控制器均启用时，三级页显示 GPS、控制器两项。
- [ ] 仅 GPS 启用时，三级页只显示 GPS；仅控制器启用时，三级页只显示控制器。
- [ ] GPS 与控制器均关闭时，设置根页不显示“速度仪表”。
- [ ] 现有选择、返回导航、运行时 GPS 不可用的禁用态和控制器离线回退均保持。
- [ ] `FIRMWARE_VERSION`、生成硬件头、运行时 `/api/status.version` 路径均验证为相同 ASCII 值。

## 范围外

- 不修改手机 Web 的速度来源下拉框、固件 OTA 流程、NVS 格式、版本输入校验或 TFT 字库。
- 不刷写硬件；仅构建与主机验证。
