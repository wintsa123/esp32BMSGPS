# 固件组件化与编译期裁剪

## Goal

把当前单体 runtime 拆成核心与可选 ESP-IDF 组件，使未选功能从编译、链接、UI、Web、API 和资源层面完全消失。

## Requirements

- 固定核心拥有启动、NVS、LVGL、显示、输入、设置框架和稳定的状态/动作契约。
- 拆出 BMS BLE、GPS、控制器 BLE、音频、网络、HTTP/OTA、投屏；内部 BLE/Wi-Fi/HTTP 依赖自动补齐。
- 生成注册表是 `main` 唯一可选生命周期入口，CMake 只使所选组件可达。
- TFT/Web 设置、API 路由、Web 片段和嵌入资源由所选组件贡献；零/一/多选项行为一致。
- 每次迁移删除旧实现，保持 legacy enabled 路径行为，不留重复死副本。

## Acceptance Criteria

- [ ] 七个模块分别通过 enabled/disabled 构建。
- [ ] disabled 构建的组件图、map、符号和 strings 都证明模块不存在。
- [ ] enabled legacy 全功能配置通过基线 UI、API、启动和实机回归。
- [ ] 核心头文件不依赖可选组件头文件，`main` 不直接调用可选模块符号。
- [ ] TFT/Web 共用稳定能力 ID，零/一/多贡献测试通过。
