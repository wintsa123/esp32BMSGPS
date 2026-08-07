#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/esp32bmsgps-configurator.XXXXXX")"
mkdir -p "${repo_root}/firmware-builds"
profile_dir="$(mktemp -d "${repo_root}/firmware-builds/.configurator-requires.XXXXXX")"
trap 'rm -rf "${work_dir}" "${profile_dir}"' EXIT

rg -Fq 'default y if SPIRAM' "${repo_root}/components/esp_bms_lvgl_bridge/Kconfig"
rg -qx 'CONFIG_ESP_BMS_LVGL_BRIDGE_DOUBLE_BUFFER=y' "${repo_root}/config/sdkconfig/sdkconfig.defaults.esp32s3"
rg -qx 'CONFIG_ESP_BMS_LVGL_BRIDGE_SPI_DRAW_BUFFER_HEIGHT=120' "${repo_root}/config/sdkconfig/sdkconfig.defaults.esp32s3"
rg -qx 'CONFIG_LV_USE_SNAPSHOT=y' "${repo_root}/config/sdkconfig/sdkconfig.defaults.esp32s3"
rg -Fq '#define LV_USE_SNAPSHOT 1' "${repo_root}/simulator/lv_conf.h"
rg -qx '# CONFIG_ESP_BMS_LVGL_BRIDGE_DOUBLE_BUFFER is not set' "${repo_root}/config/sdkconfig/sdkconfig.defaults"
rg -Fq 'default n' "${repo_root}/components/esp_bms_lvgl_ui/Kconfig"
rg -qx '# CONFIG_ESP_BMS_LVGL_UI_DRAG_FULL_INVALIDATE is not set' "${repo_root}/config/sdkconfig/sdkconfig.defaults"
rg -qx '# CONFIG_ESP_BMS_LVGL_UI_DRAG_FULL_INVALIDATE is not set' "${repo_root}/config/sdkconfig/sdkconfig.defaults.esp32s3"
rg -qx 'CONFIG_ESP_BMS_LVGL_UI_DRAG_FULL_INVALIDATE=y' "${repo_root}/config/sdkconfig/sdkconfig.defaults.dragdiag-full-invalidate"
! rg -q 'transform_scale' "${repo_root}/components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c"
rg -Fq 'DASHBOARD_STATIC_CACHE_BYTES == 307200U' "${repo_root}/components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c"
rg -Fq 'dashboard_static_cache_finalize(&s_ui.battery_static_cache' "${repo_root}/components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c"
rg -Fq 'dashboard_static_cache_finalize(&s_ui.fireblade_static_cache' "${repo_root}/components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c"
rg -Fq '#define DISPLAY_SERVICE_COMMAND_QUEUE_LENGTH 12U' "${repo_root}/components/esp_bms_display_service/esp_bms_display_service.c"
rg -Fq '#define DISPLAY_SERVICE_ACTION_QUEUE_LENGTH 16U' "${repo_root}/components/esp_bms_display_service/esp_bms_display_service.c"
rg -Fq 'xQueueCreateStatic(1,' "${repo_root}/components/esp_bms_display_service/esp_bms_display_service.c"
! rg -Fq 'xQueueCreate(' "${repo_root}/components/esp_bms_display_service/esp_bms_display_service.c"
! rg -q 'esp_bms_lvgl_bridge_(init|start|lock|unlock|set_brightness|set_rotation|write_rgb565)' "${repo_root}/main/idf_main.c"
! rg -q 'esp_bms_lvgl_ui_(init|update|boot_|show_dashboard|set_page)' "${repo_root}/main/idf_main.c"
! rg -q 'esp_bms_lvgl_(bridge|ui)_' "${repo_root}/components/esp_bms_idf_runtime/esp_bms_idf_runtime.c"
rg -qx 'PIXEL_CLOCK_HZ=25000000' "${repo_root}/firmware/catalog/display/st7796u-i80.env"
rg -qx 'PIXEL_CLOCK_HZ=10000000' "${repo_root}/firmware/catalog/display/st7796-4p0-i80.env"
rg -qx 'PIXEL_CLOCK_HZ=10000000' "${repo_root}/firmware/catalog/display/st7796-6p0-i80.env"
rg -qx 'PIXEL_CLOCK_HZ=10000000' "${repo_root}/firmware/catalog/display/st7796-7p0-i80.env"

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

