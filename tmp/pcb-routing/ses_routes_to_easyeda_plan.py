#!/usr/bin/env python3

import json
import re
import sys
from pathlib import Path


ALLOWED_NETS = {
    "3V3_EXP",
    "DBG_UART_RX_GPIO44",
    "DBG_UART_TX_GPIO43",
    "EXP_GPIO1",
    "EXP_UART_RX_GPIO44",
    "EXP_UART_TX_GPIO43",
    "GPIO1_EXP_RAW",
}

LAYER_IDS = {
    "3__top_copper": 1,
    "17__bottom_copper": 2,
}


def scaled(value: str) -> float:
    return int(value) / 1000


def parse_routes(source: Path) -> dict[str, list[dict[str, object]]]:
    lines: list[dict[str, object]] = []
    vias: list[dict[str, object]] = []
    current_net: str | None = None
    current_path: dict[str, object] | None = None
    path_points: list[tuple[float, float]] = []
    in_network_out = False
    routes_resolution_seen = False

    def finish_path() -> None:
        nonlocal current_path, path_points
        if current_path is None:
            return
        if len(path_points) < 2:
            raise ValueError(f"path has fewer than two points: {current_path}")
        for start, end in zip(path_points, path_points[1:]):
            if start == end:
                continue
            lines.append(
                {
                    **current_path,
                    "startX": start[0],
                    "startY": start[1],
                    "endX": end[0],
                    "endY": end[1],
                }
            )
        current_path = None
        path_points = []

    for raw_line in source.read_text(encoding="utf-8").splitlines():
        stripped = raw_line.strip()
        if stripped.startswith("(routes"):
            continue
        if stripped == "(resolution mil 1000)":
            routes_resolution_seen = True
            continue
        if stripped.startswith("(network_out"):
            in_network_out = True
            continue
        if not in_network_out:
            continue

        net_match = re.fullmatch(r"\(net\s+(\S+)", stripped)
        if net_match:
            finish_path()
            current_net = net_match.group(1)
            if current_net not in ALLOWED_NETS:
                raise ValueError(f"forbidden routed net: {current_net}")
            continue

        path_match = re.fullmatch(r"\(path\s+(\S+)\s+(-?\d+)", stripped)
        if path_match:
            finish_path()
            if current_net is None:
                raise ValueError("path found before net")
            layer_name = path_match.group(1)
            if layer_name not in LAYER_IDS:
                raise ValueError(f"forbidden routed layer: {layer_name}")
            current_path = {
                "net": current_net,
                "layer": LAYER_IDS[layer_name],
                "lineWidth": scaled(path_match.group(2)),
            }
            continue

        if current_path is not None:
            point_match = re.fullmatch(r"(-?\d+)\s+(-?\d+)", stripped)
            if point_match:
                path_points.append(
                    (scaled(point_match.group(1)), scaled(point_match.group(2)))
                )
                continue
            if stripped == ")":
                finish_path()
                continue

        via_match = re.fullmatch(r"\(via\s+\S+\s+(-?\d+)\s+(-?\d+)", stripped)
        if via_match:
            if current_net is None:
                raise ValueError("via found before net")
            vias.append(
                {
                    "net": current_net,
                    "x": scaled(via_match.group(1)),
                    "y": scaled(via_match.group(2)),
                    "holeDiameter": 12,
                    "diameter": 24,
                }
            )

    finish_path()
    if not routes_resolution_seen:
        raise ValueError("routes resolution must be mil 1000")
    if not lines:
        raise ValueError("no routed line segments found")
    return {"lines": lines, "vias": vias}


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {Path(sys.argv[0]).name} ROUTED_EASYEDA.ses")
    plan = parse_routes(Path(sys.argv[1]))
    json.dump(plan, sys.stdout, separators=(",", ":"))
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
