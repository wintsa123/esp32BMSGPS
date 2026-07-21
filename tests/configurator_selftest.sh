#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/esp32bmsgps-configurator.XXXXXX")"
mkdir -p "${repo_root}/firmware-builds"
profile_dir="$(mktemp -d "${repo_root}/firmware-builds/.configurator-requires.XXXXXX")"
trap 'rm -rf "${work_dir}" "${profile_dir}"' EXIT

expect_fail() {
    local expected="$1"
    shift
    local output
    if output="$("$@" 2>&1)"; then
        echo "expected failure: $*" >&2
        exit 1
    fi
    [[ "$output" == *"$expected"* ]] || {
        echo "missing expected error '$expected': $output" >&2
        exit 1
    }
}

"${repo_root}/start.sh" validate --modules ota --profile module-auto >"${work_dir}/modules.out"
grep -qx 'valid: profile=module-auto modules=network,ota' "${work_dir}/modules.out"

expect_fail 'dangerous' "${repo_root}/start.sh" validate --gpio TFT_DC=5
"${repo_root}/start.sh" validate --gpio TFT_DC=5 --confirm-dangerous-gpio >/dev/null
expect_fail 'assigned to both' "${repo_root}/start.sh" validate --gpio TFT_DC=4

cat >"${work_dir}/malicious.env" <<'EOF'
SCHEMA_VERSION=1
PROFILE=$(touch-payload)
EOF
expect_fail 'invalid value' "${repo_root}/start.sh" validate --config "${work_dir}/malicious.env"

cat >"${work_dir}/unknown.env" <<'EOF'
SCHEMA_VERSION=1
UNKNOWN=1
EOF
expect_fail 'unknown configuration key' "${repo_root}/start.sh" validate --config "${work_dir}/unknown.env"

cat >"${work_dir}/duplicate.env" <<'EOF'
SCHEMA_VERSION=1
PROFILE=one
PROFILE=two
EOF
expect_fail 'duplicate key PROFILE' "${repo_root}/start.sh" validate --config "${work_dir}/duplicate.env"

cp -a "${repo_root}/firmware/catalog" "${work_dir}/catalog"
sed -i 's/^REQUIRES_MODULES=$/REQUIRES_MODULES=gps/' "${work_dir}/catalog/module/bms.env"
sed -i 's/^REQUIRES_MODULES=$/REQUIRES_MODULES=bms/' "${work_dir}/catalog/module/gps.env"
expect_fail 'dependency cycle' env FIRMWARE_CATALOG_DIR="${work_dir}/catalog" "${repo_root}/start.sh" validate --modules bms

cat >"${work_dir}/golden.env" <<'EOF'
SCHEMA_VERSION=1
PROFILE=golden
MCU=esp32
BOARD=esp32-wroom-32e-legacy
DISPLAY=st7789-spi
INPUT=xpt2046-spi
MODULES=ota
EOF
FIRMWARE_BUILD_ROOT="${work_dir}/bash-build" "${repo_root}/start.sh" configure --config "${work_dir}/golden.env" >/dev/null
FIRMWARE_BUILD_ROOT="${work_dir}/no-audio-build" "${repo_root}/start.sh" configure --profile no-audio --modules gps >/dev/null
! rg -q 'esp_bms_audio_feedback' "${work_dir}/no-audio-build/no-audio/generated/profile.cmake"
rg -qx 'TRIMMING=audio-component-excluded;legacy-runtime-untrimmed' "${work_dir}/no-audio-build/no-audio/report.txt"

cat >"${profile_dir}/profile.cmake" <<'EOF'
set(ESP_BMS_FEATURE_AUDIO 0)
set(ESP_BMS_FEATURE_BMS 0)
set(ESP_BMS_PROFILE_MAIN_REQUIRES "esp_bms_idf_runtime;esp_bms_lvgl_bridge;esp_bms_lvgl_ui;lvgl;esp_lvgl_adapter")
EOF
cat >"${work_dir}/early-requires.cmake" <<EOF
set(CMAKE_BUILD_EARLY_EXPANSION 1)
macro(idf_component_register)
    cmake_parse_arguments(profile "" "" "REQUIRES" \${ARGN})
    file(WRITE "\${OUTPUT_FILE}" "\${profile_REQUIRES}\\n")
endmacro()
macro(target_compile_definitions)
endmacro()
include("${repo_root}/main/CMakeLists.txt")
EOF
ESP_BMS_PROFILE_FILE="${profile_dir}/profile.cmake" cmake -DOUTPUT_FILE="${work_dir}/early-requires.out" -P "${work_dir}/early-requires.cmake"
! rg -q 'esp_bms_audio_feedback' "${work_dir}/early-requires.out"

if command -v pwsh >/dev/null 2>&1; then
    FIRMWARE_BUILD_ROOT="${work_dir}/powershell-build" pwsh -NoProfile -File "${repo_root}/start.ps1" configure --config "${work_dir}/golden.env" >/dev/null
    cmp "${work_dir}/bash-build/golden/normalized.env" "${work_dir}/powershell-build/golden/normalized.env"
else
    echo 'PowerShell comparison skipped: pwsh is unavailable'
fi

echo 'firmware configurator self-test passed'
