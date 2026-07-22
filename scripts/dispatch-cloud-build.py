#!/usr/bin/env python3
"""Dispatch a validated firmware profile to the repository's Actions workflow."""

from __future__ import annotations

import argparse
import base64
import getpass
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORKFLOW_FILE = "cloud-build.yml"
GITHUB_API = "https://api.github.com"
GITHUB_SEGMENT = re.compile(r"[A-Za-z0-9_.-]+\Z")


class DispatchError(Exception):
    pass


def message(english: str, chinese: str) -> str:
    return english if os.environ.get("FIRMWARE_LANG") == "en" else chinese


def git_output(arguments: list[str]) -> str:
    result = subprocess.run(
        ["git", *arguments],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
        check=False,
    )
    if result.returncode:
        raise DispatchError(message("Git verification failed.", "Git 校验失败。"))
    return result.stdout.strip()


def github_repository(origin: str) -> str:
    origin = origin.strip()
    if origin.startswith("git@github.com:"):
        path = origin.removeprefix("git@github.com:")
    else:
        parsed = urllib.parse.urlparse(origin)
        if parsed.hostname != "github.com":
            raise DispatchError(message("origin must point to github.com.", "origin 必须指向 github.com。"))
        path = parsed.path
    path = path.strip("/")
    if path.endswith(".git"):
        path = path[:-4]
    parts = path.split("/")
    if len(parts) != 2 or not all(GITHUB_SEGMENT.fullmatch(part) for part in parts):
        raise DispatchError(message("origin is not a valid GitHub repository.", "origin 不是有效的 GitHub 仓库。"))
    return "/".join(parts)


def pushed_branch() -> tuple[str, str]:
    branch = git_output(["rev-parse", "--abbrev-ref", "HEAD"])
    if branch == "HEAD":
        raise DispatchError(message("cloud builds require a checked-out branch.", "云端构建需要当前已检出的分支。"))
    local_head = git_output(["rev-parse", "HEAD"])
    remote = git_output(["ls-remote", "--heads", "origin", f"refs/heads/{branch}"])
    remote_head = remote.split(maxsplit=1)[0] if remote else ""
    if remote_head != local_head:
        raise DispatchError(
            message(
                "current branch HEAD is not pushed to origin; push it before cloud building.",
                "当前分支 HEAD 尚未推送到 origin；请先推送后再云端构建。",
            )
        )
    return branch, local_head


def cloud_token() -> str:
    token = os.environ.get("ESP_BMS_GITHUB_TOKEN", "").strip()
    if not token and sys.stdin.isatty():
        token = getpass.getpass(message("GitHub token: ", "GitHub Token：")).strip()
    if not token:
        raise DispatchError(
            message(
                "missing cloud build token; set ESP_BMS_GITHUB_TOKEN or run interactively.",
                "缺少云端构建 Token；请设置 ESP_BMS_GITHUB_TOKEN 或在交互终端运行。",
            )
        )
    return token


def dispatch_request(repository: str, branch: str, configuration: bytes, token: str) -> urllib.request.Request:
    payload = json.dumps(
        {
            "ref": branch,
            "inputs": {"firmware_env_base64": base64.b64encode(configuration).decode("ascii")},
        },
        separators=(",", ":"),
    ).encode("utf-8")
    return urllib.request.Request(
        f"{GITHUB_API}/repos/{repository}/actions/workflows/{WORKFLOW_FILE}/dispatches",
        data=payload,
        headers={
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {token}",
            "User-Agent": "esp32-bms-gps-cloud-build",
            "X-GitHub-Api-Version": "2022-11-28",
        },
        method="POST",
    )


def send_dispatch(request: urllib.request.Request, opener=urllib.request.urlopen) -> None:
    try:
        with opener(request, timeout=30) as response:
            if response.status != 204:
                raise DispatchError(message("cloud build dispatch was rejected.", "云端构建分派被拒绝。"))
    except urllib.error.HTTPError as error:
        raise DispatchError(message(f"cloud build API returned HTTP {error.code}.", f"云端构建 API 返回 HTTP {error.code}。")) from error
    except urllib.error.URLError as error:
        raise DispatchError(message("cloud build API request failed.", "云端构建 API 请求失败。")) from error


def dispatch(config_path: Path) -> tuple[str, str, str]:
    if not config_path.is_file() or not config_path.read_bytes():
        raise DispatchError(message("missing generated firmware.env.", "缺少生成的 firmware.env。"))
    repository = github_repository(git_output(["remote", "get-url", "origin"]))
    branch, head = pushed_branch()
    request = dispatch_request(repository, branch, config_path.read_bytes(), cloud_token())
    send_dispatch(request)
    return repository, branch, head


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", required=True, type=Path, help="validated firmware.env path")
    arguments = parser.parse_args()
    try:
        repository, branch, head = dispatch(arguments.config)
    except DispatchError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    print(message(
        f"cloud build dispatched: {repository}@{branch} ({head[:12]})",
        f"已分派云端构建：{repository}@{branch}（{head[:12]}）",
    ))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
