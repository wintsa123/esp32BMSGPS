#!/usr/bin/env python3
"""Publish APK and firmware updates to the Vercel static update server.

Writes:
  vercel/public/apk/latest.json          - APK release manifest (version from build.gradle.kts)
  vercel/public/apk/两轮智控.apk          - APK binary
  vercel/public/firmware/firmware.json   - online firmware manifest
  vercel/public/firmware/<profile>.bin   - firmware binaries

The four-digit OTA code is CRC-32 of the app image modulo 10000, matching the
firmware-side verification (see scripts/publish-flash-artifacts.py).
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import re
import shutil
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_APK = ROOT / "两轮智控.apk"
GRADLE_FILE = ROOT / "android-cast" / "app" / "build.gradle.kts"
PUBLIC_DIR = ROOT / "vercel" / "public"
APK_DIR = PUBLIC_DIR / "apk"
FIRMWARE_DIR = PUBLIC_DIR / "firmware"
APK_FILE_NAME = "两轮智控.apk"

# Human-readable names for known profiles; unknown profiles fall back to the profile id.
PROFILE_NAMES = {
    "esp32": "ESP32 精简版",
    "esp32-full": "ESP32 完整版",
    "esp32s3-n16r8-st7796u-gt1151": "ESP32-S3 完整版",
    "two-wheel-s3": "ESP32-S3 两轮版",
}


def fail(message: str) -> None:
    raise ValueError(message)


def firmware_code(path: Path) -> str:
    crc = 0
    with path.open("rb") as firmware:
        while chunk := firmware.read(64 * 1024):
            crc = zlib.crc32(chunk, crc)
    return f"{(crc & 0xFFFFFFFF) % 10000:04d}"


def read_gradle_version() -> tuple[int, str]:
    if not GRADLE_FILE.is_file():
        fail(f"missing Android gradle file: {GRADLE_FILE}")
    text = GRADLE_FILE.read_text(encoding="utf-8")
    code_match = re.search(r"versionCode\s*=\s*(\d+)", text)
    name_match = re.search(r'versionName\s*=\s*"([^"]+)"', text)
    if not code_match or not name_match:
        fail(f"cannot find versionCode/versionName in {GRADLE_FILE}")
    return int(code_match.group(1)), name_match.group(1)


def publish_apk(apk_path: Path, dry_run: bool) -> dict[str, object]:
    if not apk_path.is_file():
        fail(f"missing APK: {apk_path}")
    version_code, version_name = read_gradle_version()
    manifest = {
        "versionCode": version_code,
        "versionName": version_name,
        "url": f"/apk/{APK_FILE_NAME}",
        "size": apk_path.stat().st_size,
        "note": "最新版本，建议升级",
        "publishedAt": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
    }
    if dry_run:
        print(f"[dry-run] APK: {apk_path} -> {APK_DIR / APK_FILE_NAME}")
        print(f"[dry-run] manifest: {json.dumps(manifest, ensure_ascii=False)}")
        return manifest
    APK_DIR.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(apk_path, APK_DIR / APK_FILE_NAME)
    _write_json(APK_DIR / "latest.json", manifest)
    print(f"APK published: {APK_DIR / APK_FILE_NAME} (version {version_name}, code {version_code})")
    return manifest


def collect_firmware(entries: list[str], directory: Path | None) -> list[tuple[str, Path]]:
    collected: list[tuple[str, Path]] = []
    for entry in entries:
        if "=" not in entry:
            fail(f"--firmware expects PROFILE=PATH, got: {entry}")
        profile, raw_path = entry.split("=", 1)
        path = Path(raw_path)
        if not path.is_absolute():
            path = ROOT / path
        if not profile or not path.is_file():
            fail(f"invalid --firmware entry: {entry}")
        collected.append((profile, path))
    if directory is not None:
        for path in sorted(directory.iterdir()):
            if path.is_file() and path.suffix == ".bin":
                collected.append((path.stem, path))
    if not collected:
        fail("no firmware binaries given; pass --firmware PROFILE=PATH or --firmware-dir DIR")
    return collected


def publish_firmware(entries: list[str], directory: Path | None, dry_run: bool) -> dict[str, object]:
    firmwares: list[dict[str, object]] = []
    for profile, path in collect_firmware(entries, directory):
        firmware: dict[str, object] = {
            "profile": profile,
            "name": PROFILE_NAMES.get(profile, profile),
            "chip": "",
            "version": "",
            "url": f"/firmware/{profile}.bin",
            "size": path.stat().st_size,
            "code": firmware_code(path),
            "note": "",
        }
        env_file = ROOT / "firmware-builds" / profile / "firmware.env"
        if env_file.is_file():
            env = {}
            for line in env_file.read_text(encoding="utf-8").splitlines():
                if "=" in line and not line.lstrip().startswith("#"):
                    key, _, value = line.partition("=")
                    env[key.strip()] = value.strip()
            firmware["chip"] = env.get("MCU", "")
            firmware["version"] = env.get("FIRMWARE_VERSION", "")
            note = env.get("NOTE", "")
            dashboards = env.get("DASHBOARDS", "")
            if note:
                firmware["note"] = note
            elif dashboards:
                firmware["note"] = f"仪表：{dashboards.replace(',', '、')}"
        firmwares.append(firmware)
        if dry_run:
            print(f"[dry-run] firmware: {path} -> {FIRMWARE_DIR / (profile + '.bin')} code={firmware['code']}")
        else:
            FIRMWARE_DIR.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, FIRMWARE_DIR / f"{profile}.bin")
            print(f"firmware published: {profile}.bin ({path.stat().st_size} bytes, code {firmware['code']})")
    manifest = {
        "updatedAt": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
        "firmwares": firmwares,
    }
    if not dry_run:
        _write_json(FIRMWARE_DIR / "firmware.json", manifest)
    else:
        print(f"[dry-run] firmware manifest: {json.dumps(manifest, ensure_ascii=False, indent=2)}")
    return manifest


def _write_json(path: Path, data: dict[str, object]) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("w", encoding="utf-8", newline="\n") as output:
        json.dump(data, output, ensure_ascii=False, indent=2)
        output.write("\n")
    os.replace(temporary, path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apk", type=Path, default=DEFAULT_APK, help="APK file to publish (default: 两轮智控.apk)")
    parser.add_argument("--firmware", action="append", default=[], metavar="PROFILE=PATH", help="firmware binary to publish (repeatable)")
    parser.add_argument("--firmware-dir", type=Path, help="directory scanned for *.bin firmware files")
    parser.add_argument("--dry-run", action="store_true", help="print manifests without writing files")
    args = parser.parse_args()
    try:
        publish_apk(args.apk, args.dry_run)
        publish_firmware(args.firmware, args.firmware_dir, args.dry_run)
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
