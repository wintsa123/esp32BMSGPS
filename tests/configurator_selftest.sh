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

"${repo_root}/start.sh" validate --lang en --modules ota --profile module-auto >"${work_dir}/modules.out"
grep -qx 'valid: profile=module-auto modules=network,ota' "${work_dir}/modules.out"

expect_fail 'dangerous' "${repo_root}/start.sh" validate --lang en --gpio TFT_DC=5
"${repo_root}/start.sh" validate --lang en --gpio TFT_DC=5 --confirm-dangerous-gpio >/dev/null
expect_fail 'assigned to both' "${repo_root}/start.sh" validate --lang en --gpio TFT_DC=4

cat >"${work_dir}/malicious.env" <<'EOF'
SCHEMA_VERSION=1
PROFILE=$(touch-payload)
EOF
expect_fail 'invalid value' "${repo_root}/start.sh" validate --lang en --config "${work_dir}/malicious.env"

cat >"${work_dir}/unknown.env" <<'EOF'
SCHEMA_VERSION=1
UNKNOWN=1
EOF
expect_fail 'unknown configuration key' "${repo_root}/start.sh" validate --lang en --config "${work_dir}/unknown.env"

cat >"${work_dir}/duplicate.env" <<'EOF'
SCHEMA_VERSION=1
PROFILE=one
PROFILE=two
EOF
expect_fail 'duplicate key PROFILE' "${repo_root}/start.sh" validate --lang en --config "${work_dir}/duplicate.env"

cp -a "${repo_root}/firmware/catalog" "${work_dir}/catalog"
sed -i 's/^REQUIRES_MODULES=$/REQUIRES_MODULES=gps/' "${work_dir}/catalog/module/bms.env"
sed -i 's/^REQUIRES_MODULES=$/REQUIRES_MODULES=bms/' "${work_dir}/catalog/module/gps.env"
expect_fail 'dependency cycle' env FIRMWARE_CATALOG_DIR="${work_dir}/catalog" "${repo_root}/start.sh" validate --lang en --modules bms

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
rg -qx 'PROFILE=golden' "${work_dir}/bash-build/golden/firmware.env"
rg -qx 'MODULES=network,ota' "${work_dir}/bash-build/golden/firmware.env"
rg -qx 'MODULES=gps' "${work_dir}/no-audio-build/no-audio/firmware.env"
[[ "$(find "${work_dir}/bash-build/golden" -maxdepth 1 -type f | wc -l)" == 1 ]]

expect_fail 'missing configuration file' "${repo_root}/scripts/build-profile.sh" --lang en --config "${work_dir}/missing.env"

"${repo_root}/start.sh" validate --modules ota --profile chinese-default >"${work_dir}/chinese.out"
grep -qx '校验通过：配置档=chinese-default 模块=network,ota' "${work_dir}/chinese.out"
"${repo_root}/start.sh" --lang zh help >"${work_dir}/chinese-help.out"
rg -q '^用法：' "${work_dir}/chinese-help.out"
expect_fail 'invalid language' "${repo_root}/start.sh" validate --lang en --lang ja

printf '2\n\n\n\n\n\n\n' | FIRMWARE_BUILD_ROOT="${work_dir}/interactive-build" "${repo_root}/start.sh" >"${work_dir}/interactive.out"
rg -q '^请选择语言 / Select language$' "${work_dir}/interactive.out"
rg -q '^config: .*/interactive-build/legacy/firmware.env$' "${work_dir}/interactive.out"
! rg -q '^LANGUAGE=' "${work_dir}/interactive-build/legacy/firmware.env"

printf 'invalid\n2\n\n\n\n\n\n\n' | FIRMWARE_BUILD_ROOT="${work_dir}/interactive-retry-build" "${repo_root}/start.sh" >"${work_dir}/interactive-retry.out" 2>"${work_dir}/interactive-retry.err"
rg -q '^请输入 1、2、zh 或 en。 / Enter 1, 2, zh, or en\.$' "${work_dir}/interactive-retry.err"
rg -q '^config: .*/interactive-retry-build/legacy/firmware.env$' "${work_dir}/interactive-retry.out"

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
    FIRMWARE_BUILD_ROOT="${work_dir}/powershell-build" pwsh -NoProfile -File "${repo_root}/start.ps1" configure --lang en --config "${work_dir}/golden.env" >/dev/null
    cmp "${work_dir}/bash-build/golden/firmware.env" "${work_dir}/powershell-build/golden/normalized.env"
else
    echo 'PowerShell comparison skipped: pwsh is unavailable'
fi

echo 'firmware configurator self-test passed'
