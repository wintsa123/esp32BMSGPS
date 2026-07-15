#!/usr/bin/env python3

import argparse
import subprocess
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / "build" / "esp32_bms_gps_idf.bin"


def firmware_code(path: Path) -> str:
    crc = 0
    with path.open("rb") as firmware:
        while chunk := firmware.read(64 * 1024):
            crc = zlib.crc32(chunk, crc)
    return f"{(crc & 0xFFFFFFFF) % 10000:04d}"


def self_test() -> None:
    assert zlib.crc32(b"123456789") & 0xFFFFFFFF == 0xCBF43926
    assert f"{42 % 10000:04d}" == "0042"


def main() -> None:
    parser = argparse.ArgumentParser(description="Build firmware and generate its four-digit OTA code")
    parser.add_argument("--self-test", action="store_true", help="run CRC and formatting checks only")
    args = parser.parse_args()

    self_test()
    if args.self_test:
        print("firmware code self-test passed")
        return

    subprocess.run([str(ROOT / "scripts" / "esp-idf-env.sh"), "build"], cwd=ROOT, check=True)
    code = firmware_code(FIRMWARE)
    code_path = FIRMWARE.with_suffix(".code.txt")
    code_path.write_text(f"{code}\n", encoding="ascii")
    print(f"firmware: {FIRMWARE}")
    print(f"code: {code_path} ({code})")


if __name__ == "__main__":
    main()