cat >"${work_dir}/c3-spi.env" <<'EOF'
SCHEMA_VERSION=1
PROFILE=c3-spi
MCU=esp32c3
BOARD=custom
BOARD_NAME=c3-spi-board
DISPLAY=st7789-1p8-spi
INPUT=none
MODULES=
FLASH_MB=4
PSRAM_MB=0
PARTITIONS=firmware/partitions/esp32-wroom-32e-legacy.csv
OUTPUT_GPIO=TFT_MOSI:3,TFT_SCLK:4,TFT_CS:5,TFT_DC:6
EOF
FIRMWARE_BUILD_ROOT="${work_dir}/c3-build" "${repo_root}/start.sh" configure --lang en --config "${work_dir}/c3-spi.env" >/dev/null
rg -qx 'MCU=esp32c3' "${work_dir}/c3-build/c3-spi/firmware.env"
rg -Fx '#define ESP_BMS_PROFILE_DISPLAY_SIZE_INCH "1.8"' "${work_dir}/c3-build/c3-spi/generated/esp_bms_profile_hardware.h"
expect_fail 'display ili9488-8p0-i80 is unavailable on esp32c3' "${repo_root}/start.sh" validate --lang en --config "${work_dir}/c3-spi.env" --display ili9488-8p0-i80

cat >"${work_dir}/p4-i80.env" <<'EOF'
SCHEMA_VERSION=1
PROFILE=p4-i80
MCU=esp32p4
BOARD=custom
BOARD_NAME=p4-i80-board
DISPLAY=ili9488-8p0-i80
INPUT=none
MODULES=
FLASH_MB=16
PSRAM_MB=0
PARTITIONS=firmware/partitions/esp32-wroom-32e-legacy.csv
OUTPUT_GPIO=TFT_D0:4,TFT_D1:5,TFT_D2:6,TFT_D3:7,TFT_D4:8,TFT_D5:9,TFT_D6:10,TFT_D7:11,TFT_WR:12,TFT_CS:13,TFT_DC:14
EOF
FIRMWARE_BUILD_ROOT="${work_dir}/p4-build" "${repo_root}/start.sh" configure --lang en --config "${work_dir}/p4-i80.env" >/dev/null
rg -qx 'MCU=esp32p4' "${work_dir}/p4-build/p4-i80/firmware.env"
rg -Fx '#define ESP_BMS_PROFILE_DISPLAY_SIZE_INCH "8.0"' "${work_dir}/p4-build/p4-i80/generated/esp_bms_profile_hardware.h"
rg -Fx '#define ESP_BMS_PROFILE_COMMUNICATION_COPROCESSOR "ESP32C6"' "${work_dir}/p4-build/p4-i80/generated/esp_bms_profile_hardware.h"
rg -Fx 'set(ESP_BMS_PROFILE_COMMUNICATION_COPROCESSOR "ESP32C6" CACHE STRING "Firmware profile radio coprocessor" FORCE)' "${work_dir}/p4-build/p4-i80/generated/profile.cmake"
rg -Fx 'COMMUNICATION_COPROCESSOR=ESP32C6' "${work_dir}/p4-build/p4-i80/report.txt"
rg -Fq 'atanisoft/esp_lcd_ili9488:' "${work_dir}/p4-build/p4-i80/generated/idf_component.yml"
expect_fail 'bms requires capability BLE' "${repo_root}/start.sh" validate --lang en --config "${work_dir}/p4-i80.env" --modules bms
expect_fail 'ble-media-hid requires capability BLE' "${repo_root}/start.sh" validate --lang en --config "${work_dir}/p4-i80.env" --modules ble-media-hid
expect_fail 'network requires capability WIFI' "${repo_root}/start.sh" validate --lang en --config "${work_dir}/p4-i80.env" --modules network

