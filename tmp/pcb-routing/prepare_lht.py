#!/usr/bin/env python3

import re
import sys
from pathlib import Path


source = Path(sys.argv[1])
target = Path(sys.argv[2])
board = source.read_text(encoding="utf-8")


def replace_once(old: str, new: str, label: str) -> None:
    global board
    count = board.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, found {count}")
    board = board.replace(old, new, 1)


replace_once(
    " li:styles {\n }",
    """ li:styles {
   ha:Signal {
    via_proto = 1
    thickness = 0.20mm
    text_thick = 0.0
    text_scale = 0
    clearance = 0.20mm
   }
 }""",
    "route style",
)

replace_once(
    """   ha:size {
    y2 = 5.9352in
    x1 = 0.0
    x2 = 7.6674in
    thermal_scale = 0.500000
    y1 = 0.0
   }""",
    """   ha:size {
    y2 = 2.9312in
    x1 = 3.004in
    x2 = 7.4134in
    thermal_scale = 0.500000
    y1 = 254.0mil
   }""",
    "board extents",
)

via_prototype = """

   ha:ps_proto_v6.1 {
     hdia=0.30mm; hplated=1; htop=0; hbottom=0;
     li:shape {

       ha:ps_shape_v4 {
        ha:ps_circ { x=0.0; y=0.0; dia=0.60mm;        }
        ha:combining {        }
        ha:layer_mask {
         copper = 1
         top = 1
        }
        clearance=0.20mm
       }

       ha:ps_shape_v4 {
        ha:ps_circ { x=0.0; y=0.0; dia=0.60mm;        }
        ha:combining {        }
        ha:layer_mask {
         copper = 1
         intern = 1
        }
        clearance=0.20mm
       }

       ha:ps_shape_v4 {
        ha:ps_circ { x=0.0; y=0.0; dia=0.60mm;        }
        ha:combining {        }
        ha:layer_mask {
         bottom = 1
         copper = 1
        }
        clearance=0.20mm
       }
     }
   }
"""

root_pstk_end = "\n  }\n\n   li:objects {"
root_pstk_start = board.index("  li:padstack_prototypes {")
root_pstk_stop = board.index(root_pstk_end, root_pstk_start)
board = board[:root_pstk_stop] + via_prototype + board[root_pstk_stop:]

root_layers_start = board.rindex("\n   li:layers {")
root_layers_end = board.index("\n }\n\n ha:font {", root_layers_start)
root_layers = board[root_layers_start:root_layers_end]
root_layers = root_layers.replace("{ha:Top Silkscreen Layer}", "ha:L2_GND", 1)
root_layers = root_layers.replace("{ha:Top Paste Mask Layer}", "ha:L3_PWR_SIGNAL", 1)

unused_inner_pattern = re.compile(
    r"\n    \{ha:Top Solder Mask Layer\} \{\n"
    r"     lid=17\n"
    r"     group=15\n"
    r"     ha:combining \{     \}\n\n"
    r"      li:objects \{\n"
    r"      \}\n"
    r"      color = \{#3a5fcd\}\n"
    r"    \}\n"
)
root_layers, removed = unused_inner_pattern.subn("\n", root_layers, count=1)
if removed != 1:
    raise SystemExit(f"unused Inner3 layer: expected one match, found {removed}")

outer_outline = """
       ha:line.50000 {
        x1=7.4134in; y1=254.0mil; x2=7.4134in; y2=2.9312in; thickness=0.1mil; clearance=0.0;
        ha:flags {
         clearline=1
        }
       }
       ha:line.50001 {
        x1=7.4134in; y1=2.9312in; x2=3.004in; y2=2.9312in; thickness=0.1mil; clearance=0.0;
        ha:flags {
         clearline=1
        }
       }
       ha:line.50002 {
        x1=3.004in; y1=2.9312in; x2=3.004in; y2=254.0mil; thickness=0.1mil; clearance=0.0;
        ha:flags {
         clearline=1
        }
       }
       ha:line.50003 {
        x1=3.004in; y1=254.0mil; x2=7.4134in; y2=254.0mil; thickness=0.1mil; clearance=0.0;
        ha:flags {
         clearline=1
        }
       }
"""
outline_marker = re.compile(
    r"(\n    ha:outline \{.*?\n      li:objects \{)(\n       ha:line\.\d+ \{)",
    re.DOTALL,
)
root_layers, inserted = outline_marker.subn(
    lambda match: match.group(1) + outer_outline + match.group(2),
    root_layers,
    count=1,
)
if inserted != 1:
    raise SystemExit(f"outline insertion marker: expected one match, found {inserted}")
board = board[:root_layers_start] + root_layers + board[root_layers_end:]

stack_start = board.index("\n ha:layer_stack {")
stack_end = board.index("\n li:pcb-rnd-conf-v1 {", stack_start)
stack = board[stack_start:stack_end]
stack = stack.replace(
    """   ha:11 {
    name = Intern""",
    """   ha:11 {
    name = L2_GND""",
    1,
)
stack = stack.replace(
    """   ha:13 {
    name = Intern""",
    """   ha:13 {
    name = L3_PWR_SIGNAL""",
    1,
)
unused_stack_pattern = re.compile(
    r"   ha:15 \{\n"
    r"    name = Intern\n"
    r"    ha:type \{ copper=1; intern=1;    \}\n"
    r"    li:layers \{ 17;    \}\n"
    r"   \}\n"
    r"   ha:16 \{\n"
    r"    ha:type \{ substrate=1; intern=1;    \}\n"
    r"    li:layers \{    \}\n"
    r"   \}\n"
)
stack, removed = unused_stack_pattern.subn("", stack, count=1)
if removed != 1:
    raise SystemExit(f"unused Inner3 stack: expected one match, found {removed}")
board = board[:stack_start] + stack + board[stack_end:]

target.write_text(board, encoding="utf-8")
print(f"wrote {target} ({target.stat().st_size} bytes)")
