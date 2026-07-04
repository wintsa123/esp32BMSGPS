# Suggested Commands

- Inspect Trellis context: `python3 ./.trellis/scripts/get_context.py`.
- Inspect workflow phase detail: `python3 ./.trellis/scripts/get_context.py --mode phase --step <step>`.
- Create a Trellis task after user consent: `python3 ./.trellis/scripts/task.py create "<title>" --slug <slug>`.
- Check current/active Trellis tasks: `python3 ./.trellis/scripts/task.py current` if supported, otherwise `python3 ./.trellis/scripts/get_context.py`.
- Search files quickly: `rg --files`; search content: `rg "<pattern>"`.
- Firmware build/test commands are not defined yet because no ESP32 project scaffold exists.