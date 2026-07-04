# Task Completion

- Current repository has no firmware build, format, lint, unit-test, or flashing command.
- For Trellis-managed work, completion requires at least running the relevant local validation commands once they exist and documenting any command that cannot run.
- Before ending a coding task, run `python3 ./.trellis/scripts/get_context.py` to verify task state and inspect uncommitted workspace changes.
- After onboarding or memory edits, the user can run `serena memories check` from the project root to validate memory references.