FIRMWARE_BUILD_ROOT="${work_dir}/s3-default-build" "${repo_root}/start.sh" configure --profile s3-default >/dev/null
rg -qx 'MCU=esp32s3' "${work_dir}/s3-default-build/s3-default/firmware.env"
rg -qx 'BOARD=esp32s3-n16r8-st7796u-gt1151' "${work_dir}/s3-default-build/s3-default/firmware.env"
rg -Fq '#define ESP_BMS_PROFILE_DISPLAY_ROTATION_DEFAULT_VERSION 4' "${work_dir}/s3-default-build/s3-default/generated/esp_bms_profile_hardware.h"
rg -Fq '.physical_width = 320' "${work_dir}/s3-default-build/s3-default/generated/esp_bms_profile_hardware.h"
rg -Fq '.physical_height = 480' "${work_dir}/s3-default-build/s3-default/generated/esp_bms_profile_hardware.h"
rg -Fq '.rotation = ESP_BMS_DISPLAY_ROTATION_LANDSCAPE' "${work_dir}/s3-default-build/s3-default/generated/esp_bms_profile_hardware.h"
rg -Fq '.panel_mirror_x = true' "${work_dir}/s3-default-build/s3-default/generated/esp_bms_profile_hardware.h"
rg -Fq '.rgb_element_order = LCD_RGB_ELEMENT_ORDER_BGR' "${work_dir}/s3-default-build/s3-default/generated/esp_bms_profile_hardware.h"
rg -Fq '.invert_color = true' "${work_dir}/s3-default-build/s3-default/generated/esp_bms_profile_hardware.h"
rg -Fq '.i80_swap_color_bytes = false' "${work_dir}/s3-default-build/s3-default/generated/esp_bms_profile_hardware.h"
rg -Fq '# CONFIG_ESP_BMS_LVGL_BRIDGE_FULL_REFRESH_DOUBLE_BUFFER is not set' "${work_dir}/s3-default-build/s3-default/sdkconfig.defaults"
rg -Fq '.touch_mirror_x = true' "${work_dir}/s3-default-build/s3-default/generated/esp_bms_profile_hardware.h"
rg -Fq '.pin_expander_sda = (gpio_num_t)2' "${work_dir}/s3-default-build/s3-default/generated/esp_bms_profile_hardware.h"
rg -Fq '.pin_expander_scl = (gpio_num_t)1' "${work_dir}/s3-default-build/s3-default/generated/esp_bms_profile_hardware.h"
rg -Fq '.pin_rd = (gpio_num_t)14' "${work_dir}/s3-default-build/s3-default/generated/esp_bms_profile_hardware.h"
rg -Fq '.use_xl9555_expander = true' "${work_dir}/s3-default-build/s3-default/generated/esp_bms_profile_hardware.h"
rg -qx 'DISPLAY=st7796u-i80' "${work_dir}/s3-default-build/s3-default/firmware.env"
rg -qx 'INPUT=gt1151-i2c' "${work_dir}/s3-default-build/s3-default/firmware.env"
rg -qx 'GPIO_TFT_D15=4' "${work_dir}/s3-default-build/s3-default/firmware.env"
rg -qx 'GPIO_TFT_RD=14' "${work_dir}/s3-default-build/s3-default/firmware.env"
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
"${repo_root}/start.sh" validate --lang en --mcu esp32 --board esp32-wroom-32e-legacy --display st7789-spi --input xpt2046-spi --modules ble-media-hid >/dev/null
FIRMWARE_BUILD_ROOT="${work_dir}/classic-media-hid-build" "${repo_root}/start.sh" configure --lang en --profile classic-media-hid-check --mcu esp32 --board esp32-wroom-32e-legacy --display st7789-spi --input xpt2046-spi --modules classic-media-hid >/dev/null
rg -qx 'MODULES=classic-media-hid' "${work_dir}/classic-media-hid-build/classic-media-hid-check/firmware.env"
rg -Fx 'set(ESP_BMS_FEATURE_BLE 0 CACHE BOOL "Firmware profile BLE capability" FORCE)' "${work_dir}/classic-media-hid-build/classic-media-hid-check/generated/profile.cmake"
rg -Fx 'set(ESP_BMS_FEATURE_CLASSIC_MEDIA_HID 1 CACHE BOOL "Firmware profile Classic HID media feature" FORCE)' "${work_dir}/classic-media-hid-build/classic-media-hid-check/generated/profile.cmake"
rg -Fq 'esp_bms_classic_media_hid;esp_bms_idf_runtime' "${work_dir}/classic-media-hid-build/classic-media-hid-check/generated/profile.cmake"
rg -qx '# CONFIG_BT_NIMBLE_ENABLED is not set' "${work_dir}/classic-media-hid-build/classic-media-hid-check/sdkconfig.defaults"
rg -qx 'CONFIG_BT_BLUEDROID_ENABLED=y' "${work_dir}/classic-media-hid-build/classic-media-hid-check/sdkconfig.defaults"
rg -qx 'CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY=y' "${work_dir}/classic-media-hid-build/classic-media-hid-check/sdkconfig.defaults"
! rg -q '^CONFIG_BT_NIMBLE_ENABLED=y' "${work_dir}/classic-media-hid-build/classic-media-hid-check/sdkconfig.defaults"
expect_fail 'classic-media-hid requires capability BT_CLASSIC' "${repo_root}/start.sh" validate --lang en --mcu esp32s3 --modules classic-media-hid
expect_fail 'classic-media-hid requires capability BT_CLASSIC' "${repo_root}/start.sh" validate --lang en --config "${work_dir}/c3-spi.env" --modules classic-media-hid
expect_fail 'classic-media-hid conflicts with ble-media-hid' "${repo_root}/start.sh" validate --lang en --mcu esp32 --board esp32-wroom-32e-legacy --display st7789-spi --input xpt2046-spi --modules ble-media-hid,classic-media-hid
expect_fail 'bms conflicts with classic-media-hid' "${repo_root}/start.sh" validate --lang en --mcu esp32 --board esp32-wroom-32e-legacy --display st7789-spi --input xpt2046-spi --modules classic-media-hid,bms

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

