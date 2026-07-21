#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/esp32bmsgps-selftests.XXXXXX")"
trap 'rm -rf "${build_dir}"' EXIT

cc_bin="${CC:-cc}"
cflags=(-std=c11 -Wall -Wextra -Werror)

"${cc_bin}" "${cflags[@]}" \
    -I"${repo_root}/components/esp_bms_gps/include" \
    "${repo_root}/tests/gps_stream_selftest.c" \
    "${repo_root}/components/esp_bms_gps/esp_bms_gps_stream.c" \
    -o "${build_dir}/gps_stream_selftest"
"${build_dir}/gps_stream_selftest"

"${cc_bin}" "${cflags[@]}" \
    -I"${repo_root}/components/esp_bms_idf_runtime/include" \
    -I"${repo_root}/components/esp_bms_gps/include" \
    "${repo_root}/tests/speed_dashboard_selftest.c" \
    "${repo_root}/components/esp_bms_idf_runtime/esp_bms_speed_dashboard.c" \
    "${repo_root}/components/esp_bms_gps/esp_bms_gps_stream.c" \
    -o "${build_dir}/speed_dashboard_selftest"
"${build_dir}/speed_dashboard_selftest"

"${cc_bin}" "${cflags[@]}" \
    -I"${repo_root}/components/esp_fardriver_protocol/include" \
    "${repo_root}/tests/fardriver_protocol_selftest.c" \
    "${repo_root}/components/esp_fardriver_protocol/esp_fardriver_protocol.c" \
    -lm \
    -o "${build_dir}/fardriver_protocol_selftest"
"${build_dir}/fardriver_protocol_selftest"
printf '%s\n' "FarDriver protocol self-test passed"

python3 "${repo_root}/scripts/push-agnss.py" --self-test
python3 "${repo_root}/scripts/build-firmware.py" --self-test
