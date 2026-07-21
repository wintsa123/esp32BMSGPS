# 实施计划

1. 新建根 `start.sh`、`start.ps1`、`start.cmd` 和 versioned catalog；实现
   `doctor/configure/validate/build-local/build-cloud` 及无参数菜单。
2. 对 catalog 与配置使用严格的 `KEY=VALUE` 解析，输出排序后的
   `normalized.env`，并生成 profile 输入与报告。
3. 为 legacy profile 生成可构建的独立 ESP-IDF build 命令；S3 profile 在
   板级支持到位前拒绝 `build-local`，避免虚假成功。
4. 添加 shell 自检，覆盖恶意值、重复 GPIO、依赖补齐、冲突和危险 GPIO。
5. 运行自检、legacy profile 配置/构建、`git diff --check` 和 GitNexus
   变更检测。组件拆分后补充 map/symbol 排除证据。

## 风险

当前 `main/CMakeLists.txt` 和 `esp_bms_idf_runtime` 固定引用全量运行时。
本子任务不得通过运行时开关宣称二进制裁剪；真实的 CMake 可达性变更属于
`07-21-modular-firmware`，并以本生成器的 `profile.cmake` 为输入。
