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

expect_fail 'dangerous' "${repo_root}/start.sh" validate --lang en --gpio TFT_DC=0
"${repo_root}/start.sh" validate --lang en --gpio TFT_DC=0 --confirm-dangerous-gpio >/dev/null
expect_fail 'assigned to both' "${repo_root}/start.sh" validate --lang en --gpio TFT_DC=4

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

FIRMWARE_BUILD_ROOT="${work_dir}/audio-legacy-build" "${repo_root}/start.sh" configure --lang en --profile audio-legacy --mcu esp32 --board esp32-wroom-32e-legacy --display st7789-spi --input xpt2046-spi --modules audio >/dev/null
rg -qx 'GPIO_AUDIO_DAC=26' "${work_dir}/audio-legacy-build/audio-legacy/firmware.env"
rg -qx 'GPIO_AUDIO_ENABLE=4' "${work_dir}/audio-legacy-build/audio-legacy/firmware.env"
rg -qx 'GPIO_BATTERY_ADC=34' "${work_dir}/audio-legacy-build/audio-legacy/firmware.env"
python3 "${repo_root}/scripts/generate-hardware-config.py" --catalog "${repo_root}/firmware/catalog" --firmware-env "${work_dir}/audio-legacy-build/audio-legacy/firmware.env" --output "${work_dir}/audio-legacy.h"
rg -Fx '#define ESP_BMS_PROFILE_BATTERY_ADC (gpio_num_t)34' "${work_dir}/audio-legacy.h"
rg -Fx '#define ESP_BMS_PROFILE_AUDIO_BACKEND ESP_BMS_PROFILE_AUDIO_BACKEND_DAC' "${work_dir}/audio-legacy.h"

FIRMWARE_BUILD_ROOT="${work_dir}/audio-s3-build" "${repo_root}/start.sh" configure --lang en --profile audio-s3 --mcu esp32s3 --board esp32s3-wroom-1-n16r8-i80 --display ili9488-i80 --input ft6336u-i2c --modules audio >/dev/null
rg -qx 'GPIO_AMP_SHDN=2' "${work_dir}/audio-s3-build/audio-s3/firmware.env"
rg -qx 'GPIO_I2S_BCLK=42' "${work_dir}/audio-s3-build/audio-s3/firmware.env"
rg -qx 'GPIO_I2S_LRCK=47' "${work_dir}/audio-s3-build/audio-s3/firmware.env"
rg -qx 'GPIO_I2S_DATA=48' "${work_dir}/audio-s3-build/audio-s3/firmware.env"
python3 "${repo_root}/scripts/generate-hardware-config.py" --catalog "${repo_root}/firmware/catalog" --firmware-env "${work_dir}/audio-s3-build/audio-s3/firmware.env" --output "${work_dir}/audio-s3.h"
rg -Fx '#define ESP_BMS_PROFILE_BATTERY_ADC GPIO_NUM_NC' "${work_dir}/audio-s3.h"
rg -Fx '#define ESP_BMS_PROFILE_AUDIO_BACKEND ESP_BMS_PROFILE_AUDIO_BACKEND_I2S' "${work_dir}/audio-s3.h"
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
    FIRMWARE_BUILD_ROOT="${work_dir}/local-build" \
    ESP_BMS_IDF_BUILD_ROOT="${work_dir}/output" \
    FAKE_IDF_ARGS="${work_dir}/local-idf.args" \
    "${repo_root}/start.sh" compile-local --lang en --config "${work_dir}/golden.env" >"${work_dir}/local-build.out"
rg -Fq 'Build completed' "${work_dir}/local-build.out"
rg -Fx -- '-B' "${work_dir}/local-idf.args"
rg -Fx "${work_dir}/output/golden/idf-build" "${work_dir}/local-idf.args"
test -f "${work_dir}/output/golden/idf-build/esp32_bms_gps_idf.bin"

printf '2\nsaved-s3\n' | \
    IDF_PATH="$fake_idf_root" \
    FIRMWARE_BUILD_ROOT="$saved_build_root" \
    ESP_BMS_IDF_BUILD_ROOT="${work_dir}/saved-idf-build" \
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
grep -qx '校验通过：配置档=chinese-default 模块=network,ota' "${work_dir}/chinese.out"
"${repo_root}/start.sh" --lang zh help >"${work_dir}/chinese-help.out"
rg -q '^用法：' "${work_dir}/chinese-help.out"
expect_fail 'invalid language' "${repo_root}/start.sh" validate --lang en --lang ja

printf '2\n\n\n\n\n\n' | FIRMWARE_BUILD_ROOT="${work_dir}/interactive-build" "${repo_root}/start.sh" >"${work_dir}/interactive.out"
rg -q '^=== ESP32 BMS GPS Firmware Configurator / ESP32 BMS GPS 固件定制器 ===$' "${work_dir}/interactive.out"
rg -q '^请选择语言 / Select language$' "${work_dir}/interactive.out"
rg -q '^ ESP32 BMS GPS Firmware Configurator$' "${work_dir}/interactive.out"
rg -Fq '  2) esp32s3-n16r8-st7796u-gt1151 ' "${work_dir}/interactive.out"
rg -q '^Modules$' "${work_dir}/interactive.out"
rg -q '^config: .*/interactive-build/esp32s3-n16r8-st7796u-gt1151/firmware.env$' "${work_dir}/interactive.out"
! rg -q 'Profile \[' "${work_dir}/interactive.out"
rg -qx 'PROFILE=esp32s3-n16r8-st7796u-gt1151' "${work_dir}/interactive-build/esp32s3-n16r8-st7796u-gt1151/firmware.env"
! rg -q '^LANGUAGE=' "${work_dir}/interactive-build/esp32s3-n16r8-st7796u-gt1151/firmware.env"

