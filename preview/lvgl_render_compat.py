#!/usr/bin/env python3
import argparse
import json
import os
import struct
import subprocess
import sys
import tempfile


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKILL_DIR = "/home/wintsa/.codex/skills/lvgl-preview-skill"
DEFAULT_BIN = "/home/wintsa/lv_micropython/ports/unix/build-lvgl/micropython"


RENDER_CODE = r'''
import json
import sys

import lvgl as lv

ui_path = sys.argv[1]
out_raw = sys.argv[2]
w = int(sys.argv[3])
h = int(sys.argv[4])
state = json.loads(sys.argv[5]) if len(sys.argv) > 5 and sys.argv[5] else {}
fn = sys.argv[6] if len(sys.argv) > 6 else "build"

if hasattr(lv, "init"):
    lv.init()
elif hasattr(lv, "__init__"):
    lv.__init__()

buf = bytearray(w * h * 4)
display = lv.display_create(w, h)
if hasattr(display, "set_default"):
    display.set_default()
if hasattr(display, "set_color_format") and hasattr(lv, "COLOR_FORMAT"):
    display.set_color_format(lv.COLOR_FORMAT.ARGB8888)
mode = getattr(lv, "DISPLAY_RENDER_MODE", None)
full_mode = mode.FULL if mode is not None and hasattr(mode, "FULL") else 0
if hasattr(display, "set_buffers"):
    display.set_buffers(buf, None, len(buf), full_mode)
elif hasattr(display, "set_draw_buffers"):
    display.set_draw_buffers(buf, None)

def flush_cb(disp, area, px):
    disp.flush_ready()

if hasattr(display, "set_flush_cb"):
    display.set_flush_cb(flush_cb)

if "/" in ui_path:
    ui_dir, ui_name = ui_path.rsplit("/", 1)
else:
    ui_dir, ui_name = ".", ui_path
if ui_name.endswith(".py"):
    ui_name = ui_name[:-3]
sys.path.insert(0, ui_dir)
sys.path.insert(0, "''' + SKILL_DIR + r'''/lib")
mod = __import__(ui_name)

scr_fn = getattr(lv, "scr_act", None) or getattr(lv, "screen_active")
scr = scr_fn()
getattr(mod, fn)(scr, **state)
lv.refr_now(display)

with open(out_raw, "wb") as f:
    f.write(w.to_bytes(4, "little"))
    f.write(h.to_bytes(4, "little"))
    f.write(buf)
'''


def render(ui, out, size, state, fn):
    binary = os.environ.get("LVMP_BIN", DEFAULT_BIN)
    if not os.path.isfile(binary):
        raise SystemExit("missing lv_micropython binary: " + binary)
    w_text, h_text = size.lower().split("x", 1)
    w = int(w_text)
    h = int(h_text)
    ui = os.path.abspath(ui)
    out = os.path.abspath(out)
    os.makedirs(os.path.dirname(out), exist_ok=True)

    with tempfile.NamedTemporaryFile("w", suffix=".py", delete=False) as script_file:
        script_file.write(RENDER_CODE)
        render_script = script_file.name
    with tempfile.NamedTemporaryFile(suffix=".raw", delete=False) as raw_file:
        raw = raw_file.name
    try:
        subprocess.run(
            [binary, "-X", "heapsize=64m", render_script, ui, raw, str(w), str(h), state, fn],
            check=True,
        )
        subprocess.run(
            [sys.executable, os.path.join(SKILL_DIR, "scripts", "raw2png.py"), raw, out],
            check=True,
        )
    finally:
        for path in (render_script, raw):
            try:
                os.remove(path)
            except OSError:
                pass


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("ui")
    parser.add_argument("--out", required=True)
    parser.add_argument("--size", default="240x320")
    parser.add_argument("--state", default="{}")
    parser.add_argument("--fn", default="build")
    args = parser.parse_args()
    json.loads(args.state)
    render(args.ui, args.out, args.size, args.state, args.fn)
    print("preview -> " + os.path.abspath(args.out))


if __name__ == "__main__":
    main()
