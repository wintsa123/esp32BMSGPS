#!/usr/bin/env python3
"""Publish a portable ESP-IDF flash bundle from one completed build."""

from __future__ import annotations

import argparse
import json
import os
import shlex
import shutil
import sys
import zlib
from pathlib import Path


def fail(message: str) -> None:
    raise ValueError(message)


def parse_offset(value: str) -> int:
    try:
        offset = int(value, 0)
    except ValueError as error:
        raise ValueError(f"invalid flash offset: {value}") from error
    if offset < 0:
        fail(f"flash offset must be non-negative: {value}")
    return offset


def source_path(build_dir: Path, relative_path: str) -> Path:
    candidate = (build_dir / relative_path).resolve()
    try:
        candidate.relative_to(build_dir)
    except ValueError as error:
        raise ValueError(f"flash file escapes build directory: {relative_path}") from error
    if not candidate.is_file():
        fail(f"missing flash file: {candidate}")
    return candidate


def write_merged_image(destination: Path, files: list[tuple[int, Path]]) -> None:
    temporary = destination.with_suffix(destination.suffix + ".tmp")
    cursor = 0
    try:
        with temporary.open("wb") as merged:
            for offset, source in files:
                if offset < cursor:
                    fail(f"overlapping flash file at 0x{offset:x}: {source.name}")
                merged.write(b"\xff" * (offset - cursor))
                with source.open("rb") as input_file:
                    shutil.copyfileobj(input_file, merged)
                cursor = offset + source.stat().st_size
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)


def firmware_code(path: Path) -> str:
    crc = 0
    with path.open("rb") as firmware:
        while chunk := firmware.read(64 * 1024):
            crc = zlib.crc32(chunk, crc)
    return f"{(crc & 0xFFFFFFFF) % 10000:04d}"


def write_release_files(
    output_dir: Path,
    profile: str,
    chip: str,
    flash_settings: dict[str, object],
    published: list[tuple[int, str, Path]],
    merged_name: str,
    ota_enabled: bool,
) -> None:
    setting_names = ("flash_mode", "flash_freq", "flash_size")
    settings = [flash_settings.get(name) for name in setting_names]
    if not chip or not all(isinstance(value, str) and value for value in settings):
        fail("missing ESP-IDF flash settings for generated flash helper")

    flash_mode, flash_freq, flash_size = settings
    quote = shlex.quote
    flash_script = output_dir / "flash.sh"
    flash_script.write_text(
        "#!/usr/bin/env bash\n"
        "set -euo pipefail\n\n"
        "cd -- \"$(dirname \"$0\")\"\n"
        "port=\"${1:-/dev/ttyUSB0}\"\n"
        "baud=\"${ESP_BMS_FLASH_BAUD:-921600}\"\n"
        "command -v esptool.py >/dev/null 2>&1 || {\n"
        "  printf 'missing esptool.py; activate ESP-IDF first\\n' >&2\n"
        "  exit 127\n"
        "}\n"
        f"exec esptool.py --chip {quote(chip)} --port \"$port\" --baud \"$baud\" write_flash "
        f"--flash_mode {quote(flash_mode)} --flash_freq {quote(flash_freq)} "
        f"--flash_size {quote(flash_size)} 0x0 {quote(merged_name)}\n",
        encoding="utf-8",
    )
    flash_script.chmod(0o755)

    image_rows = "\n".join(f"- `{filename}`: `0x{offset:x}`" for offset, filename, _ in published)
    if ota_enabled:
        ota_section = (
            "## OTA\n\n"
            f"网页 OTA 上传文件：`{profile}.bin`。\n\n"
            f"OTA 四位验证码：`{firmware_code(output_dir / f'{profile}.bin')}`。\n"
        )
    else:
        ota_section = "## OTA\n\n此固件未启用 OTA，不能通过设备网页升级。\n"
    (output_dir / "README.md").write_text(
        f"# {profile} 固件烧录包\n\n"
        "## 一键烧录\n\n"
        "先激活 ESP-IDF 环境，再连接设备并执行：\n\n"
        "```bash\n"
        "./flash.sh /dev/ttyUSB0\n"
        "```\n\n"
        "不传串口参数时默认使用 `/dev/ttyUSB0`。可通过 `ESP_BMS_FLASH_BAUD` 调整波特率。"
        f"脚本将完整镜像 `{merged_name}` 写入 `0x0`。\n\n"
        "## 分文件烧录位置\n\n"
        "完整镜像优先。若必须分文件烧录，请严格使用下列地址（也见 `flash-manifest.json`）：\n\n"
        f"{image_rows}\n\n"
        f"{ota_section}",
        encoding="utf-8",
    )


