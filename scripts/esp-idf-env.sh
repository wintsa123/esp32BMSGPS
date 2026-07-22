#!/usr/bin/env bash
set -euo pipefail

export https_proxy="${https_proxy:-http://127.0.0.1:7897}"
export http_proxy="${http_proxy:-http://127.0.0.1:7897}"
export all_proxy="${all_proxy:-socks5://127.0.0.1:7897}"

readonly ESP_IDF_REQUIRED_VERSION="ESP-IDF v6.0.2"

if [[ -f "${IDF_PATH:-}/export.sh" ]]; then
    # shellcheck source=/dev/null
    source "$IDF_PATH/export.sh"
elif [[ -f "$HOME/esp/esp-idf-v6.0.2/export.sh" ]]; then
    # shellcheck source=/dev/null
    source "$HOME/esp/esp-idf-v6.0.2/export.sh"
else
    printf 'missing ESP-IDF v6.0.2 export.sh; set IDF_PATH or install it under %s/esp/esp-idf-v6.0.2\n' "$HOME" >&2
    exit 127
fi

actual_version="$(idf.py --version)"
if [[ "$actual_version" != "$ESP_IDF_REQUIRED_VERSION" ]]; then
    printf 'unsupported ESP-IDF version: expected %s, got %s\n' "$ESP_IDF_REQUIRED_VERSION" "$actual_version" >&2
    exit 2
fi

exec idf.py "$@"
