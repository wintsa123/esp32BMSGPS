# 本地云端构建与全矩阵验证

## Goal

提供可诊断、可本地或 GitHub Actions 重现、可审计产物与可信度的完整构建发布链。

## Confirmed Current State

- `start.sh` 与 `start.ps1` 的交互式“在线构建”以及 `build-cloud` 命令都会先校验并写出 `firmware.env`，随后打印占位提示并以退出码 3 结束；仓库尚无 GitHub Actions 工作流，因此不会发生分派。
- 现有 Bash 自测将上述退出码和提示作为预期，必须改为验证真实的分派请求与失败边界。
- 当前工具链、安装脚本和自测均固定 ESP-IDF 6.0.2；本任务及父任务中的 5.5.4 描述已与可执行配置不一致。

## Requirements

- `doctor` 检查已确认的 ESP-IDF 版本、目标工具链、Git、CMake、Ninja、Python、磁盘、网络，并在安装前报告和询问。
- Windows 10/11、apt/dnf/pacman Linux 支持官方安装路径；其他 Linux 给出完整人工安装命令。
- `build-cloud` 与交互式“在线构建”在配置生成后自动校验当前分支的 HEAD 已推送，通过 Actions API 分派预先提交的工作流；不提交、不建分支。
- 云端工作流固定使用 ESP-IDF 6.0.2，与当前本地工具链保持一致。
- Token 只来自 `ESP_BMS_GITHUB_TOKEN` 或会话，不写入配置/报告/日志。
- 本地和云端都生成应用 bin、完整 flash 包、OTA bin、四位校验码、size/map、配置摘要、构建报告和 SHA-256。
- Actions 构建十目标合法基准和七模块 on/off 矩阵，并汇总组件/符号/资源排除证据。

## Acceptance Criteria

- [ ] doctor 在支持平台给出准确缺项、退出码和安装确认边界。
- [ ] 未推送 ref、无 Token、非法配置和网络错误不会产生提交、分支或泄露秘密。
- [ ] 本地与云端相同 profile 的标准化配置和产物 manifest 契约一致。
- [ ] 在线构建成功时返回零退出码和分派确认；未推送 ref、无 Token 或 API 错误时不产生远端构建且给出可诊断错误。
- [ ] 规定产物全部存在、哈希可验证，四位码可从最终 OTA 哈希确定性复算。
- [ ] 十 MCU、七模块 on/off、UI 能力和 BMS 主机测试矩阵均有 CI 证据。
- [ ] 经典 ESP32 RFC2217 与可用后的 S3 实机报告逐项区分 pass/fail/unverified。
