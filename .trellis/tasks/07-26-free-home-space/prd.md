# 释放 home 空间

## Goal

释放根分区空间，同时保持 Serena 语言服务可用。

## Confirmed Facts

- `/home` 所在根分区已满；`/vol1` 有充足可用空间。
- `/home/wintsa/.serena/language_servers/static` 占约 2.4G，且没有 Serena 进程运行。
- 当前 Codex 正在运行；会话迁移需要先复制并在最终切换前增量同步。

## Requirements

- 将 Serena 语言服务目录迁移到 `/vol1/1000/wintsa-home-storage/`。
- 在原路径创建符号链接，保持 Serena 的现有访问路径。
- 迁移前后校验目录内容和磁盘可用空间。
- 清理 APT 已下载的软件包缓存。
- 将 Codex 会话、顶层 Node 依赖、`.hermes`，以及 `CLI-WeChat-Bridge`、`boluobobo-ai-court-tutorial`、`civagent`、`edict`、`lv_micropython`、`docker`、`multica_workspaces` 迁移到 `/vol1/1000/wintsa-home-storage/`。
- 为每个迁移项建立原路径符号链接，并验证文件数与链接目标。

## Acceptance Criteria

- [x] 原路径为指向 `/vol1` 的有效符号链接。
- [x] 迁移目标包含原语言服务文件。
- [x] 根分区已释放超过 10G 空间。
- [x] APT 缓存已清理。
- [x] 所有新增迁移项均以有效符号链接保留原路径。
- [x] 每个迁移项的文件数在迁移前后一致。

## Out Of Scope

- 不迁移 `.linuxbrew`、Codex 规则、记忆或插件。
- 不删除项目或工具数据。