expect_fail 'requires display st7789-spi' "${repo_root}/start.sh" validate --lang en --mcu esp32 --board esp32-wroom-32e-legacy --display ili9341-2p8-spi --input xpt2046-spi
cat >"${work_dir}/legacy-ili9341-2p8.env" <<'EOF'
SCHEMA_VERSION=1
PROFILE=legacy-ili9341-2p8
MCU=esp32
BOARD=custom
BOARD_NAME=legacy-ili9341-2p8-board
DISPLAY=ili9341-2p8-spi
INPUT=xpt2046-spi
MODULES=
FLASH_MB=4
PSRAM_MB=0
PARTITIONS=firmware/partitions/esp32-wroom-32e-legacy.csv
INPUT_GPIO=TOUCH_MISO:39
OUTPUT_GPIO=TFT_MOSI:13,TFT_SCLK:14,TFT_CS:15,TFT_DC:2,TOUCH_MOSI:32,TOUCH_CS:33,TOUCH_SCLK:25
EOF
FIRMWARE_BUILD_ROOT="${work_dir}/legacy-ili9341-2p8-build" "${repo_root}/start.sh" configure --lang en --config "${work_dir}/legacy-ili9341-2p8.env" >/dev/null
rg -qx 'DISPLAY=ili9341-2p8-spi' "${work_dir}/legacy-ili9341-2p8-build/legacy-ili9341-2p8/firmware.env"
rg -Fx '#define ESP_BMS_PROFILE_DISPLAY_SIZE_INCH "2.8"' "${work_dir}/legacy-ili9341-2p8-build/legacy-ili9341-2p8/generated/esp_bms_profile_hardware.h"
rg -Fq '.physical_width = 240' "${work_dir}/legacy-ili9341-2p8-build/legacy-ili9341-2p8/generated/esp_bms_profile_hardware.h"

expect_fail 'does not provide an audio hardware profile' "${repo_root}/start.sh" validate --lang en --profile audio-st7796 --mcu esp32s3 --board esp32s3-n16r8-st7796u-gt1151 --display st7796u-i80 --input gt1151-i2c --modules audio

cat >"${work_dir}/s3-i2s-audio.env" <<'EOF'
SCHEMA_VERSION=1
PROFILE=s3-i2s-audio
MCU=esp32s3
BOARD=esp32s3-n16r8-st7796u-gt1151
DISPLAY=st7796u-i80
INPUT=gt1151-i2c
MODULES=audio
AUDIO_BACKEND=I2S
AUDIO_DAC_CHANNEL=0
AUDIO_ENABLE_ACTIVE_LEVEL=0
GPIO_I2S_BCLK=20
GPIO_I2S_LRCK=21
GPIO_I2S_DATA=47
GPIO_AMP_SHDN=48
EOF
FIRMWARE_BUILD_ROOT="${work_dir}/s3-i2s-audio-build" "${repo_root}/start.sh" configure --lang en --config "${work_dir}/s3-i2s-audio.env" >/dev/null
rg -qx 'GPIO_I2S_DATA=47' "${work_dir}/s3-i2s-audio-build/s3-i2s-audio/firmware.env"
rg -Fx '#define ESP_BMS_PROFILE_AUDIO_BACKEND ESP_BMS_PROFILE_AUDIO_BACKEND_I2S' "${work_dir}/s3-i2s-audio-build/s3-i2s-audio/generated/esp_bms_profile_hardware.h"