if printf '2\n\n\n\n\n\n2\n' | FIRMWARE_BUILD_ROOT="${work_dir}/interactive-cloud-build" "${repo_root}/start.sh" >"${work_dir}/interactive-cloud.out" 2>"${work_dir}/interactive-cloud.err"; then
    echo 'expected interactive cloud choice to return exit code 3' >&2
    exit 1
fi
rg -qx 'Next step' "${work_dir}/interactive-cloud.out"
rg -Fq 'cloud build request prepared; workflow dispatch belongs to 07-21-build-cloud-verification' "${work_dir}/interactive-cloud.err"

printf 'invalid\n2\n\n\n\n\n\n' | FIRMWARE_BUILD_ROOT="${work_dir}/interactive-retry-build" "${repo_root}/start.sh" >"${work_dir}/interactive-retry.out" 2>"${work_dir}/interactive-retry.err"
rg -q '^请输入 1、2、zh 或 en。 / Enter 1, 2, zh, or en\.$' "${work_dir}/interactive-retry.err"
rg -q '^config: .*/interactive-retry-build/esp32s3-n16r8-st7796u-gt1151/firmware.env$' "${work_dir}/interactive-retry.out"

printf '1\n2\n\n\n2,7\nn\n' | FIRMWARE_BUILD_ROOT="${work_dir}/interactive-cancel-build" "${repo_root}/start.sh" >"${work_dir}/interactive-cancel.out"
rg -Fq '  1) ili9488-i80 ' "${work_dir}/interactive-cancel.out"
rg -Fq '  1) ft6336u-i2c ' "${work_dir}/interactive-cancel.out"
rg -q '^已取消生成配置。$' "${work_dir}/interactive-cancel.out"
! test -e "${work_dir}/interactive-cancel-build/esp32s3-n16r8-st7796u-gt1151/firmware.env"

if script --version >/dev/null 2>&1; then
    (
        cd "${repo_root}"
        printf '2\nesp32s3-wroom-1-n16r8-i80\nili9488-i80\nft6336u-i2c\n \033[B \n\n' |
            FIRMWARE_BUILD_ROOT="${work_dir}/keyboard-build" script -qefc './start.sh' /dev/null >"${work_dir}/keyboard.out"
    )
    rg -Fq 'Use Up/Down to move, Space to toggle, Enter to continue.' "${work_dir}/keyboard.out"
    rg -qx 'MODULES=audio,cast,controller,network,ota' "${work_dir}/keyboard-build/esp32s3-wroom-1-n16r8-i80/firmware.env"
fi

printf '%s\n' \
    2 4 console-custom 1 '' '' '' panel '' '' touch '' gps \
    13 14 15 2 21 36 39 32 33 25 27 35 18 y y y |
    FIRMWARE_BUILD_ROOT="${work_dir}/interactive-custom-build" "${repo_root}/start.sh" >"${work_dir}/interactive-custom.out"
rg -Fq '  4) custom ' "${work_dir}/interactive-custom.out"
rg -qx 'PROFILE=console-custom' "${work_dir}/interactive-custom-build/console-custom/firmware.env"
rg -qx 'BOARD=custom' "${work_dir}/interactive-custom-build/console-custom/firmware.env"
rg -qx 'GPIO_GPS_RX=27' "${work_dir}/interactive-custom-build/console-custom/firmware.env"

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
rg -Fq 'Space to toggle, Enter to continue.' "${repo_root}/start.ps1"
rg -Fq "'DISPLAY_DATA_WIDTH'" "${repo_root}/start.ps1"
rg -Fq "'DATA_WIDTH'" "${repo_root}/start.ps1"
! rg -Fq 'scripts/esp-idf-env.sh' "${repo_root}/start.ps1"
rg -Fq '. $IdfExport' "${repo_root}/start.ps1"
rg -Fq '& idf.py @IdfArgs' "${repo_root}/start.ps1"
rg -Fq 'Test-Path -LiteralPath Variable:global:LASTEXITCODE' "${repo_root}/start.ps1"
rg -Fq 'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME' "${repo_root}/start.ps1"
rg -Fq 'scripts/esp-idf-env.sh' "${repo_root}/start.sh"
rg -Fq 'IDF_BUILD_ROOT="${ESP_BMS_IDF_BUILD_ROOT:-$ROOT/output}"' "${repo_root}/start.sh"
rg -Fq "Join-Path \$Root 'output'" "${repo_root}/start.ps1"
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
rg -Fq 'DATA_WIDTH' "${repo_root}/start.sh"

echo 'firmware configurator self-test passed'
