#!/usr/bin/env bash
set -euo pipefail

export https_proxy="${https_proxy:-http://127.0.0.1:7897}"
export http_proxy="${http_proxy:-http://127.0.0.1:7897}"
export all_proxy="${all_proxy:-socks5://127.0.0.1:7897}"

if [[ -f "${IDF_PATH:-}/export.sh" ]]; then
    # shellcheck source=/dev/null
    source "$IDF_PATH/export.sh"
elif [[ -f "$HOME/esp/esp-idf-v5.5.4/export.sh" ]]; then
    # shellcheck source=/dev/null
    source "$HOME/esp/esp-idf-v5.5.4/export.sh"
fi

idf.py "$@"
