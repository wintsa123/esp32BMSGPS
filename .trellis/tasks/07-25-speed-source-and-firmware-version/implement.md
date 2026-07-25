# 实施计划

1. 在 `esp_bms_lvgl_ui.c` 将速度来源的显示标签与枚举值合并为受 GPS/控制器 feature 宏筛选的静态选项表；使回调提交选项自身的枚举值。
2. 让 GPS-only 详情页也保留可点击的“速度来源”行，进入同一三级选择页；不改变控制器连接、轮胎、传动比或速度单位布局。
3. 运行 host selftest 和 headless feature matrix，检查全功能、GPS-only、controller-only、两者关闭的编译与导航。
4. 运行配置器版本自测，检查 `FIRMWARE_VERSION` 到生成头的值；必要时编译当前 profile 以验证实际镜像。
5. 执行 `git diff --check`、GitNexus `detect_changes`，再进行 Trellis quality check。
