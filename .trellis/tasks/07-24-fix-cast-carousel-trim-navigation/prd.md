# 修复投屏裁剪与轮播导航

## Goal

使固件配置器的投屏模块选择准确控制投屏运行时能力与轮播入口，避免未选择投屏的固件仍出现投屏页面，或在用户切换轮播页时被投屏状态强制跳转。

## Confirmed Facts

- 用户在 ESP32-WROOM-32E legacy 固件中观察到未选择投屏仍显示投屏页，且投屏页激活后轮播切换会回到投屏页。
- 当前发布配置 `firmware-builds/esp32-wroom-32e-legacy/generated/modules.env:1` 将 `cast` 记录为已选模块。
- `start.sh:941-988` 只为 audio、bms、controller、gps、network 与 ota 输出编译期特性开关，没有投屏特性开关。
- `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:9401-9421` 无条件创建投屏页；`page_from_scroll_x` 在速度页不可用时无条件返回投屏页（8812-8818）。
- `main/idf_main.c:356-375` 依据运行时 `cast_active` 直接切换到投屏页，未受构建配置约束。
- `components/esp_bms_network/esp_bms_network.c:275-295` 无条件注册 `/cast` WebSocket 路由。

## Requirements

- R1: 未选择 `cast` 的配置不得将投屏能力、投屏轮播入口或 `/cast` WebSocket 路由编入运行固件。
- R2: 未选择 `cast` 时，轮播页仅在实际可用页面间切换；速度页不可用也不得将手势解析为投屏页。
- R3: 选择 `cast` 时，保留现有投屏二维码、WebSocket 投屏能力和投屏激活时的页面切换行为。
- R4: 配置器生成产物必须能证明 `cast` 是否被选择，并覆盖显式未选择投屏的配置场景。

## Acceptance Criteria

- [ ] 以不含 `cast` 的模块集合生成 legacy 配置后，生成的模块清单和 CMake 特性开关均表示投屏已禁用。
- [ ] 不含 `cast` 的固件不会创建或可导航到投屏页，也不会注册 `/cast` 路由。
- [ ] 不含 `cast` 的固件在 GPS/控制器速度页不可用时，轮播仍停留或回退到有效非投屏页面。
- [ ] 含 `cast` 的配置仍可进入投屏页并保持现有投屏会话页面切换行为。
- [ ] 自动化测试覆盖有/无投屏的配置与轮播页映射。

## Out Of Scope

- 不改变投屏协议、二维码载荷、Android 投屏客户端或其他模块的选择语义。
