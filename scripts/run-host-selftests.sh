#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$(mktemp -d "${repo_root}/.selftests.XXXXXX")"
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
    -I"${repo_root}/components/esp_bms_idf_runtime/include" \
    "${repo_root}/tests/ride_records_selftest.c" \
    "${repo_root}/components/esp_bms_idf_runtime/esp_bms_ride_records.c" \
    -o "${build_dir}/ride_records_selftest"
"${build_dir}/ride_records_selftest"

"${cc_bin}" "${cflags[@]}" \
    -I"${repo_root}/components/esp_fardriver_protocol/include" \
    "${repo_root}/tests/fardriver_protocol_selftest.c" \
    "${repo_root}/components/esp_fardriver_protocol/esp_fardriver_protocol.c" \
    -lm \
    -o "${build_dir}/fardriver_protocol_selftest"
"${build_dir}/fardriver_protocol_selftest"
printf '%s\n' "FarDriver protocol self-test passed"

"${cc_bin}" "${cflags[@]}" \
    -I"${repo_root}/components/esp_bms_bms_ble/protocols" \
    -I"${repo_root}/components/esp_bms_bms_ble/protocols/yanyang" \
    "${repo_root}/tests/yanyang_bms_protocol_selftest.c" \
    "${repo_root}/components/esp_bms_bms_ble/protocols/yanyang/esp_bms_yanyang_protocol.c" \
    -o "${build_dir}/yanyang_bms_protocol_selftest"
"${build_dir}/yanyang_bms_protocol_selftest"
printf '%s\n' "Yanyang BMS protocol self-test passed"

"${cc_bin}" "${cflags[@]}" \
    -I"${repo_root}/components/esp_bms_bms_ble/protocols" \
    -I"${repo_root}/components/esp_bms_bms_ble/protocols/ant" \
    "${repo_root}/tests/ant_bms_protocol_selftest.c" \
    "${repo_root}/components/esp_bms_bms_ble/protocols/ant/esp_bms_ant_protocol.c" \
    -o "${build_dir}/ant_bms_protocol_selftest"
"${build_dir}/ant_bms_protocol_selftest"
printf '%s\n' "ANT BMS protocol self-test passed"

for brand in jk jbd daly; do
    "${cc_bin}" "${cflags[@]}" \
        -I"${repo_root}/components/esp_bms_bms_ble/protocols" \
        -I"${repo_root}/components/esp_bms_bms_ble/protocols/${brand}" \
        "${repo_root}/tests/${brand}_bms_protocol_selftest.c" \
        "${repo_root}/components/esp_bms_bms_ble/protocols/${brand}/esp_bms_${brand}_protocol.c" \
        -o "${build_dir}/${brand}_bms_protocol_selftest"
    "${build_dir}/${brand}_bms_protocol_selftest"
done
printf '%s\n' "JK/JBD/Daly BMS protocol self-tests passed"

python3 "${repo_root}/scripts/push-agnss.py" --self-test
python3 "${repo_root}/scripts/build-firmware.py" --self-test
