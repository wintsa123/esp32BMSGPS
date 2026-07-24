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
grep -qx 'valid: profile=module-auto modules=network,ota dashboards=' "${work_dir}/modules.out"

FIRMWARE_BUILD_ROOT="${work_dir}/dashboard-fireblade-build" "${repo_root}/start.sh" configure --lang en --profile dashboard-fireblade --dashboards fireblade >/dev/null
rg -qx 'DASHBOARDS=fireblade' "${work_dir}/dashboard-fireblade-build/dashboard-fireblade/firmware.env"
expect_fail 'select at least one dashboard UI' "${repo_root}/start.sh" validate --lang en --dashboards ''
expect_fail 'missing file:' "${repo_root}/start.sh" validate --lang en --dashboards unknown
expect_fail 'controller dashboard requires controller module' "${repo_root}/start.sh" validate --lang en --modules gps --dashboards controller --gpio GPS_RX=37 --gpio GPS_PPS=47 --gpio GPS_TX=48

expect_fail 'dangerous' "${repo_root}/start.sh" validate --lang en --gpio TFT_DC=0
"${repo_root}/start.sh" validate --lang en --gpio TFT_DC=0 --confirm-dangerous-gpio >/dev/null
expect_fail 'assigned to both' "${repo_root}/start.sh" validate --lang en --gpio TFT_DC=4
expect_fail 'missing file:' "${repo_root}/start.sh" validate --lang en --mcu esp32s3 --board esp32s3-wroom-1-n16r8-i80 --display ili9488-i80 --input ft6336u-i2c

FIRMWARE_BUILD_ROOT="${work_dir}/s3-default-build" "${repo_root}/start.sh" configure --profile s3-default >/dev/null
rg -qx 'MCU=esp32s3' "${work_dir}/s3-default-build/s3-default/firmware.env"
rg -qx 'BOARD=esp32s3-n16r8-st7796u-gt1151' "${work_dir}/s3-default-build/s3-default/firmware.env"
rg -qx 'DISPLAY=st7796u-i80' "${work_dir}/s3-default-build/s3-default/firmware.env"
rg -qx 'INPUT=gt1151-i2c' "${work_dir}/s3-default-build/s3-default/firmware.env"
rg -qx 'GPIO_TFT_D15=4' "${work_dir}/s3-default-build/s3-default/firmware.env"
rg -qx 'GPIO_TOUCH_INT=42' "${work_dir}/s3-default-build/s3-default/firmware.env"
! rg -q '^GPIO_GPS_' "${work_dir}/s3-default-build/s3-default/firmware.env"
expect_fail 'missing required input GPIO role GPS_PPS' "${repo_root}/start.sh" validate --lang en --profile s3-missing-gps --modules gps
FIRMWARE_BUILD_ROOT="${work_dir}/s3-gps-build" "${repo_root}/start.sh" configure --lang en --profile s3-gps --modules gps --gpio GPS_RX=37 --gpio GPS_PPS=47 --gpio GPS_TX=48 >/dev/null
rg -qx 'GPIO_GPS_RX=37' "${work_dir}/s3-gps-build/s3-gps/firmware.env"
rg -qx 'GPIO_GPS_PPS=47' "${work_dir}/s3-gps-build/s3-gps/firmware.env"
rg -qx 'GPIO_GPS_TX=48' "${work_dir}/s3-gps-build/s3-gps/firmware.env"
rg -qx 'DASHBOARDS=fireblade,s1000rr' "${work_dir}/s3-gps-build/s3-gps/firmware.env"

FIRMWARE_BUILD_ROOT="${work_dir}/no-cast-build" "${repo_root}/start.sh" configure --lang en --profile no-cast --mcu esp32 --board esp32-wroom-32e-legacy --display st7789-spi --input xpt2046-spi --modules audio,bms,controller,gps,network,ota --dashboards fireblade >/dev/null
rg -qx 'MODULES=audio,bms,controller,gps,network,ota' "${work_dir}/no-cast-build/no-cast/firmware.env"

