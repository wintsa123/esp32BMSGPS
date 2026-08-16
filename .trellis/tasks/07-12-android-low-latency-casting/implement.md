# 实施清单：Android 低延迟镜像投屏

1. 对 `esp_bms_idf_runtime_http_cast_connected`、`esp_bms_idf_runtime_http_cast_ws_handler`、`esp_bms_idf_runtime_stop_cast`、`esp_bms_idf_runtime_tick`、显示服务命令处理、显示桥 RGB565 写入和 `app_main` 执行 GitNexus upstream 影响分析；若为 HIGH/CRITICAL，先报告再改动。
2. 在显示桥复用 `esp_lv_adapter` dummy-draw：添加进入/退出投屏和真实分辨率查询接口；将最大 `64x64` RGB565 块写入改为固定块字节序转换加同步 `dummy_draw_blit`，删除对 WebSocket 接收缓冲异步生命周期的依赖；命令入队后立即唤醒显示任务，避免逐块轮询延迟。
3. 在显示服务增加投屏进入、临时旋转和恢复命令。投屏状态仅运行 cast 命令，释放 LVGL 根页面、缓存、动画和输入处理；恢复时重建轮播 UI 并恢复全量刷新。
4. 升级 runtime/WebSocket 到 v2：真实尺寸能力、帧边界旋转、会话就绪门、严格坐标/长度校验、断线/超时/协议失败恢复；仅实现只读镜像，不新增反向触控消息。临时旋转不得保存到 NVS。
5. 在 `idf_main` 和 module registry 按投屏状态边沿停止/恢复当前 profile 的 controller BLE、GPS 与普通 tick；GPS 恢复使用现有初始化路径重建 UART/PPS，不假定存在 start API；保留显示、AP、HTTP/WebSocket、主循环和看门狗。
6. 更新 Android capability 解析、方向选择、源图像旋转/拉伸、帧边界 encoder 重建与 v2 消息；补充颜色、方向和差分单测。
7. 运行固件/Android 自测、构建、GitNexus `detect-changes`，并通过 RFC2217 刷入 `cast-s3-build` 真机验证竖横屏、色块、断线、退出、恢复与 heap 日志。

## 验证命令

```bash
./scripts/build-profile.sh --config firmware-builds/cast-s3-build/firmware.env
RUN_TESTS=1 ./scripts/build-android-cast.sh
node .gitnexus/run.cjs detect-changes -r esp32BMSGPS
git diff --check
```

真机固件变化完成后，按项目 LAN RFC2217 流程执行一次 flash 和 monitor，并记录实际 profile、方向、色块和恢复结果。

## 风险点与回滚

- 不改动用户现有的 `AGENTS.md`、`CLAUDE.md` 或无关工作树改动。
- DMA 完成等待不能在 ISR 以外的错误上下文调用；所有 panel/LVGL 操作仍由显示服务任务串行执行。
- `esp_bms_lvgl_bridge_write_rgb565` 的 GitNexus upstream 影响为 HIGH（显示服务和 simulator 均受影响）；修改后必须覆盖颜色、DMA 完成和恢复路径。
- 服务恢复失败只记录并保留可重试的正常启动路径，不能让设备停在 dummy-draw 或投屏方向。
- 不允许完整 RGB565 帧、依画面尺寸分配或投屏期间继续普通 LVGL 刷新。
