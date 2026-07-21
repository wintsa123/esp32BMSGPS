# 网络/OTA 组件化与一次性语言选择设计

## 边界

| 层 | 责任 |
| --- | --- |
| `esp_bms_network` | Wi-Fi、Setup AP、HTTP server、首页资源和服务停止 |
| `esp_bms_ota` | `/api/ota` 的验证码、CRC、写分区、激活与重启 |
| `esp_bms_idf_runtime` | 状态、核心 API 分发、网络驱动回调和 feature 回退 |
| 模块注册表 | 仅在 feature 启用时注册模块并转发 Setup AP 生命周期 |
| 配置器 | catalog 解析、组件闭包生成、进程内文案翻译 |

`idf_main.c` 保留现有 Setup AP 状态机，但只经模块注册表和 runtime 网络驱动进入
网络组件。network off 时注册表返回 `ESP_ERR_NOT_SUPPORTED`。HTTP 的核心 API 继续由
runtime 分发；当 `ESP_BMS_FEATURE_OTA=1` 时 `/api/ota` 委托给 OTA component，否则
返回 501。

## 编译闭包

```text
catalog -> start.sh/start.ps1 -> generated/profile.cmake
        -> main CMake REQUIRES -> generated registry -> selected module init

Setup AP UI -> idf_main -> registry -> runtime network driver -> esp_bms_network
HTTP /api/ota -> runtime API dispatcher -> esp_bms_ota (feature enabled only)
```

- profile 是唯一 component-closure 输入。Bash 和 PowerShell 都生成
  `ESP_BMS_FEATURE_{AUDIO,BMS,CONTROLLER,GPS,NETWORK,OTA}`，数值与 requires 顺序一致。
- network component 独立声明 Wi-Fi、netif、event、HTTP 与首页嵌入；OTA component
  独立声明 `app_update` 和 HTTP。runtime 仅在 OTA feature 打开时依赖 OTA component。
- cast 仍为 `legacy-runtime`；profile/report 不宣称其已经从镜像和路由中移除。

## 本地化协议

- 无参数：每次调用显示 `1=简体中文`、`2=English`，只接受编号或 `zh`/`en`，无效输入
  循环重试。选择仅存于脚本变量。
- 有命令：默认 `zh`，全局 `--lang zh|en` 覆盖本次运行。CMD 原样转交 PowerShell；
  包装编译脚本接收当前语言而不自行持久化。
- 使用内部 message key 翻译帮助、提示、状态与错误。技术 token 原样拼入文案；绝不翻译
  命令、选项、配置 key、ID、路径、退出码或生成文件内容。

## 兼容与回滚

- profile 仍先写临时目录后移动，语言选择失败不触碰现有 profile。
- 默认 legacy 选择、Setup AP SSID/密码和二维码行为不变。
- 若网络/OTA 构建或设备回归失败，只回退本任务涉及的 component/CMake/注册表/脚本文案；
  不删除已有 firmware build profile 或无关工作树改动。
