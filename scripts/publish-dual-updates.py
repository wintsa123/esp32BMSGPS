#!/usr/bin/env python3
"""Prepare the trusted APK config or stage the same release for CF and GitHub. Does not upload."""
import argparse
import base64
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SOURCES = {
    "cf": "https://fuckbms.com/",
    "github": "https://raw.githubusercontent.com/wintsa123/espBmsSetting/updates/",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--key", type=Path, required=True)
    parser.add_argument("--sequence", type=int, required=True)
    parser.add_argument("--prepare-app", action="store_true")
    parser.add_argument("--apk", type=Path)
    args = parser.parse_args()
    if not 1 <= args.sequence <= 9007199254740991:
        parser.error("invalid sequence")
    if args.prepare_app:
        public = subprocess.run(["openssl", "pkey", "-in", str(args.key), "-pubout", "-outform", "DER"], capture_output=True, check=True).stdout
        config = {"manifestUrls": {name: base + f"web/manifest-{name}.json" for name, base in SOURCES.items()},
                  "publicKey": base64.b64encode(public).decode(), "bundledSequence": args.sequence}
        target = ROOT / "android-cast/app/src/main/assets/dashboard-update.json"
        target.write_text(json.dumps(config, indent=2) + "\n")
        print("Trusted public configuration prepared; no private key copied")
        return
    if not args.apk or not args.apk.is_file():
        parser.error("pass the freshly built --apk, or use --prepare-app before building it")
    modules = {}
    for name, filename in [("package", "package-web-update.py"), ("publish", "publish-update-server.py")]:
        spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / filename)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        modules[name] = module
    dist = ROOT / "vercel/dist"
    version = json.loads((dist / "web-version.json").read_text())["version"]
    if json.loads((dist / "web-update.json").read_text())["sequence"] != args.sequence:
        parser.error("build with WEB_UPDATE_SEQUENCE equal to --sequence")
    config = json.loads((ROOT / "android-cast/app/src/main/assets/dashboard-update.json").read_text())
    if config["bundledSequence"] != args.sequence:
        parser.error("prepare the APK configuration with this sequence before building")
    # Refuse an old APK paired with this workspace's newer catalog or signing configuration.
    with zipfile.ZipFile(args.apk) as archive:
        if json.loads(archive.read("assets/dashboard-update.json")) != config:
            parser.error("APK trust configuration does not match the release")
        if json.loads(archive.read("assets/dashboard/web-version.json"))["version"] != version:
            parser.error("APK bundles a different Web version; build the APK again")
    metadata = args.apk.parent / "output-metadata.json"
    expected_code, expected_name = modules["publish"].read_gradle_version()
    elements = json.loads(metadata.read_text())["elements"]
    if not any(item["outputFile"] == args.apk.name and item["versionCode"] == expected_code and item["versionName"] == expected_name for item in elements):
        parser.error("APK output metadata does not match the declared App version")
    web = dist / "web"
    if web.exists():
        parser.error("rebuild dist before staging another release")
    code, _ = modules["publish"].read_gradle_version()
    with tempfile.TemporaryDirectory(prefix="dual-web-", dir=ROOT / ".tools") as temporary:
        for source, base in SOURCES.items():
            output = Path(temporary) / source
            modules["package"].package_web(dist, output, base + "web", args.key, version, args.sequence, code)
            expected = json.loads((output / "dashboard-update.json").read_text())
            if expected["publicKey"] != config["publicKey"]:
                parser.error("signing key does not match the APK's trusted public key")
            web.mkdir(exist_ok=True)
            shutil.copyfile(output / "manifest.json", web / f"manifest-{source}.json")
            if source == "cf":
                shutil.copytree(output / f"web-{args.sequence}", web / f"web-{args.sequence}")
    manifest = modules["publish"].publish_apk(args.apk, False)
    manifest["sha256"] = hashlib.sha256(args.apk.read_bytes()).hexdigest()
    manifest["note"] = "GitHub / Cloudflare 双链路更新，独立手动检查与更新卡片交互优化"
    (ROOT / "vercel/public/apk/latest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    shutil.copytree(ROOT / "vercel/public/apk", dist / "apk", dirs_exist_ok=True)
    print(f"Staged {version}, sequence {args.sequence}, App code {code}; nothing uploaded")


if __name__ == "__main__":
    main()