FIRMWARE_BUILD_ROOT="${work_dir}/version-build" "${repo_root}/start.sh" configure --lang en --profile version-test --firmware-version v1.2.3 >/dev/null
rg -qx 'FIRMWARE_VERSION=v1.2.3' "${work_dir}/version-build/version-test/firmware.env"
python3 "${repo_root}/scripts/generate-hardware-config.py" --catalog "${repo_root}/firmware/catalog" --firmware-env "${work_dir}/version-build/version-test/firmware.env" --output "${work_dir}/version-test.h"
rg -Fx '#define ESP_BMS_PROFILE_FIRMWARE_VERSION "v1.2.3"' "${work_dir}/version-test.h"

FIRMWARE_BUILD_ROOT="${work_dir}/audio-legacy-build" "${repo_root}/start.sh" configure --lang en --profile audio-legacy --mcu esp32 --board esp32-wroom-32e-legacy --display st7789-spi --input xpt2046-spi --modules audio >/dev/null
rg -qx 'GPIO_TFT_BACKLIGHT=21' "${work_dir}/audio-legacy-build/audio-legacy/firmware.env"
rg -qx 'GPIO_AUDIO_DAC=26' "${work_dir}/audio-legacy-build/audio-legacy/firmware.env"
rg -qx 'GPIO_AUDIO_ENABLE=4' "${work_dir}/audio-legacy-build/audio-legacy/firmware.env"
rg -qx 'GPIO_BATTERY_ADC=34' "${work_dir}/audio-legacy-build/audio-legacy/firmware.env"
python3 "${repo_root}/scripts/generate-hardware-config.py" --catalog "${repo_root}/firmware/catalog" --firmware-env "${work_dir}/audio-legacy-build/audio-legacy/firmware.env" --output "${work_dir}/audio-legacy.h"
rg -Fq '.pin_backlight = (gpio_num_t)21' "${work_dir}/audio-legacy.h"
sed '/^GPIO_TFT_BACKLIGHT=/d' "${work_dir}/audio-legacy-build/audio-legacy/firmware.env" >"${work_dir}/no-backlight.env"
python3 "${repo_root}/scripts/generate-hardware-config.py" --catalog "${repo_root}/firmware/catalog" --firmware-env "${work_dir}/no-backlight.env" --output "${work_dir}/no-backlight.h"
rg -Fq '.pin_backlight = GPIO_NUM_NC' "${work_dir}/no-backlight.h"
rg -Fx '#define ESP_BMS_PROFILE_BATTERY_ADC (gpio_num_t)34' "${work_dir}/audio-legacy.h"
rg -Fx '#define ESP_BMS_PROFILE_AUDIO_BACKEND ESP_BMS_PROFILE_AUDIO_BACKEND_DAC' "${work_dir}/audio-legacy.h"

expect_fail 'does not provide an audio hardware profile' "${repo_root}/start.sh" validate --lang en --profile audio-st7796 --mcu esp32s3 --board esp32s3-n16r8-st7796u-gt1151 --display st7796u-i80 --input gt1151-i2c --modules audio

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
expect_fail 'dependency cycle' env FIRMWARE_CATALOG_DIR="${work_dir}/catalog" "${repo_root}/start.sh" validate --lang en --mcu esp32 --board esp32-wroom-32e-legacy --display st7789-spi --input xpt2046-spi --modules bms

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
FIRMWARE_BUILD_ROOT="${work_dir}/no-audio-build" "${repo_root}/start.sh" configure --profile no-audio --modules bms >/dev/null
rg -qx 'PROFILE=golden' "${work_dir}/bash-build/golden/firmware.env"
rg -qx 'MODULES=network,ota' "${work_dir}/bash-build/golden/firmware.env"
rg -qx 'MODULES=bms' "${work_dir}/no-audio-build/no-audio/firmware.env"
rg -qx 'DASHBOARDS=' "${work_dir}/no-audio-build/no-audio/firmware.env"
[[ "$(find "${work_dir}/bash-build/golden" -maxdepth 1 -type f | wc -l)" == 1 ]]