def publish(build_dir: Path, output_dir: Path, profile: str, ota_enabled: bool) -> None:
    build_dir = build_dir.resolve()
    flasher_args_path = build_dir / "flasher_args.json"
    if not flasher_args_path.is_file():
        fail(f"missing ESP-IDF flash manifest: {flasher_args_path}")

    with flasher_args_path.open(encoding="utf-8") as manifest_file:
        flasher_args = json.load(manifest_file)
    flash_files = flasher_args.get("flash_files")
    if not isinstance(flash_files, dict) or not flash_files:
        fail(f"invalid ESP-IDF flash manifest: {flasher_args_path}")

    app_file = flasher_args.get("app", {}).get("file")
    if not isinstance(app_file, str):
        fail(f"missing app image in ESP-IDF flash manifest: {flasher_args_path}")

    output_dir.mkdir(parents=True, exist_ok=True)
    published: list[tuple[int, str, Path]] = []
    used_names: set[str] = set()
    for offset_text, relative_path in flash_files.items():
        if not isinstance(offset_text, str) or not isinstance(relative_path, str):
            fail(f"invalid flash entry in ESP-IDF flash manifest: {flasher_args_path}")
        source = source_path(build_dir, relative_path)
        filename = f"{profile}.bin" if relative_path == app_file else source.name
        if filename in used_names:
            fail(f"duplicate output filename in ESP-IDF flash manifest: {filename}")
        used_names.add(filename)
        destination = output_dir / filename
        shutil.copyfile(source, destination)
        published.append((parse_offset(offset_text), filename, destination))

    published.sort(key=lambda item: item[0])
    merged_name = f"{profile}-flash.bin"
    write_merged_image(output_dir / merged_name, [(offset, path) for offset, _, path in published])

    extra_esptool_args = flasher_args.get("extra_esptool_args", {})
    flash_settings = flasher_args.get("flash_settings", {})
    chip = extra_esptool_args.get("chip") if isinstance(extra_esptool_args, dict) else None
    if not isinstance(chip, str) or not isinstance(flash_settings, dict):
        fail(f"invalid ESP-IDF flash settings: {flasher_args_path}")

    portable_manifest = {
        "chip": chip,
        "flash_settings": flash_settings,
        "files": [
            {"offset": f"0x{offset:x}", "file": filename}
            for offset, filename, _ in published
        ],
        "merged_image": {"offset": "0x0", "file": merged_name},
    }
    temporary_manifest = output_dir / "flash-manifest.json.tmp"
    with temporary_manifest.open("w", encoding="utf-8", newline="\n") as manifest_file:
        json.dump(portable_manifest, manifest_file, ensure_ascii=False, indent=2)
        manifest_file.write("\n")
    os.replace(temporary_manifest, output_dir / "flash-manifest.json")
    write_release_files(output_dir, profile, chip, flash_settings, published, merged_name, ota_enabled)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--profile", required=True)
    parser.add_argument("--ota-enabled", action="store_true")
    args = parser.parse_args()
    try:
        publish(args.build_dir, args.output_dir, args.profile, args.ota_enabled)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
