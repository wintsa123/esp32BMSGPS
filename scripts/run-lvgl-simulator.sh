#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${repo_root}/build-simulator"

cmake -S "${repo_root}/simulator" -B "${build_dir}"
cmake --build "${build_dir}" --parallel
exec "${build_dir}/esp_bms_lvgl_simulator" "$@"
