#!/usr/bin/env python3

import re
import sys
from pathlib import Path


source = Path(sys.argv[1])
target = Path(sys.argv[2])
expected_components = int(sys.argv[3]) if len(sys.argv) > 3 else 127
expected_nets = int(sys.argv[4]) if len(sys.argv) > 4 else 84
dsn = source.read_text(encoding="utf-8")

boundary_rect = re.compile(r"^    \(boundary \(rect pcb [^\n]+\)\)\n", re.MULTILINE)
dsn, removed = boundary_rect.subn("", dsn, count=1)
if removed != 1:
    raise SystemExit(f"expected one generated rectangular boundary, found {removed}")

if dsn.count("    (boundary (path signal 0") != 1:
    raise SystemExit("expected one 112 x 68 mm path boundary")

keepouts = """    (keepout \"FPC40_CU\" (polygon signal 0 106.3016 23.49088 110.3016 23.49088 110.3016 46.49088 106.3016 46.49088 106.3016 23.49088))
    (via_keepout \"FPC40_VIA\" (polygon signal 0 106.3016 23.49088 110.3016 23.49088 110.3016 46.49088 106.3016 46.49088 106.3016 23.49088))
    (keepout \"FPC6_CU\" (polygon signal 0 83.0166 11.44088 87.0166 11.44088 87.0166 17.44088 83.0166 17.44088 83.0166 11.44088))
    (via_keepout \"FPC6_VIA\" (polygon signal 0 83.0166 11.44088 87.0166 11.44088 87.0166 17.44088 83.0166 17.44088 83.0166 11.44088))
    (keepout \"M3_TL_CU\" (circle signal 8.0 80.3016 64.00088))
    (via_keepout \"M3_TL_VIA\" (circle signal 8.0 80.3016 64.00088))
    (keepout \"M3_TR_CU\" (circle signal 8.0 184.3016 64.00088))
    (via_keepout \"M3_TR_VIA\" (circle signal 8.0 184.3016 64.00088))
    (keepout \"M3_BL_CU\" (circle signal 8.0 80.3016 4.00088))
    (via_keepout \"M3_BL_VIA\" (circle signal 8.0 80.3016 4.00088))
    (keepout \"M3_BR_CU\" (circle signal 8.0 184.3016 4.00088))
    (via_keepout \"M3_BR_VIA\" (circle signal 8.0 184.3016 4.00088))
    (keepout \"ESP32_ANT_CU_CENTER\" (polygon signal 0 179.3016 26.2 188.30036 26.2 188.30036 41.8 179.3016 41.8 179.3016 26.2))
    (via_keepout \"ESP32_ANT_VIA_CENTER\" (polygon signal 0 179.3016 26.2 188.30036 26.2 188.30036 41.8 179.3016 41.8 179.3016 26.2))
    (keepout \"ESP32_ANT_CU_LOWER\" (polygon signal 0 181.2 22.0 188.30036 22.0 188.30036 26.2 181.2 26.2 181.2 22.0))
    (via_keepout \"ESP32_ANT_VIA_LOWER\" (polygon signal 0 181.2 22.0 188.30036 22.0 188.30036 26.2 181.2 26.2 181.2 22.0))
    (keepout \"ESP32_ANT_CU_UPPER\" (polygon signal 0 181.2 41.8 188.30036 41.8 188.30036 46.0 181.2 46.0 181.2 41.8))
    (via_keepout \"ESP32_ANT_VIA_UPPER\" (polygon signal 0 181.2 41.8 188.30036 41.8 188.30036 46.0 181.2 46.0 181.2 41.8))
"""

layer_marker = '    (layer "3__top_copper" (type signal))\n'
if dsn.count(layer_marker) != 1:
    raise SystemExit("top copper layer marker was not unique")
dsn = dsn.replace(layer_marker, keepouts + layer_marker, 1)

if len(re.findall(r"^    \(layer ", dsn, flags=re.MULTILINE)) != 4:
    raise SystemExit("DSN does not contain exactly four routing layers")
component_count = len(re.findall(r"^    \(component ", dsn, flags=re.MULTILINE))
if component_count != expected_components:
    raise SystemExit(
        f"DSN component count: expected {expected_components}, found {component_count}"
    )
net_count = len(re.findall(r"^    \(net ", dsn, flags=re.MULTILINE))
if net_count != expected_nets:
    raise SystemExit(f"DSN net count: expected {expected_nets}, found {net_count}")

target.write_text(dsn, encoding="utf-8")
print(f"wrote {target} ({target.stat().st_size} bytes)")