cat >"${work_dir}/custom-i2s-audio.env" <<'EOF'
SCHEMA_VERSION=1
PROFILE=custom-i2s-audio
MCU=esp32s3
BOARD=custom
BOARD_NAME=custom-i2s-audio-board
DISPLAY=st7789-1p8-spi
INPUT=none
MODULES=audio
FLASH_MB=16
PSRAM_MB=8
PARTITIONS=firmware/partitions/esp32-wroom-32e-legacy.csv
AUDIO_BACKEND=I2S
AUDIO_DAC_CHANNEL=0
AUDIO_ENABLE_ACTIVE_LEVEL=0
OUTPUT_GPIO=TFT_MOSI:13,TFT_SCLK:14,TFT_CS:15,TFT_DC:16,I2S_BCLK:17,I2S_LRCK:18,I2S_DATA:19,AMP_SHDN:20
EOF
FIRMWARE_BUILD_ROOT="${work_dir}/custom-i2s-audio-build" "${repo_root}/start.sh" configure --lang en --config "${work_dir}/custom-i2s-audio.env" >/dev/null
rg -qx 'AUDIO_BACKEND=I2S' "${work_dir}/custom-i2s-audio-build/custom-i2s-audio/firmware.env"
rg -Fx '#define ESP_BMS_PROFILE_AUDIO_BACKEND ESP_BMS_PROFILE_AUDIO_BACKEND_I2S' "${work_dir}/custom-i2s-audio-build/custom-i2s-audio/generated/esp_bms_profile_hardware.h"
rg -Fx '#define ESP_BMS_PROFILE_AUDIO_I2S_DATA (gpio_num_t)19' "${work_dir}/custom-i2s-audio-build/custom-i2s-audio/generated/esp_bms_profile_hardware.h"

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
test -f "${work_dir}/bash-build/golden/generated/idf_component.yml"
test -f "${work_dir}/bash-build/golden/generated/profile.cmake"

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
        build_dir="${args[$((index + 1))]}"
        if [[ -e "$build_dir/CMakeCache.txt" ]]; then
            printf '%s\n' 'stale CMake cache was not removed before build' >&2
            exit 1
        fi
        mkdir -p "$build_dir/bootloader" "$build_dir/partition_table"
        printf 'bootloader' >"$build_dir/bootloader/bootloader.bin"
        printf 'partition-table' >"$build_dir/partition_table/partition-table.bin"
        printf 'ota-data' >"$build_dir/ota_data_initial.bin"
        printf 'application' >"$build_dir/esp32_bms_gps_idf.bin"
        cat >"$build_dir/flasher_args.json" <<'JSON'
{
  "flash_settings": {"flash_mode": "dio", "flash_freq": "40m", "flash_size": "4MB"},
  "flash_files": {
    "0x1000": "bootloader/bootloader.bin",
    "0x8000": "partition_table/partition-table.bin",
    "0xd000": "ota_data_initial.bin",
    "0x10000": "esp32_bms_gps_idf.bin"
  },
  "app": {"offset": "0x10000", "file": "esp32_bms_gps_idf.bin"},
  "extra_esptool_args": {"chip": "esp32"}
}
JSON
        break
    fi
done
printf '%s\n' "$@" >"${FAKE_IDF_ARGS:?}"
EOF
chmod +x "$fake_idf_root/bin/idf.py"

IDF_PATH="$fake_idf_root" \
    FAKE_IDF_ARGS="${work_dir}/dragdiag-default.args" \
    "${repo_root}/scripts/esp-idf-drag-diag.sh" \
    --build-dir "${work_dir}/dragdiag-default" \
    --sdkconfig "${work_dir}/dragdiag-default.sdkconfig" build >/dev/null
rg -Fx -- '-DSDKCONFIG_DEFAULTS=config/sdkconfig/sdkconfig.defaults;config/sdkconfig/sdkconfig.defaults.dragdiag' "${work_dir}/dragdiag-default.args"

IDF_PATH="$fake_idf_root" \
    FAKE_IDF_ARGS="${work_dir}/dragdiag-full.args" \
    "${repo_root}/scripts/esp-idf-drag-diag.sh" \
    --full-invalidate \
    --build-dir "${work_dir}/dragdiag-full" \
    --sdkconfig "${work_dir}/dragdiag-full.sdkconfig" build >/dev/null
rg -Fx -- '-DSDKCONFIG_DEFAULTS=config/sdkconfig/sdkconfig.defaults;config/sdkconfig/sdkconfig.defaults.dragdiag;config/sdkconfig/sdkconfig.defaults.dragdiag-full-invalidate' "${work_dir}/dragdiag-full.args"

IDF_PATH="$fake_idf_root" \
    FAKE_IDF_ARGS="${work_dir}/dragdiag-compat.args" \
    "${repo_root}/scripts/esp-idf-drag-diag.sh" \
    --no-full-invalidate \
    --build-dir "${work_dir}/dragdiag-compat" \
    --sdkconfig "${work_dir}/dragdiag-compat.sdkconfig" build >/dev/null
rg -Fx -- '-DSDKCONFIG_DEFAULTS=config/sdkconfig/sdkconfig.defaults;config/sdkconfig/sdkconfig.defaults.dragdiag' "${work_dir}/dragdiag-compat.args"

