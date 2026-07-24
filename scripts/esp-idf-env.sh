#!/usr/bin/env bash
set -euo pipefail

export https_proxy="${https_proxy:-http://127.0.0.1:7897}"
export http_proxy="${http_proxy:-http://127.0.0.1:7897}"
export all_proxy="${all_proxy:-socks5://127.0.0.1:7897}"

readonly ESP_IDF_REQUIRED_VERSION="ESP-IDF v6.0.2"
readonly PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly PROJECT_IDF_PATH="$PROJECT_ROOT/esp-idf-v6.0.2"
readonly PROJECT_TOOLS_PATH="$PROJECT_ROOT/esp-idf-tools"
configured_idf_path=''
idf_path_config="${XDG_CONFIG_HOME:-$HOME/.config}/esp32-bms-gps/idf-path"

if [[ -z "${IDF_PATH:-}" && -r "$idf_path_config" ]]; then
    IFS= read -r configured_idf_path < "$idf_path_config" || true
fi

if [[ -z "${IDF_PATH:-}" && -f "$configured_idf_path/export.sh" ]]; then
    export IDF_PATH="$configured_idf_path"
elif [[ -z "${IDF_PATH:-}" && -f "$PROJECT_IDF_PATH/export.sh" ]]; then
    export IDF_PATH="$PROJECT_IDF_PATH"
elif [[ -z "${IDF_PATH:-}" && -f "$HOME/esp/esp-idf-v6.0.2/export.sh" ]]; then
    export IDF_PATH="$HOME/esp/esp-idf-v6.0.2"
fi

if [[ -z "${IDF_TOOLS_PATH:-}" && "$IDF_PATH" == "$PROJECT_IDF_PATH" && -d "$PROJECT_TOOLS_PATH" ]]; then
    export IDF_TOOLS_PATH="$PROJECT_TOOLS_PATH"
fi

if [[ -f "${IDF_PATH:-}/export.sh" ]]; then
    # shellcheck source=/dev/null
    source "$IDF_PATH/export.sh"
else
    printf 'missing ESP-IDF v6.0.2 export.sh; set IDF_PATH, run ./start.sh install-idf, or keep esp-idf-v6.0.2 in the project root\n' >&2
    exit 127
fi

actual_version="$(idf.py --version)"
if [[ "$actual_version" != "$ESP_IDF_REQUIRED_VERSION" ]]; then
    printf 'unsupported ESP-IDF version: expected %s, got %s\n' "$ESP_IDF_REQUIRED_VERSION" "$actual_version" >&2
    exit 2
fi

exec idf.py "$@"
