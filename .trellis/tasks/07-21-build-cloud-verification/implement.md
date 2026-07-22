# 自动云构建分派计划

1. 添加共享 Python 分派脚本：解析 GitHub origin、验证已推送 HEAD、获取不落盘 Token、编码配置并调用 `workflow_dispatch`。
2. 在 Bash 和 PowerShell 的交互式和命令式 `build-cloud` 路径中调用该脚本，替换占位退出。
3. 添加 ESP-IDF 6.0.2 的 GitHub Actions 工作流，解码、复验、构建并上传单 profile 配置与固件。
4. 将配置器自测改为覆盖分派前失败边界和共享脚本的请求构造；执行语法、host 自测和 GitNexus 变更检查。

## Validation

```bash
bash -n start.sh
python3 -m unittest tests/test_cloud_dispatch.py
./tests/configurator_selftest.sh
git diff --check
node .gitnexus/run.cjs detect-changes --repo esp32BMSGPS
```

## Risk

- GitHub workflow 必须先随脚本推送，首次实际分派只能针对包含该工作流的已推送提交。
- API 分派没有运行 ID 返回值；本实现不轮询或下载产物，避免把异步 CI 状态伪装成本地同步成功。