IDF_PATH="$fake_idf_root" \
    FIRMWARE_BUILD_ROOT="${work_dir}/dashboard-fireblade-build" \
    ESP_BMS_IDF_BUILD_ROOT="${work_dir}/dashboard-fireblade-idf-build" \
    FIRMWARE_OUTPUT_ROOT="${work_dir}/dashboard-fireblade-output" \
    FAKE_IDF_ARGS="${work_dir}/dashboard-fireblade-idf.args" \
    "${repo_root}/start.sh" compile-local --lang en --profile dashboard-fireblade --dashboards fireblade >/dev/null
rg -Fx 'set(ESP_BMS_FEATURE_DASHBOARD_S1000RR 0 CACHE BOOL "Firmware profile S1000RR dashboard" FORCE)' "${work_dir}/dashboard-fireblade-build/dashboard-fireblade/generated/profile.cmake"
rg -Fx 'set(ESP_BMS_FEATURE_DASHBOARD_CONTROLLER 0 CACHE BOOL "Firmware profile controller dashboard" FORCE)' "${work_dir}/dashboard-fireblade-build/dashboard-fireblade/generated/profile.cmake"
rg -Fx 'set(ESP_BMS_FEATURE_DASHBOARD_FIREBLADE 1 CACHE BOOL "Firmware profile Fireblade dashboard" FORCE)' "${work_dir}/dashboard-fireblade-build/dashboard-fireblade/generated/profile.cmake"
rg -Fq 'espressif/esp_lcd_st7796:' "${work_dir}/dashboard-fireblade-build/dashboard-fireblade/generated/idf_component.yml"
rg -Fq 'espressif/esp_lcd_touch_gt1151:' "${work_dir}/dashboard-fireblade-build/dashboard-fireblade/generated/idf_component.yml"
! rg -Fq 'atanisoft/esp_lcd_ili9488:' "${work_dir}/dashboard-fireblade-build/dashboard-fireblade/generated/idf_component.yml"
! rg -Fq 'espressif/esp_lcd_ili9341:' "${work_dir}/dashboard-fireblade-build/dashboard-fireblade/generated/idf_component.yml"
rg -Fx 'set(ESP_BMS_PROFILE_DRIVER_REQUIRES "esp_lcd_st7796;esp_lcd_touch_gt1151" CACHE STRING "Firmware profile display and touch components" FORCE)' "${work_dir}/dashboard-fireblade-build/dashboard-fireblade/generated/profile.cmake"

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

mkdir -p "${work_dir}/ascii-idf-build/golden/idf-build"
touch "${work_dir}/ascii-idf-build/golden/idf-build/CMakeCache.txt"
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
test -f "${work_dir}/output/golden/bootloader.bin"
test -f "${work_dir}/output/golden/partition-table.bin"
test -f "${work_dir}/output/golden/ota_data_initial.bin"
test -f "${work_dir}/output/golden/golden-flash.bin"
test -x "${work_dir}/output/golden/flash.sh"
test -f "${work_dir}/output/golden/README.md"
rg -Fq '"offset": "0x10000"' "${work_dir}/output/golden/flash-manifest.json"
rg -Fq '"file": "golden-flash.bin"' "${work_dir}/output/golden/flash-manifest.json"
rg -Fq 'write_flash' "${work_dir}/output/golden/flash.sh"
rg -Fq '`0x0`' "${work_dir}/output/golden/README.md"
ota_code="$(python3 -c 'import sys,zlib; data=open(sys.argv[1], "rb").read(); print(f"{zlib.crc32(data) % 10000:04d}")' "${work_dir}/output/golden/golden.bin")"
rg -Fq "OTA 四位验证码：\`${ota_code}\`" "${work_dir}/output/golden/README.md"
[[ "$(dd if="${work_dir}/output/golden/golden-flash.bin" bs=1 skip=4096 count=10 status=none)" == bootloader ]]
[[ "$(dd if="${work_dir}/output/golden/golden-flash.bin" bs=1 skip=65536 count=11 status=none)" == application ]]
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

delete_profile_root="${work_dir}/delete-saved-profile"
mkdir -p "$delete_profile_root/remove-me"
cp "${work_dir}/golden.env" "$delete_profile_root/remove-me/firmware.env"
printf 'y\n' | FIRMWARE_BUILD_ROOT="$delete_profile_root" bash -c '
    source <(sed -e "/^main /d" "$1")
    delete_saved_profile remove-me
' bash "${repo_root}/start.sh"
test ! -e "$delete_profile_root/remove-me"
rg -Fq -- '--delete-saved-profile' "${repo_root}/start.sh"
rg -Fq 'Remove-SavedProfile' "${repo_root}/start.ps1"

