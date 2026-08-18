# 实施计划：低延迟 JPEG 投屏

## Sequence

1. 先向 Android `CastService` 增加发送侧时序与覆盖帧统计，保持当前采集尺寸、quality 80 和 decoder 生命周期不变，构建并在真机连续投屏 10 秒取得基线。
2. 提取可单测的等比例采集尺寸计算，更新 `configureCapture()` 以使用缩小后的 `ImageReader` / VirtualDisplay 尺寸，并保留现有 Canvas 目标拉伸。
3. 将设备 `cast-info` 中的 JPEG quality 改为 60，并使 Android 常量和 capability 单测表达同一默认值；维持协议 v3 与 20 FPS。
4. 在 `esp_bms_lvgl_bridge` 为相同逻辑尺寸的投屏帧复用 JPEG decoder；方向变化或会话退出时关闭并重新创建，保留 `RGB565_BE` 和 40 行同步提交。
5. 构建、闪存并在同一热点、同一动态画面条件下复测至少 10 秒，比较 Android 帧龄/ACK 与设备 decode/present/total/FPS 日志。

## Files

- `android-cast/app/src/main/java/com/fuckingbms/cast/CastService.kt`: 缩小采集源、不可变帧时间戳和发送侧聚合日志。
- `android-cast/app/src/main/java/com/fuckingbms/cast/CastProtocol.kt`: 对齐 JPEG 默认质量常量。
- `android-cast/app/src/test/java/com/fuckingbms/cast/CastCapabilitiesTest.kt`: 覆盖 quality 60 和采集尺寸计算的覆盖/比例边界。
- `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c`: 在 capability 响应中声明 quality 60。
- `components/esp_bms_lvgl_bridge/esp_bms_lvgl_bridge.c`: 管理会话内 JPEG decoder 生命周期。

## Validation

用户要求验收由其显式触发。在收到“开始验收”前，不运行下列构建、测试、GitNexus change detection、刷机、日志采集或硬件复测，也不提交代码；实施阶段只做必需的静态影响分析。

```bash
RUN_TESTS=1 ./scripts/build-android-cast.sh
./scripts/run-host-selftests.sh
./tests/configurator_selftest.sh
./scripts/build-profile.sh --config firmware-builds/cast-s3-build/firmware.env
node .gitnexus/run.cjs detect-changes -r esp32BMSGPS
```

完成固件构建后，使用项目固定 RFC2217 路径闪存 S3，并检查 10 秒动态投屏、方向切换、主动停止和断线恢复。记录基线与优化后的 Android `[cast]` 帧龄/ACK 日志，以及固件 `[cast]` 的 bytes、decode、present、total、fps。

## Risks

- 用户已有 `MainActivity`、通知和构建脚本的未提交 UI 改动不属于本任务；实现时只修改列出的投屏核心区段并保留它们。
- 真机若 quality 60 导致文字不可辨识，则只回滚质量值到 80；不以 WebP 或新编解码器替代。
- 若实际瓶颈仍是 Wi-Fi 接收而非编码/解码，日志会显示 `total - decode - present` 偏高；本任务不靠增加 FPS 或排队掩盖该网络问题。
