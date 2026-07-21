#!/usr/bin/env python3

import re
import sys
from pathlib import Path


source = Path(sys.argv[1])
target = Path(sys.argv[2])
dsn = source.read_text(encoding="utf-8")

default_route_nets = [
    "GPIO1_EXP_RAW",
    "EXP_GPIO1",
    "DBG_UART_TX_GPIO43",
    "EXP_UART_TX_GPIO43",
    "DBG_UART_RX_GPIO44",
    "EXP_UART_RX_GPIO44",
    "3V3_EXP",
]
route_nets = sys.argv[3:] or default_route_nets
if len(route_nets) != len(set(route_nets)):
    raise SystemExit("route net list contains duplicates")


def scope_end(text: str, start: int) -> int:
    depth = 0
    quoted = False
    escaped = False
    for index in range(start, len(text)):
        char = text[index]
        if quoted:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                quoted = False
            continue
        if char == '"':
            quoted = True
        elif char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return index + 1
    raise SystemExit("unterminated DSN scope")


class_prefix = "    (class pcb_rnd_default "
class_start = dsn.find(class_prefix)
if class_start < 0 or dsn.find(class_prefix, class_start + 1) >= 0:
    raise SystemExit("expected exactly one pcb_rnd_default class")
class_stop = scope_end(dsn, class_start + 4)
class_scope = dsn[class_start:class_stop]

circuit_start = class_scope.find("(circuit")
rule_start = class_scope.find("(rule")
if circuit_start < 0 or rule_start < 0:
    raise SystemExit("default class is missing circuit or rule policy")
circuit_scope = class_scope[circuit_start:scope_end(class_scope, circuit_start)]
rule_scope = class_scope[rule_start:scope_end(class_scope, rule_start)]

net_tokens = re.findall(
    r'"(?:\\.|[^"\\])*"|[^\s()]+',
    class_scope[len(class_prefix) : min(circuit_start, rule_start)],
)
net_names = [token[1:-1] if token.startswith('"') else token for token in net_tokens]
all_nets = re.findall(r"^    \(net ([^\s()]+)", dsn, flags=re.MULTILINE)
if len(all_nets) != 89 or set(net_names) != set(all_nets):
    raise SystemExit(
        f"default class/net mismatch: class={len(net_names)}, nets={len(all_nets)}"
    )
missing = [name for name in route_nets if name not in net_names]
if missing:
    raise SystemExit(f"route nets missing from DSN: {', '.join(missing)}")

protected_nets = [name for name in net_names if name not in route_nets]
if "(use_layer" in circuit_scope:
    raise SystemExit("input class already contains a use_layer policy")


def format_class(name: str, nets: list[str], layers: list[str]) -> str:
    rows = [" ".join(nets[index : index + 6]) for index in range(0, len(nets), 6)]
    net_list = ("\n" + " " * 6).join(rows)
    quoted_layers = " ".join(f'"{layer}"' for layer in layers)
    layer_policy = f"        (use_layer {quoted_layers})\n"
    restricted_circuit = circuit_scope.replace(
        "(circuit\n", "(circuit\n" + layer_policy, 1
    )
    policy = restricted_circuit + "\n      " + rule_scope
    return f"    (class {name} {net_list}\n      {policy}\n    )"


replacement = format_class(
    "J6_AUTO",
    route_nets,
    ["3__top_copper", "13__L3_PWR_SIGNAL", "17__bottom_copper"],
)
replacement += "\n" + format_class(
    "NO_AUTOROUTE",
    protected_nets,
    ["3__top_copper", "17__bottom_copper"],
)
dsn = dsn[:class_start] + replacement + dsn[class_stop:]

if dsn.count("(class J6_AUTO ") != 1 or dsn.count("(class NO_AUTOROUTE ") != 1:
    raise SystemExit("failed to create constrained net classes")
if dsn.count("(use_layer ") != 2 or '"11__L2_GND"' in dsn[class_start:]:
    raise SystemExit("failed to constrain route layers or exclude L2_GND")

target.write_text(dsn, encoding="utf-8")
print(
    f"wrote {target} ({target.stat().st_size} bytes): "
    f"route={len(route_nets)}, protected={len(protected_nets)}"
)
