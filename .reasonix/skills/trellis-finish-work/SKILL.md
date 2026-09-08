---
name: trellis-finish-work
description: "Wrap up the current session: verify completion, coordinate the approved commit step, archive only completed tasks, and record session progress. Use when ready to end the session."
---

# Finish Work

Wrap up the current session: archive the active task (and any other completed-but-unarchived tasks the user wants to clean up) and record the session journal. Code commits are NOT done here — those happen in workflow Phase 3.4 before you invoke this command.

## Step 1: Survey current state

```bash
python3 ./.trellis/scripts/get_context.py --mode record
```

This prints:

- **My active tasks** — review whether any besides the current one are actually done (code merged, AC met) and should be archived this round.
- **Git status** — quick visual on what's dirty.
- **Recent commits** — you'll need their hashes in Step 4 for `--commit`.

If `--mode record` surfaces other completed tasks not tied to the current session, surface them to the user with a one-shot confirmation: "These N tasks look done — archive them too in this round? [y/N]". Default is no; this optional question does not block work on the current task.

Before archiving the current task, verify its acceptance criteria are met, required checks passed, and no blocking findings remain. A clean working tree is not proof of completion. If incomplete, keep the task active, continue authorized work where possible, and report remaining work or blocked verification. If the user is ending the session, record progress without marking the task completed.

## Step 2: Sanity check — classify dirty paths

Run:

```bash
git status --porcelain
```

Filter out paths under `.trellis/workspace/` and `.trellis/tasks/` — those are managed by `add_session.py` and `task.py archive` auto-commits and will appear dirty as part of this skill's own work.

For each remaining dirty path, decide whether it belongs to **the current task** or to **other parallel work** (e.g., another terminal window editing the same repo). Heuristics:

- Paths referenced in the current task's `prd.md` / `implement.jsonl` / `check.jsonl` → current task
- Paths in code areas matching the task's stated scope, or that you remember editing this session → current task
- Paths in unrelated areas you have no recollection of touching this session → other parallel work

Then route:

- **Any remaining path looks like current-task work** — the main session returns to Phase 3.4:
  > "This task has uncommitted changes: `<list>`. I will prepare the Phase 3.4 commit plan for approval, then continue wrap-up after it is committed."

  The main session prepares or resumes the Phase 3.4 commit plan, obtains its required approval, and resumes this skill after the commits succeed. Do not ask the user to navigate workflow commands. Reuse valid approval of an unchanged commit plan. If the user explicitly chooses manual commits, wait only for that dependency and continue independent work.
- **All remaining paths look unrelated** (other parallel-window work) — report them once and continue to Step 3:
  > "FYI, dirty files outside this task's scope — leaving them for the other window: `<list>`."
- **Genuinely unsure** — ask the user once: "Are `<list>` this task's work I forgot to commit, or another window's? (commit / ignore)" — then route per their answer.

## Step 3: Archive task(s)

```bash
python3 ./.trellis/scripts/task.py archive <task-name>
```

Archive the current active task only after the completion check in Step 1 passes, plus any completed extra tasks the user confirmed. Each archive produces a `chore(task): archive ...` commit via the script's auto-commit when enabled. Preserve explicit limits the user placed on committing.

If there is no active task and the user did not confirm any cleanup archives, skip this step.

## Step 4: Record session journal

```bash
python3 ./.trellis/scripts/add_session.py \
  --title "Session Title" \
  --commit "hash1,hash2" \
  --summary "Brief summary"
```

Use the work-commit hashes produced in Phase 3.4 (visible in Step 1's `Recent commits` list, or via `git log --oneline`) for `--commit`. Do not include archive commit hashes or invent hashes for unfinished work; omit `--commit` when there are no work commits. Describe unfinished work as progress, not completion. This produces a `chore: record journal` commit when auto-commit is enabled; pass `--no-commit` if the user prohibited commits.

Final git log order: `<work commits from 3.4>` → `chore(task): archive ...` (one or more) → `chore: record journal`.
