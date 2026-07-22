# 自动云构建分派设计

## Scope

本切片实现已校验配置的 GitHub Actions 自动分派与单 profile 云端编译。十目标矩阵、模块排除审计、完整产物 manifest 和实机报告保留在本任务的后续迭代。

## Data Flow

`start.sh` / `start.ps1` 保持现有配置校验和 `firmware.env` 生成；随后共同调用 `scripts/dispatch-cloud-build.py`。该脚本验证当前分支 HEAD 与 `origin` 上同名分支一致，读取 `ESP_BMS_GITHUB_TOKEN`（交互终端可安全提示输入），将配置 Base64 编码为 Actions 输入，并向当前 GitHub `origin` 的 `cloud-build.yml` 发送 `workflow_dispatch`。

`.github/workflows/cloud-build.yml` 在分派 ref 上检出源码，解码输入到临时文件，再调用现有 `start.sh validate` 和 `build-local`。工作流运行 ESP-IDF 6.0.2，并上传生成的配置和固件目录。

## Boundaries

- 配置在本地和云端各验证一次；远端只接收 Base64 编码的规范 `firmware.env`。
- Token 仅通过环境变量或不回显的终端输入进入 Python 进程，不进入命令行参数、配置、请求确认输出或日志。
- 仅允许 GitHub `origin`、已推送的当前分支和与本地 HEAD 相同的远端 SHA；任一校验失败时，不发 HTTP 请求。
- GitHub 的分派 API 成功只返回 204，CLI 只输出分支与提交短 SHA，不猜测运行编号或产物链接。

## Rollback

删除工作流文件并恢复两端 `build-cloud` 调用即可停止新分派；没有自动提交、推送或本地配置格式迁移。
