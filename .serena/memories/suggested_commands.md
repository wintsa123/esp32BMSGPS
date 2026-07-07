# Suggested Commands

- Inspect Trellis context: `python3 ./.trellis/scripts/get_context.py`.
- Inspect workflow phase detail: `python3 ./.trellis/scripts/get_context.py --mode phase --step <step>`.
- Check current/active Trellis tasks: `python3 ./.trellis/scripts/task.py current` if supported, otherwise `python3 ./.trellis/scripts/get_context.py`.
- Search files quickly: `rg --files`; search content: `rg "<pattern>"`.
- Build firmware: `./scripts/esp-idf-env.sh build`.
- Flash through the fixed LAN bridge: `./scripts/esp-idf-env.sh -p "rfc2217://192.168.2.10:4000?ign_set_control" -b 115200 flash`.
- Monitor through the fixed LAN bridge: `./scripts/esp-idf-env.sh -p "rfc2217://192.168.2.10:4000?ign_set_control" -b 115200 monitor`.
- GitNexus pre-commit scope check: `node .gitnexus/run.cjs detect-changes -r esp32BMSGPS --scope all`.
