<!-- TRELLIS:START -->
# Trellis Instructions

These instructions are for AI assistants working in this project.

This project is managed by Trellis. The working knowledge you need lives under `.trellis/`:

- `.trellis/workflow.md` — development phases, when to create tasks, skill routing
- `.trellis/spec/` — package- and layer-scoped coding guidelines (read before writing code in a given layer)
- `.trellis/workspace/` — per-developer journals and session traces
- `.trellis/tasks/` — active and archived tasks (PRDs, research, jsonl context)

If a Trellis command is available on your platform (e.g. `/trellis:finish-work`, `/trellis:continue`), prefer it over manual steps. Not every platform exposes every command.

If you're using Codex or another agent-capable tool, additional project-scoped helpers may live in:
- `.agents/skills/` — reusable Trellis skills
- `.codex/agents/` — optional custom subagents

Managed by Trellis. Edits outside this block are preserved; edits inside may be overwritten by a future `trellis update`.

<!-- TRELLIS:END -->

## Project Constraints

- 默认用户界面语言是中文；新增本地 Web UI 文案时先写中文，再提供英文切换。
- 语言切换入口必须放在设备设置里，不要单独做登录页、首页提示或弹窗。
- TFT 当前只内置 ASCII 点阵字体；没有引入中文点阵/字库前，屏幕上的语言状态使用 `ZH` / `EN` 这类 ASCII 标记。
- Setup AP 的 SSID 必须使用 `fuckingBms_` 加随机后缀；首启或旧配置不符合当前策略时要自动重生并保存。
- Setup AP 密码必须是 8 位随机数字，并且二维码页面必须把当前 SSID 和当前密码同时显示出来。
