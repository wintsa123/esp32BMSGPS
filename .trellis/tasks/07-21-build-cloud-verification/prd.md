# 本地云端构建与全矩阵验证

## Goal

提供可诊断、可本地或 GitHub Actions 重现、可审计产物与可信度的完整构建发布链。

## Requirements

- `doctor` 检查 IDF 5.5.4、目标工具链、Git、CMake、Ninja、Python、磁盘、网络，并在安装前报告和询问。
- Windows 10/11、apt/dnf/pacman Linux 支持官方安装路径；其他 Linux 给出完整人工安装命令。
- 云构建只使用已推送 ref，通过 Actions API 接收已校验编码配置，不提交、不建分支。
- Token 只来自 `ESP_BMS_GITHUB_TOKEN` 或会话，不写入配置/报告/日志。
- 本地和云端都生成应用 bin、完整 flash 包、OTA bin、四位校验码、size/map、配置摘要、构建报告和 SHA-256。
- Actions 构建十目标合法基准和七模块 on/off 矩阵，并汇总组件/符号/资源排除证据。

## Acceptance Criteria

- [ ] doctor 在支持平台给出准确缺项、退出码和安装确认边界。
- [ ] 未推送 ref、无 Token、非法配置和网络错误不会产生提交、分支或泄露秘密。
- [ ] 本地与云端相同 profile 的标准化配置和产物 manifest 契约一致。
- [ ] 规定产物全部存在、哈希可验证，四位码可从最终 OTA 哈希确定性复算。
- [ ] 十 MCU、七模块 on/off、UI 能力和 BMS 主机测试矩阵均有 CI 证据。
- [ ] 经典 ESP32 RFC2217 与可用后的 S3 实机报告逐项区分 pass/fail/unverified。
