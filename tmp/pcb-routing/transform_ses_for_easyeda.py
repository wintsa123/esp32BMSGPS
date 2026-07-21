#!/usr/bin/env python3

import re
import sys
from pathlib import Path


source = Path(sys.argv[1])
target = Path(sys.argv[2])
ses = source.read_text(encoding="utf-8")

x_origin_nm = 76_301_600
y_origin_nm = 68_000_880
nm_per_mil = 25_400
output_resolution = 1000


def coordinate(value: int, origin_nm: int) -> int:
    return round((value - origin_nm) * output_resolution / nm_per_mil)


def dimension(value: int) -> int:
    return round(value * output_resolution / nm_per_mil)


lines = ses.splitlines(keepends=True)
result: list[str] = []
in_routes = False
in_path = False
routes_resolution_seen = False

for line in lines:
    stripped = line.strip()
    if stripped.startswith("(routes"):
        in_routes = True
        result.append(line)
        continue
    if not in_routes:
        result.append(line)
        continue

    if stripped.startswith("(resolution "):
        if routes_resolution_seen:
            raise SystemExit("routes contains more than one resolution")
        routes_resolution_seen = True
        indent = line[: len(line) - len(line.lstrip())]
        result.append(f"{indent}(resolution mil {output_resolution})\n")
        continue

    circle = re.fullmatch(r"(\s*\(circle\s+\S+\s+)(-?\d+)(\s+0\s+0\)\s*)", line.rstrip("\n"))
    if circle:
        result.append(
            f"{circle.group(1)}{dimension(int(circle.group(2)))}{circle.group(3)}\n"
        )
        continue

    path = re.fullmatch(r"(\s*\(path\s+\S+\s+)(-?\d+)(\s*)", line.rstrip("\n"))
    if path:
        in_path = True
        result.append(
            f"{path.group(1)}{dimension(int(path.group(2)))}{path.group(3)}\n"
        )
        continue

    if in_path:
        pair = re.fullmatch(r"(\s*)(-?\d+)\s+(-?\d+)(\s*)", line.rstrip("\n"))
        if pair:
            x = coordinate(int(pair.group(2)), x_origin_nm)
            y = coordinate(int(pair.group(3)), y_origin_nm)
            result.append(f"{pair.group(1)}{x} {y}{pair.group(4)}\n")
            continue
        if stripped == ")":
            in_path = False
            result.append(line)
            continue

    via = re.fullmatch(
        r"(\s*\(via\s+\S+\s+)(-?\d+)\s+(-?\d+)(\s*)",
        line.rstrip("\n"),
    )
    if via:
        x = coordinate(int(via.group(2)), x_origin_nm)
        y = coordinate(int(via.group(3)), y_origin_nm)
        result.append(f"{via.group(1)}{x} {y}{via.group(4)}\n")
        continue

    result.append(line)

if not routes_resolution_seen:
    raise SystemExit("routes resolution was not found")

transformed = "".join(result)
if "(path 11__L2_GND " in transformed:
    raise SystemExit("L2_GND routes are forbidden")

target.write_text(transformed, encoding="utf-8")
print(f"wrote {target} ({target.stat().st_size} bytes)")