cat >"${work_dir}/custom.env" <<'EOF'
SCHEMA_VERSION=1
PROFILE=custom-gps
MCU=esp32
BOARD=custom
BOARD_NAME=my-esp32-board
DISPLAY=st7789-spi
INPUT=ft6336u-i2c
FLASH_MB=4
PSRAM_MB=0
PARTITIONS=firmware/partitions/esp32-wroom-32e-legacy.csv
INPUT_GPIO=TOUCH_INT:36,GPS_RX:27,GPS_PPS:35
OUTPUT_GPIO=TFT_MOSI:13,TFT_SCLK:14,TFT_CS:15,TFT_DC:2,TFT_BACKLIGHT:21,TOUCH_SDA:32,TOUCH_SCL:33,GPS_TX:18
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

printf '1\n2\n\n\n\n\nn\n' | FIRMWARE_BUILD_ROOT="${work_dir}/interactive-cancel-build" "${repo_root}/start.sh" >"${work_dir}/interactive-cancel.out"
rg -q '^[[:space:]]+[0-9]+\) ble-media-hid ' "${work_dir}/interactive-cancel.out"
! rg -q 'classic-media-hid' "${work_dir}/interactive-cancel.out"
rg -Fq '  1) gt1151-i2c ' "${work_dir}/interactive-cancel.out"
rg -Fq '  2) none ' "${work_dir}/interactive-cancel.out"
rg -q '^已取消生成配置。$' "${work_dir}/interactive-cancel.out"
! test -e "${work_dir}/interactive-cancel-build/esp32s3-n16r8-st7796u-gt1151/firmware.env"

printf '%s\n' \
    2 3 console-custom 1 '' '' st7789-spi 2 gps '1,2' \
    13 14 15 2 36 32 33 27 35 18 y y '' '' |
    FIRMWARE_BUILD_ROOT="${work_dir}/interactive-custom-build" "${repo_root}/start.sh" >"${work_dir}/interactive-custom.out"
rg -Fq '  3) custom ' "${work_dir}/interactive-custom.out"
rg -Fq 'MCU' "${work_dir}/interactive-custom.out"
rg -Fq 'GPIO range: 0-39' "${work_dir}/interactive-custom.out"
rg -Fq 'Selected MCU: esp32' "${work_dir}/interactive-custom.out"
rg -Fq 'st7789-spi — ST7789, 240 x 320' "${work_dir}/interactive-custom.out"
! rg -Fq 'ili9488-i80' "${work_dir}/interactive-custom.out"
[[ "$(rg -c '^[[:space:]]+[0-9]+\) none ' "${work_dir}/interactive-custom.out")" == 1 ]]
rg -Fq 'Display' "${work_dir}/interactive-custom.out"
rg -Fq 'Touch' "${work_dir}/interactive-custom.out"
! rg -Fq 'Custom display name' "${work_dir}/interactive-custom.out"
! rg -Fq 'Custom input name' "${work_dir}/interactive-custom.out"
rg -qx 'PROFILE=console-custom' "${work_dir}/interactive-custom-build/console-custom/firmware.env"
rg -qx 'BOARD=custom' "${work_dir}/interactive-custom-build/console-custom/firmware.env"
rg -qx 'DISPLAY=st7789-spi' "${work_dir}/interactive-custom-build/console-custom/firmware.env"
rg -qx 'INPUT=ft6336u-i2c' "${work_dir}/interactive-custom-build/console-custom/firmware.env"
rg -qx 'GPIO_GPS_RX=27' "${work_dir}/interactive-custom-build/console-custom/firmware.env"
rg -qx 'GPIO_TFT_MOSI=13' "${work_dir}/interactive-custom-build/console-custom/firmware.env"
rg -qx 'GPIO_TOUCH_SDA=32' "${work_dir}/interactive-custom-build/console-custom/firmware.env"
rg -qx 'DASHBOARDS=fireblade,s1000rr' "${work_dir}/interactive-custom-build/console-custom/firmware.env"

