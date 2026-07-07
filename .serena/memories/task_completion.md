# Task Completion

- For firmware code tasks, run `./scripts/esp-idf-env.sh build`.
- Before commit, run `git diff --check` and `node .gitnexus/run.cjs detect-changes -r esp32BMSGPS --scope all`.
- After completing each code task, run one flash attempt via `mem:hardware/lan_rfc2217_flash_bridge`.
- If hardware logs are needed, monitor with the same RFC2217 endpoint after flashing; use a TTY for `idf.py monitor`.
- Before ending a Trellis coding task, run `python3 ./.trellis/scripts/get_context.py` to verify task state and inspect uncommitted workspace changes.
- After memory edits, the user can run `serena memories check` from the project root to validate memory references.
