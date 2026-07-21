#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
language="${FIRMWARE_LANG:-zh}"

case "$language" in
    zh|en) ;;
    *) language=zh ;;
esac

if [[ "${1:-}" == --lang ]]; then
    [[ $# -ge 2 ]] || {
        [[ "$language" == en ]] && echo 'error: --lang requires zh or en' >&2 || echo '错误：--lang 需要 zh 或 en' >&2
        exit 2
    }
    language="$2"
    shift 2
    case "$language" in
        zh|en) ;;
        *)
            [[ "$language" == en ]] && echo "error: invalid language: $language" >&2 || echo "错误：无效的语言：$language" >&2
            exit 2
            ;;
    esac
fi

if [[ $# -ne 2 || "$1" != "--config" ]]; then
    if [[ "$language" == en ]]; then
        echo "Usage: $0 [--lang zh|en] --config firmware.env" >&2
    else
        echo "用法：$0 [--lang zh|en] --config firmware.env" >&2
    fi
    exit 2
fi

config="$2"
[[ -f "$config" ]] || {
    if [[ "$language" == en ]]; then
        echo "error: missing configuration file: $config" >&2
    else
        echo "错误：缺少配置文件：$config" >&2
    fi
    exit 2
}

exec env FIRMWARE_LANG="$language" "$root/start.sh" compile-local --config "$config"