cat >"${work_dir}/none-touch.env" <<'EOF'
SCHEMA_VERSION=1
PROFILE=none-touch
MCU=esp32s3
BOARD=esp32s3-n16r8-st7796u-gt1151
DISPLAY=st7796u-i80
INPUT=none
MODULES=ota
EOF
FIRMWARE_BUILD_ROOT="${work_dir}/none-touch-build" "${repo_root}/start.sh" configure --lang en --config "${work_dir}/none-touch.env" >/dev/null
! rg -q '^GPIO_TOUCH_' "${work_dir}/none-touch-build/none-touch/firmware.env"
! rg -q '^  .*esp_lcd_touch_' "${work_dir}/none-touch-build/none-touch/generated/idf_component.yml"
rg -Fx 'set(ESP_BMS_PROFILE_DRIVER_REQUIRES "esp_lcd_st7796" CACHE STRING "Firmware profile display and touch components" FORCE)' "${work_dir}/none-touch-build/none-touch/generated/profile.cmake"

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
rg -Fq 'IsNullOrEmpty($script:BoardDisplayBus)' "${repo_root}/start.ps1"
rg -Fq 'board requires INPUT_BUS' "${repo_root}/start.ps1"
rg -Fq 'choose_dashboard_options_with_keyboard' "${repo_root}/start.sh"
rg -Fq 'choose_catalog_option_with_keyboard' "${repo_root}/start.sh"
[[ "$(rg -F "printf '\\033[2J\\033[H'" "${repo_root}/start.sh" | wc -l | tr -d '[:space:]')" == 2 ]]
rg -U -Fq $'choose_board_or_saved_profile\n    [[ "$MENU_RETURN_TO_PREVIOUS_FUNCTION_LIST" == YES ]] && continue' "${repo_root}/start.sh"
rg -U -Fq $'choose_catalog_option mcu \'MCU\' "${CFG[MCU]}" "${choices[@]}"\n                [[ "$MENU_RETURN_TO_PREVIOUS_FUNCTION_LIST" == YES ]] && continue' "${repo_root}/start.sh"
rg -U -Fq $'choose_catalog_option display \'Display\' "$default_option" "${choices[@]}"\n                [[ "$MENU_RETURN_TO_PREVIOUS_FUNCTION_LIST" == YES ]] && { stage=mcu; continue; }' "${repo_root}/start.sh"
rg -Fq '[[ "$MENU_RETURN_TO_PREVIOUS_FUNCTION_LIST" == YES ]] && { stage=display; continue; }' "${repo_root}/start.sh"
rg -Fq '[[ "$MENU_RETURN_TO_PREVIOUS_FUNCTION_LIST" == YES ]] && { stage=input; continue; }' "${repo_root}/start.sh"
rg -Fq 'Left to return to the previous feature list' "${repo_root}/start.ps1"
rg -Fq 'Left to return, Enter to continue.' "${repo_root}/start.ps1"
rg -Fq '← 返回上一个功能清单' "${repo_root}/start.sh"
rg -Fq 'MENU_RETURN_TO_PREVIOUS_FUNCTION_LIST=YES' "${repo_root}/start.sh"
rg -Fq '[ConsoleKey]::LeftArrow' "${repo_root}/start.ps1"
[[ "$(rg -F 'Clear-Host' "${repo_root}/start.ps1" | wc -l | tr -d '[:space:]')" == 2 ]]
rg -Fq 'function Update-KeyboardMenuPrefixes' "${repo_root}/start.ps1"
rg -Fq 'if ($script:ReturnToPreviousFunctionList) { $Stage = '\''display'\''; continue }' "${repo_root}/start.ps1"
rg -Fq 'if ($script:ReturnToPreviousFunctionList) { $Stage = '\''input'\''; continue }' "${repo_root}/start.ps1"
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
rg -Fq '$CloneAttempts = 3' "${repo_root}/start.ps1"
rg -Fq "Move-Item -LiteralPath \$CloneDirectory -Destination \$Directory" "${repo_root}/start.ps1"
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
rg -Fq 'Join-Path $env:SystemDrive "esp\esp-idf-v6.0.2"' "${repo_root}/scripts/serial_tcp_bridge.ps1"
rg -Fq 'Join-Path $env:SystemDrive "esp\esp-idf-tools\python_env"' "${repo_root}/scripts/serial_tcp_bridge.ps1"
rg -Fq "Initialize-IdfEnvironment" "${repo_root}/scripts/serial_tcp_bridge.ps1"
rg -Fq 'Join-Path $env:SystemDrive "esp\esp-idf-v6.0.2\export.ps1"' "${repo_root}/scripts/flash.ps1"
rg -Fq -- '-DIDF_TARGET="${CFG[MCU]}"' "${repo_root}/start.sh"
rg -Fq -- '"-DIDF_TARGET=$($Config.MCU)"' "${repo_root}/start.ps1"
rg -Fq '$env:FIRMWARE_BUILD_ROOT = $BuildRoot' "${repo_root}/start.ps1"
! rg -Fq 'esp-idf-v5.5.4' "${repo_root}/start.sh"
! rg -Fq 'esp-idf-v5.5.4' "${repo_root}/start.ps1"
rg -Fq 'DISPLAY_DATA_WIDTH' "${repo_root}/start.sh"
rg -Fq 'prompt_firmware_version' "${repo_root}/start.sh"
rg -Fq 'function Read-FirmwareVersion' "${repo_root}/start.ps1"
rg -Fq 'DATA_WIDTH' "${repo_root}/start.sh"

echo 'firmware configurator self-test passed'
