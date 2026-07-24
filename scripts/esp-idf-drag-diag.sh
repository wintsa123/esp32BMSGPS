#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: scripts/esp-idf-drag-diag.sh [options] [idf.py args...]

Build or flash an ESP-IDF LVGL drag diagnostic image without changing the
normal sdkconfig/build directory.

Options:
  --double-buffer       Also enable CONFIG_ESP_BMS_LVGL_BRIDGE_DOUBLE_BUFFER.
  --no-full-invalidate  Disable full dashboard invalidation for A/B testing.
  --build-dir DIR       Override the diagnostic build directory.
  --sdkconfig FILE      Override the generated diagnostic sdkconfig path.
  -h, --help            Show this help.

Examples:
  scripts/esp-idf-drag-diag.sh build
  scripts/esp-idf-drag-diag.sh -p /dev/ttyUSB0 flash monitor
  scripts/esp-idf-drag-diag.sh --no-full-invalidate build
  scripts/esp-idf-drag-diag.sh --double-buffer build
USAGE
}

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir=""
sdkconfig_path=""
double_buffer=false
full_invalidate=true
idf_args=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --double-buffer)
            double_buffer=true
            shift
            ;;
        --no-full-invalidate)
            full_invalidate=false
            shift
            ;;
        --build-dir)
            if [[ $# -lt 2 ]]; then
                echo "--build-dir requires a value" >&2
                exit 2
            fi
            build_dir="$2"
            shift 2
            ;;
        --sdkconfig)
            if [[ $# -lt 2 ]]; then
                echo "--sdkconfig requires a value" >&2
                exit 2
            fi
            sdkconfig_path="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            idf_args+=("$@")
            break
            ;;
        *)
            idf_args+=("$1")
            shift
            ;;
    esac
done

if [[ ${#idf_args[@]} -eq 0 ]]; then
    idf_args=(build)
fi

if [[ -z "$build_dir" ]]; then
    if [[ "$double_buffer" == true ]]; then
        build_dir="build_dragdiag_db"
    else
        build_dir="build_dragdiag"
    fi
fi

if [[ -z "$sdkconfig_path" ]]; then
    if [[ "$double_buffer" == true ]]; then
        sdkconfig_path="config/sdkconfig/sdkconfig.dragdiag-db"
    else
        sdkconfig_path="config/sdkconfig/sdkconfig.dragdiag"
    fi
fi

sdkconfig_defaults="config/sdkconfig/sdkconfig.defaults;config/sdkconfig/sdkconfig.defaults.dragdiag"
if [[ "$full_invalidate" == false ]]; then
    sdkconfig_defaults="${sdkconfig_defaults};config/sdkconfig/sdkconfig.defaults.dragdiag-no-full-invalidate"
fi
if [[ "$double_buffer" == true ]]; then
    sdkconfig_defaults="${sdkconfig_defaults};config/sdkconfig/sdkconfig.defaults.dragdiag-double-buffer"
fi

cd "$repo_root"
echo "Using diagnostic build dir: $build_dir"
echo "Using diagnostic sdkconfig: $sdkconfig_path"
echo "Using defaults: $sdkconfig_defaults"

exec "$repo_root/scripts/esp-idf-env.sh" \
    -B "$build_dir" \
    -DSDKCONFIG="$sdkconfig_path" \
    -DSDKCONFIG_DEFAULTS="$sdkconfig_defaults" \
    "${idf_args[@]}"
