#!/usr/bin/env python3
"""Build a local signed Web update release. Does not upload or publish anything."""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from urllib.parse import urlsplit
import zipfile

MAX_FILE = 8 * 1024 * 1024
MAX_TOTAL = 32 * 1024 * 1024
MAX_FILES = 512
MAX_MANIFEST = 256 * 1024
MAX_INTEGER = 9007199254740991
EXCLUDED = {"apk", "firmware"}


def https_base(value: str) -> str:
    url = urlsplit(value)
    if (url.scheme != "https" or not url.hostname or url.username is not None
            or url.password is not None or url.query or url.fragment
            or any(c.isspace() or c in "\\%?#" for c in value)):
        raise ValueError("base URL must be an absolute HTTPS directory without credentials/query/fragment")
    # Also validate malformed ports and path segments before using the URL as a prefix.
    _ = url.port
    if any(part in {".", ".."} for part in url.path.split("/")):
        raise ValueError("base URL must not contain dot segments")
    return value.rstrip("/")


def collect_files(dist: Path) -> list[Path]:
    if not dist.is_dir() or dist.is_symlink():
        raise ValueError("dist must be a real directory")
    files = []
    for path in sorted(dist.rglob("*")):
        relative = path.relative_to(dist)
        if relative.parts[0] in EXCLUDED or relative.as_posix() in {"_headers", "_redirects"}:
            continue
        if path.is_symlink():
            raise ValueError(f"symlinks are forbidden: {relative}")
        if path.is_dir():
            continue
        name = relative.as_posix()
        if "dashboard-update.json" in relative.parts:
            raise ValueError("trusted dashboard-update.json must never be inside the Web dist")
        if (not path.is_file() or len(name) > 240 or not re.fullmatch(r"[A-Za-z0-9._/-]+", name)
                or any(not part or part.startswith(".") for part in name.split("/"))):
            raise ValueError(f"unsupported Web resource path: {relative}")
        if not 1 <= path.stat().st_size <= MAX_FILE:
            raise ValueError(f"Web resource must be nonempty and at most 8 MiB: {relative}")
        files.append(path)
    if not (dist / "index.html").is_file() or not files or len(files) > MAX_FILES:
        raise ValueError("release must include index.html and at most 512 files")
    if sum(path.stat().st_size for path in files) > MAX_TOTAL:
        raise ValueError("Web release exceeds 32 MiB")
    return files


def package_web(dist: Path, output: Path, base_url: str, key: Path, version: str,
                sequence: int, min_app_version_code: int, bundled_sequence: int = 0) -> dict:
    base_url = https_base(base_url)
    if (not isinstance(version, str) or not version.strip() or len(version.encode("utf-16-le")) // 2 > 100
            or type(sequence) is not int or not 1 <= sequence <= MAX_INTEGER
            or type(min_app_version_code) is not int or not 1 <= min_app_version_code <= MAX_INTEGER
            or type(bundled_sequence) is not int or not 0 <= bundled_sequence < sequence):
        raise ValueError("version, sequence, minimum App version or bundled sequence is invalid")
    if output.exists() or output.is_symlink():
        raise ValueError("output must not exist; immutable releases cannot be overwritten")
    dist = dist.resolve()
    if output.resolve().is_relative_to(dist):
        raise ValueError("output must be outside dist")
    files = collect_files(dist)
    output.parent.mkdir(parents=True, exist_ok=True)
    release = f"web-{sequence}"
    with tempfile.TemporaryDirectory(prefix=".web-package-", dir=output.parent) as temporary:
        staging = Path(temporary)
        entries = []
        for source in files:
            relative = source.relative_to(dist).as_posix()
            target = staging / release / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
            data = target.read_bytes()
            entries.append({"path": relative, "url": f"{base_url}/{release}/{relative}",
                            "size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
        payload = json.dumps({"schema": 1, "version": version, "sequence": sequence,
                              "minAppVersionCode": min_app_version_code, "files": entries},
                             ensure_ascii=False, separators=(",", ":"))
        signing = subprocess.run(["openssl", "dgst", "-sha256", "-sign", str(key.resolve())],
                                 input=payload.encode(), capture_output=True, check=True)
        public = subprocess.run(["openssl", "pkey", "-in", str(key.resolve()), "-pubout", "-outform", "DER"],
                                capture_output=True, check=True).stdout
        # Native verifies RSA/SHA256. Reject EC/other key types rather than generating an unusable release.
        subprocess.run(["openssl", "rsa", "-pubin", "-inform", "DER", "-noout"],
                       input=public, capture_output=True, check=True)
        key_description = subprocess.run(["openssl", "pkey", "-pubin", "-inform", "DER", "-text", "-noout"],
                                         input=public, capture_output=True, check=True).stdout.decode()
        key_bits = re.search(r"Public-Key:\s*\((\d+) bit\)", key_description)
        if not key_bits or int(key_bits.group(1)) < 2048:
            raise ValueError("RSA signing key must be at least 2048 bits")
        envelope = {"payload": payload, "signature": base64.b64encode(signing.stdout).decode()}
        manifest = json.dumps(envelope, ensure_ascii=False, separators=(",", ":")).encode()
        if len(manifest) > MAX_MANIFEST:
            raise ValueError("signed manifest exceeds 256 KiB")
        (staging / "manifest.json").write_bytes(manifest)
        # This template is outside web-N/ and is only copied into the trusted APK by its builder.
        config = {"manifestUrl": f"{base_url}/manifest.json", "publicKey": base64.b64encode(public).decode(),
                  "bundledVersion": version, "bundledSequence": bundled_sequence}
        (staging / "dashboard-update.json").write_text(json.dumps(config, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        archive = staging / f"{release}.zip"
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as bundle:
            for source in sorted((staging / release).rglob("*")):
                if source.is_file():
                    info = zipfile.ZipInfo(source.relative_to(staging / release).as_posix(), date_time=(1980, 1, 1, 0, 0, 0))
                    info.compress_type = zipfile.ZIP_DEFLATED
                    info.external_attr = 0o100644 << 16
                    bundle.writestr(info, source.read_bytes())
        checksums = [f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.relative_to(staging).as_posix()}"
                     for path in sorted(staging.rglob("*")) if path.is_file()]
        (staging / "SHA256SUMS").write_text("\n".join(checksums) + "\n", encoding="utf-8")
        # Complete the local package before exposing it. Remote publishers must upload artifacts first.
        staging.rename(output)
    return envelope


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dist", type=Path, default=Path("vercel/dist"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--base-url", required=True, help="HTTPS directory containing manifest.json and web-N/")
    parser.add_argument("--key", type=Path, required=True, help="private RSA PEM key; never copied into the output")
    parser.add_argument("--version", help="display version; defaults to dist/web-version.json version")
    parser.add_argument("--sequence", type=int, required=True)
    parser.add_argument("--min-app-version-code", type=int, required=True)
    parser.add_argument("--bundled-sequence", type=int, default=0, help="sequence of the APK's bundled fallback, not the release sequence")
    args = parser.parse_args()
    try:
        version = args.version or json.loads((args.dist / "web-version.json").read_text(encoding="utf-8"))["version"]
        package_web(args.dist, args.output, args.base_url, args.key, version, args.sequence,
                    args.min_app_version_code, args.bundled_sequence)
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        parser.exit(2, f"error: {error}\n")
    print(f"Local Web release created: {args.output}; nothing uploaded")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