saved_build_root="${work_dir}/saved-build"
FIRMWARE_BUILD_ROOT="$saved_build_root" "${repo_root}/start.sh" configure --lang en --profile saved-s3 --modules gps --gpio GPS_RX=37 --gpio GPS_PPS=47 --gpio GPS_TX=48 >/dev/null
mkdir -p "$saved_build_root/.ignored" "$saved_build_root/invalid-saved"
printf 'SCHEMA_VERSION=1\nUNKNOWN=1\n' >"$saved_build_root/invalid-saved/firmware.env"
fake_idf_root="${work_dir}/fake-idf"
mkdir -p "$fake_idf_root/bin"
cat >"$fake_idf_root/export.sh" <<EOF
export PATH="$fake_idf_root/bin:\$PATH"
EOF
cat >"$fake_idf_root/bin/idf.py" <<'EOF'
#!/usr/bin/env bash
if [[ "${1:-}" == --version ]]; then
    printf '%s\n' 'ESP-IDF v6.0.2'
    exit 0
fi
args=("$@")
for ((index = 0; index < ${#args[@]}; index++)); do
    if [[ "${args[$index]}" == -B ]]; then
        mkdir -p "${args[$((index + 1))]}"
        : >"${args[$((index + 1))]}/esp32_bms_gps_idf.bin"
        break
    fi
done
printf '%s\n' "$@" >"${FAKE_IDF_ARGS:?}"
EOF
chmod +x "$fake_idf_root/bin/idf.py"

IDF_PATH="$fake_idf_root" \
    FIRMWARE_BUILD_ROOT="${work_dir}/dashboard-fireblade-build" \
    ESP_BMS_IDF_BUILD_ROOT="${work_dir}/dashboard-fireblade-idf-build" \
    FIRMWARE_OUTPUT_ROOT="${work_dir}/dashboard-fireblade-output" \
    FAKE_IDF_ARGS="${work_dir}/dashboard-fireblade-idf.args" \
    "${repo_root}/start.sh" compile-local --lang en --profile dashboard-fireblade --dashboards fireblade >/dev/null
rg -Fx 'set(ESP_BMS_FEATURE_DASHBOARD_S1000RR 0 CACHE BOOL "Firmware profile S1000RR dashboard" FORCE)' "${work_dir}/dashboard-fireblade-build/dashboard-fireblade/generated/profile.cmake"
rg -Fx 'set(ESP_BMS_FEATURE_DASHBOARD_CONTROLLER 0 CACHE BOOL "Firmware profile controller dashboard" FORCE)' "${work_dir}/dashboard-fireblade-build/dashboard-fireblade/generated/profile.cmake"
rg -Fx 'set(ESP_BMS_FEATURE_DASHBOARD_FIREBLADE 1 CACHE BOOL "Firmware profile Fireblade dashboard" FORCE)' "${work_dir}/dashboard-fireblade-build/dashboard-fireblade/generated/profile.cmake"

fake_git_bin="${work_dir}/fake-git-bin"
mkdir -p "$fake_git_bin"
cat >"$fake_git_bin/git" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
[[ "${1:-}" == clone ]] || exit 2
printf '%s\n' "$@" >>"${FAKE_GIT_ARGS:?}"
if [[ -n "${FAKE_GIT_FAIL_ONCE_FILE:-}" && ! -e "$FAKE_GIT_FAIL_ONCE_FILE" ]]; then
    : >"$FAKE_GIT_FAIL_ONCE_FILE"
    exit 1
fi
destination="${!#}"
mkdir -p "$destination/bin"
cat >"$destination/export.sh" <<'EXPORT'
export PATH="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/bin:$PATH"
EXPORT
cat >"$destination/install.sh" <<'INSTALL'
#!/usr/bin/env bash
set -euo pipefail
INSTALL
cat >"$destination/bin/idf.py" <<'IDF'
#!/usr/bin/env bash
if [[ "${1:-}" == --version ]]; then
    printf '%s\n' 'ESP-IDF v6.0.2'
    exit 0
fi
exit 2
IDF
chmod +x "$destination/install.sh" "$destination/bin/idf.py"
EOF
chmod +x "$fake_git_bin/git"

install_home="${work_dir}/install-home"
install_config="${work_dir}/install-config"
install_dir="${work_dir}/installed-esp-idf"
HOME="$install_home" \
    XDG_CONFIG_HOME="$install_config" \
    PATH="$fake_git_bin:$PATH" \
    FAKE_GIT_ARGS="${work_dir}/install-git.args" \
    "${repo_root}/start.sh" install-idf --lang en --dir "$install_dir" >"${work_dir}/install-idf.out"
rg -Fq 'ESP-IDF v6.0.2 installed at' "${work_dir}/install-idf.out"
rg -Fx 'clone' "${work_dir}/install-git.args"
rg -Fx -- '--branch' "${work_dir}/install-git.args"
rg -Fx 'v6.0.2' "${work_dir}/install-git.args"
rg -Fx "$install_dir" "${work_dir}/install-config/esp32-bms-gps/idf-path"
HOME="$install_home" XDG_CONFIG_HOME="$install_config" env -u IDF_PATH \
    "${repo_root}/scripts/esp-idf-env.sh" --version >"${work_dir}/configured-idf.out"
rg -Fx 'ESP-IDF v6.0.2' "${work_dir}/configured-idf.out"

retry_install_dir="${work_dir}/installed-esp-idf-retry"
HOME="$install_home" \
    XDG_CONFIG_HOME="$install_config" \
    PATH="$fake_git_bin:$PATH" \
    FAKE_GIT_ARGS="${work_dir}/retry-git.args" \
    FAKE_GIT_FAIL_ONCE_FILE="${work_dir}/retry-git-failed-once" \
    "${repo_root}/start.sh" install-idf --lang en --dir "$retry_install_dir" >"${work_dir}/retry-install-idf.out"
rg -Fq 'clone interrupted; retrying (2/3)' "${work_dir}/retry-install-idf.out"
[[ "$(rg -cx 'clone' "${work_dir}/retry-git.args")" == 2 ]]
test -f "${retry_install_dir}/install.sh"

IDF_PATH="$fake_idf_root" \
    FIRMWARE_BUILD_ROOT="${work_dir}/local-build" \
    ESP_BMS_IDF_BUILD_ROOT="${work_dir}/ascii-idf-build" \
    FIRMWARE_OUTPUT_ROOT="${work_dir}/output" \
    FAKE_IDF_ARGS="${work_dir}/local-idf.args" \
    "${repo_root}/start.sh" compile-local --lang en --config "${work_dir}/golden.env" >"${work_dir}/local-build.out"
rg -Fq 'Build completed' "${work_dir}/local-build.out"
rg -Fx -- '-B' "${work_dir}/local-idf.args"
rg -Fx "${work_dir}/ascii-idf-build/golden/idf-build" "${work_dir}/local-idf.args"
test -f "${work_dir}/output/golden/golden.bin"
test ! -d "${work_dir}/ascii-idf-build/golden/idf-build"

printf '2\nsaved-s3\n' | \
    IDF_PATH="$fake_idf_root" \
    FIRMWARE_BUILD_ROOT="$saved_build_root" \
    ESP_BMS_IDF_BUILD_ROOT="${work_dir}/saved-idf-build" \
    FIRMWARE_OUTPUT_ROOT="${work_dir}/saved-output" \
    FAKE_IDF_ARGS="${work_dir}/saved-idf.args" \
    "${repo_root}/start.sh" >"${work_dir}/saved-profile.out"
rg -Fq '[Saved configuration] saved-s3' "${work_dir}/saved-profile.out"
! rg -Fq 'invalid-saved' "${work_dir}/saved-profile.out"
rg -Fx -- "-DSDKCONFIG_DEFAULTS=$saved_build_root/saved-s3/sdkconfig.defaults" "${work_dir}/saved-idf.args"
rg -Fx "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"$saved_build_root/saved-s3/partitions.csv\"" "$saved_build_root/saved-s3/sdkconfig.defaults"

cat >"${work_dir}/custom.env" <<'EOF'
SCHEMA_VERSION=1
PROFILE=custom-gps
MCU=esp32
BOARD=custom
BOARD_NAME=my-esp32-board
DISPLAY=custom
DISPLAY_NAME=my-spi-panel
INPUT=custom
INPUT_NAME=my-touch
DISPLAY_BUS=SPI
INPUT_BUS=SPI
FLASH_MB=4
PSRAM_MB=0
PARTITIONS=partitions.csv
INPUT_GPIO=TOUCH_IRQ:36,TOUCH_MISO:39,GPS_RX:27,GPS_PPS:35
OUTPUT_GPIO=TFT_MOSI:13,TFT_SCLK:14,TFT_CS:15,TFT_DC:2,TFT_BACKLIGHT:21,TOUCH_MOSI:32,TOUCH_CS:33,TOUCH_SCLK:25,GPS_TX:18
MODULES=gps
CONFIRM_DANGEROUS_GPIO=YES
EOF
FIRMWARE_BUILD_ROOT="${work_dir}/custom-build" "${repo_root}/start.sh" configure --lang en --config "${work_dir}/custom.env" >/dev/null
rg -qx 'BOARD=custom' "${work_dir}/custom-build/custom-gps/firmware.env"
rg -qx 'BOARD_NAME=my-esp32-board' "${work_dir}/custom-build/custom-gps/firmware.env"
rg -qx 'GPIO_GPS_TX=18' "${work_dir}/custom-build/custom-gps/firmware.env"
sed 's/,GPS_TX:[0-9][0-9]*//' "${work_dir}/custom.env" >"${work_dir}/custom-missing-gps.env"
expect_fail 'missing required output GPIO role GPS_TX' "${repo_root}/start.sh" validate --lang en --config "${work_dir}/custom-missing-gps.env"

expect_fail 'missing configuration file' "${repo_root}/scripts/build-profile.sh" --lang en --config "${work_dir}/missing.env"

"${repo_root}/start.sh" validate --modules ota --profile chinese-default >"${work_dir}/chinese.out"
grep -qx '校验通过：配置档=chinese-default 模块=network,ota 仪表=' "${work_dir}/chinese.out"
"${repo_root}/start.sh" --lang zh help >"${work_dir}/chinese-help.out"
rg -q '^用法：' "${work_dir}/chinese-help.out"
expect_fail 'invalid language' "${repo_root}/start.sh" validate --lang en --lang ja

printf '2\n\n\n\n\n\n\n' | FIRMWARE_BUILD_ROOT="${work_dir}/interactive-build" "${repo_root}/start.sh" >"${work_dir}/interactive.out"
rg -q '^=== ESP32 BMS GPS Firmware Configurator / ESP32 BMS GPS 固件定制器 ===$' "${work_dir}/interactive.out"
rg -q '^请选择语言 / Select language$' "${work_dir}/interactive.out"
rg -q '^ ESP32 BMS GPS Firmware Configurator$' "${work_dir}/interactive.out"
rg -Fq '  2) esp32s3-n16r8-st7796u-gt1151 ' "${work_dir}/interactive.out"
rg -q '^Modules$' "${work_dir}/interactive.out"
rg -q '^config: .*/interactive-build/esp32s3-n16r8-st7796u-gt1151/firmware.env$' "${work_dir}/interactive.out"
! rg -q 'Profile \[' "${work_dir}/interactive.out"
rg -qx 'PROFILE=esp32s3-n16r8-st7796u-gt1151' "${work_dir}/interactive-build/esp32s3-n16r8-st7796u-gt1151/firmware.env"
! rg -q '^LANGUAGE=' "${work_dir}/interactive-build/esp32s3-n16r8-st7796u-gt1151/firmware.env"

rg -Fq 'dispatch_cloud_build "$BUILD_ROOT/${CFG[PROFILE]}/firmware.env"' "${repo_root}/start.sh"
rg -Fq 'Invoke-CloudBuild $Config' "${repo_root}/start.ps1"
test -f "${repo_root}/.github/workflows/cloud-build.yml"
rg -Fq 'workflow_dispatch:' "${repo_root}/.github/workflows/cloud-build.yml"

printf 'invalid\n2\n\n\n\n\n\n\n' | FIRMWARE_BUILD_ROOT="${work_dir}/interactive-retry-build" "${repo_root}/start.sh" >"${work_dir}/interactive-retry.out" 2>"${work_dir}/interactive-retry.err"
rg -q '^请输入 1、2、zh 或 en。 / Enter 1, 2, zh, or en\.$' "${work_dir}/interactive-retry.err"
rg -q '^config: .*/interactive-retry-build/esp32s3-n16r8-st7796u-gt1151/firmware.env$' "${work_dir}/interactive-retry.out"

printf '1\n2\n\n\n2,7\nn\n' | FIRMWARE_BUILD_ROOT="${work_dir}/interactive-cancel-build" "${repo_root}/start.sh" >"${work_dir}/interactive-cancel.out"
rg -Fq '  1) ili9488-i80 ' "${work_dir}/interactive-cancel.out"
rg -Fq '  1) ft6336u-i2c ' "${work_dir}/interactive-cancel.out"
rg -q '^已取消生成配置。$' "${work_dir}/interactive-cancel.out"
! test -e "${work_dir}/interactive-cancel-build/esp32s3-n16r8-st7796u-gt1151/firmware.env"

printf '%s\n' \
    2 3 console-custom 1 '' '' '' panel '' '' touch '' gps '1,2' \
    13 14 15 2 21 36 39 32 33 25 27 35 18 y y y |
    FIRMWARE_BUILD_ROOT="${work_dir}/interactive-custom-build" "${repo_root}/start.sh" >"${work_dir}/interactive-custom.out"
rg -Fq '  3) custom ' "${work_dir}/interactive-custom.out"
rg -qx 'PROFILE=console-custom' "${work_dir}/interactive-custom-build/console-custom/firmware.env"
rg -qx 'BOARD=custom' "${work_dir}/interactive-custom-build/console-custom/firmware.env"
rg -qx 'GPIO_GPS_RX=27' "${work_dir}/interactive-custom-build/console-custom/firmware.env"
rg -qx 'DASHBOARDS=fireblade,s1000rr' "${work_dir}/interactive-custom-build/console-custom/firmware.env"

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

env -u ESP_BMS_PROFILE_FILE cmake -DOUTPUT_FILE="${work_dir}/default-requires.out" -P "${work_dir}/early-requires.cmake"
rg -q '(^|;)esp_bms_gps(;|$)' "${work_dir}/default-requires.out"

power_shell=''
if command -v pwsh >/dev/null 2>&1; then
    power_shell='pwsh'
elif command -v powershell >/dev/null 2>&1; then
    power_shell='powershell'
elif command -v powershell.exe >/dev/null 2>&1; then
    power_shell='powershell.exe'
fi

if [[ -n "${power_shell}" ]]; then
    "${power_shell}" -NoProfile -NonInteractive -File "${repo_root}/start.ps1" help >"${work_dir}/powershell-help.out"
    rg -q '^用法：' "${work_dir}/powershell-help.out"
    FIRMWARE_BUILD_ROOT="${work_dir}/powershell-build" "${power_shell}" -NoProfile -NonInteractive -File "${repo_root}/start.ps1" configure --lang en --config "${work_dir}/golden.env" >/dev/null
    cmp "${work_dir}/bash-build/golden/firmware.env" "${work_dir}/powershell-build/golden/normalized.env"
    FIRMWARE_BUILD_ROOT="${work_dir}/powershell-dashboard-build" "${power_shell}" -NoProfile -NonInteractive -File "${repo_root}/start.ps1" configure --lang en --profile dashboard-fireblade --dashboards fireblade >/dev/null
    cmp "${work_dir}/dashboard-fireblade-build/dashboard-fireblade/firmware.env" "${work_dir}/powershell-dashboard-build/dashboard-fireblade/normalized.env"
    rg -Fx "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"${work_dir}/powershell-build/golden/partitions.csv\"" "${work_dir}/powershell-build/golden/sdkconfig.defaults"
else
    echo 'PowerShell comparison skipped: no PowerShell runtime is available'
fi

[[ "$(od -An -tx1 -N3 "${repo_root}/start.ps1" | tr -d '[:space:]')" == 'efbbbf' ]]
rg -Fx 'set "PS_EXE=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"' "${repo_root}/start.cmd"
rg -Fq '$Translations = @(' "${repo_root}/start.ps1"
! rg -Fq "'Profile' =" "${repo_root}/start.ps1"
! rg -Fq "'Board' =" "${repo_root}/start.ps1"
! rg -Fq "'Display' =" "${repo_root}/start.ps1"
! rg -Fq "'Input' =" "${repo_root}/start.ps1"
rg -Fq 'function Select-ModuleOptionsWithKeyboard' "${repo_root}/start.ps1"
rg -Fq 'function Select-CatalogOptionsWithKeyboard' "${repo_root}/start.ps1"
rg -Fq 'choose_dashboard_options_with_keyboard' "${repo_root}/start.sh"
rg -Fq 'choose_catalog_option_with_keyboard' "${repo_root}/start.sh"
rg -Fq 'Left to return to the previous feature list' "${repo_root}/start.ps1"
rg -Fq 'Left to return, Enter to continue.' "${repo_root}/start.ps1"
rg -Fq '← 返回上一个功能清单' "${repo_root}/start.sh"
rg -Fq 'MENU_RETURN_TO_PREVIOUS_FUNCTION_LIST=YES' "${repo_root}/start.sh"
rg -Fq '[ConsoleKey]::LeftArrow' "${repo_root}/start.ps1"
rg -Fq "'DISPLAY_DATA_WIDTH'" "${repo_root}/start.ps1"
rg -Fq "'DATA_WIDTH'" "${repo_root}/start.ps1"
! rg -Fq 'scripts/esp-idf-env.sh' "${repo_root}/start.ps1"
rg -Fq '. ([string]$IdfExport)' "${repo_root}/start.ps1"
! rg -Fq '. $IdfExport' "${repo_root}/start.ps1"
rg -Fq '& idf.py @IdfArgs' "${repo_root}/start.ps1"
rg -Fq 'Test-Path -LiteralPath Variable:global:LASTEXITCODE' "${repo_root}/start.ps1"
rg -Fq 'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME' "${repo_root}/start.ps1"
rg -Fq 'function Test-PythonExecutable' "${repo_root}/start.ps1"
rg -Fq 'function Get-PythonExecutable' "${repo_root}/start.ps1"
rg -Fq '& $PythonLauncher.Source -3 -c' "${repo_root}/start.ps1"
rg -Fq "[Environment]::GetEnvironmentVariable('IDF_PATH', \$Scope)" "${repo_root}/start.ps1"
rg -Fq "Join-Path \$Base 'esp-idf-v6.0.2'" "${repo_root}/start.ps1"
rg -Fq "Join-Path \$env:USERPROFILE \$RelativePath" "${repo_root}/start.ps1"
rg -Fq "Get-ChildItem -LiteralPath \$SearchRoot -Directory -Filter 'esp-idf*'" "${repo_root}/start.ps1"
rg -Fq 'function Ensure-IdfExportScript' "${repo_root}/start.ps1"
rg -Fq 'function Test-IdfExportScript' "${repo_root}/start.ps1"
rg -Fq "\$IdfExport = Ensure-IdfExportScript" "${repo_root}/start.ps1"
rg -Fq 'Install-EspIdf @() | Out-Host' "${repo_root}/start.ps1"
rg -Fq "\$IdfExport = [string](Get-IdfExportScript | Select-Object -Last 1)" "${repo_root}/start.ps1"
rg -Fq '\$CloneAttempts = 3' "${repo_root}/start.ps1"
rg -Fq 'Move-Item -LiteralPath \$CloneDirectory -Destination \$Directory' "${repo_root}/start.ps1"
! rg -Fq '& python3 ' "${repo_root}/start.ps1"
rg -Fq 'scripts/esp-idf-env.sh' "${repo_root}/start.sh"
rg -Fq 'IDF_BUILD_ROOT="${ESP_BMS_IDF_BUILD_ROOT:-/tmp/esp32-bms-gps-idf-builds/$UID}"' "${repo_root}/start.sh"
rg -Fq 'FIRMWARE_OUTPUT_ROOT="${FIRMWARE_OUTPUT_ROOT:-$ROOT/output}"' "${repo_root}/start.sh"
rg -Fq 'install-idf  Install ESP-IDF v6.0.2' "${repo_root}/start.sh"
rg -Fq "'install-idf' { Install-EspIdf \$Arguments; exit 0 }" "${repo_root}/start.ps1"
rg -Fq 'FIRMWARE_OUTPUT_ROOT' "${repo_root}/start.sh"
rg -Fq '$FirmwareOutputRoot' "${repo_root}/start.ps1"
rg -Fq 'idf-path' "${repo_root}/scripts/esp-idf-env.sh"
! rg -Fq 'rfc2217://192.168.2.10:4000?ign_set_control' "${repo_root}/start.sh"
rg -Fq 'Flash target: 1) Local serial (default) 2) Remote RFC2217 [1]' "${repo_root}/start.sh"
rg -Fq 'Flash target: 1) Local serial (default) 2) Remote RFC2217 [1]' "${repo_root}/start.ps1"
rg -Fq '实验性手机投屏（当前使用 legacy runtime）' "${repo_root}/start.sh"
rg -Fq '实验性手机投屏（当前使用 legacy runtime）' "${repo_root}/start.ps1"
rg -Fq '编译完成后保存此配置吗？[y/N]：' "${repo_root}/start.sh"
rg -Fq '现在烧录这个固件吗？[y/N]：' "${repo_root}/start.sh"
rg -Fq '编译完成后保存此配置吗？[y/N]' "${repo_root}/start.ps1"
rg -Fq '现在烧录这个固件吗？[y/N]' "${repo_root}/start.ps1"
rg -Fq 'ESP-IDF v6.0.2' "${repo_root}/start.ps1"
rg -Fq 'esp-idf-v6.0.2' "${repo_root}/scripts/esp-idf-env.sh"
rg -Fq -- '-DIDF_TARGET="${CFG[MCU]}"' "${repo_root}/start.sh"
rg -Fq -- '"-DIDF_TARGET=$($Config.MCU)"' "${repo_root}/start.ps1"
! rg -Fq 'esp-idf-v5.5.4' "${repo_root}/start.sh"
! rg -Fq 'esp-idf-v5.5.4' "${repo_root}/start.ps1"
rg -Fq 'DISPLAY_DATA_WIDTH' "${repo_root}/start.sh"
rg -Fq 'prompt_firmware_version' "${repo_root}/start.sh"
rg -Fq 'function Read-FirmwareVersion' "${repo_root}/start.ps1"
rg -Fq 'DATA_WIDTH' "${repo_root}/start.sh"

echo 'firmware configurator self-test passed'
