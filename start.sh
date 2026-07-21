#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CATALOG_DIR="${FIRMWARE_CATALOG_DIR:-$ROOT/firmware/catalog}"
BUILD_ROOT="${FIRMWARE_BUILD_ROOT:-$ROOT/firmware-builds}"
readonly SCHEMA_VERSION=1

declare -A CFG RECORD MODULE_STATE GPIO_VALUES GPIO_KINDS BOARD_GPIO
declare -a SELECTED_MODULES
declare -a FILTERED_ARGS

# The UI language is deliberately process-local.  Catalogs and generated
# KEY=VALUE/CMake files remain ASCII machine interfaces.
LANGUAGE="${FIRMWARE_LANG:-zh}"

message_text() {
    local text="$1"
    [[ "$LANGUAGE" == en ]] && {
        printf '%s' "$text"
        return
    }

    text="${text//error: /错误：}"
    text="${text//ok: /正常：}"
    text="${text//missing: /缺少：}"
    text="${text//valid: /校验通过：}"
    text="${text//profile: /配置档：}"
    text="${text//config: /配置：}"
    text="${text//normalized: /标准化配置：}"
    text="${text//disk-kb-available: /可用磁盘空间(KB)：}"
    text="${text//previous profile preserved at /已保留先前配置档：}"
    text="${text//cloud build request prepared; workflow dispatch belongs to /云构建请求已准备；工作流分派属于 }"
    text="${text//missing /缺少 }"
    text="${text//unknown /未知 }"
    text="${text//invalid /无效的 }"
    text="${text//unsupported /不支持的 }"
    text="${text//duplicate /重复的 }"
    text="${text//malformed /格式错误的 }"
    text="${text//configuration /配置}"
    text="${text//catalog /目录}"
    text="${text//schema /模式}"
    text="${text//record /记录}"
    text="${text//file /文件}"
    text="${text//key /键}"
    text="${text//value /值}"
    text="${text//module /模块}"
    text="${text//capability /能力}"
    text="${text//board /开发板}"
    text="${text//display /显示屏}"
    text="${text//input /输入}"
    text="${text//profile /配置档}"
    text="${text//option /选项}"
    text="${text//command /命令}"
    text="${text//partition /分区}"
    text="${text//path /路径}"
    text="${text//requires /需要}"
    text="${text//conflicts with /与 }"
    text="${text//assigned to both /同时分配给 }"
    text="${text//is unavailable on /在以下芯片不可用：}"
    text="${text//is input-only and cannot drive /仅可输入，不能驱动 }"
    text="${text//is dangerous; pass /是危险引脚；请传入 }"
    text="${text//does not accept options /不接受选项}"
    text="${text//is not build-ready yet /尚未具备本地构建条件}"
    text="${text//Profile /配置名称}"
    text="${text//Board /开发板}"
    text="${text//Display /显示屏}"
    text="${text//Input /输入设备}"
    text="${text//Modules /模块}"
    text="${text//profile=/配置档=}"
    text="${text//modules=/模块=}"
    printf '%s' "$text"
}

die() {
    printf '%s\n' "$(message_text "error: $*")" >&2
    exit 2
}

usage() {
    if [[ "$LANGUAGE" == en ]]; then
        cat <<'USAGE'
Usage: ./start.sh <command> [options]

Commands:
  doctor       Check the local ESP-IDF build prerequisites.
  configure    Validate a configuration and write one firmware.env file.
  validate     Validate a configuration without writing a profile.
  build-local  Write firmware.env, then compile it in an isolated directory.
  build-cloud  Validate and prepare a cloud-build request; it never pushes.

Options:
  --lang zh|en                 Language for this invocation (default: zh)
  --config FILE                KEY=VALUE configuration file
  --profile ID                 Profile name (default: legacy)
  --mcu ID                     esp32 or esp32s3
  --board ID                   Catalog board identifier
  --display ID                 Catalog display identifier
  --input ID                   Catalog input identifier
  --modules ID[,ID...]         Optional modules
  --gpio ROLE=PIN              Override a declared board GPIO role
  --confirm-dangerous-gpio     Allow a dangerous overridden GPIO
  -h, --help                   Show this help

Run without arguments to choose a language, then configure interactively.
USAGE
        return
    fi
    cat <<'USAGE'
用法：./start.sh <命令> [选项]

命令：
  doctor       检查本地 ESP-IDF 构建前置条件。
  configure    校验配置并写入一个 firmware.env 文件。
  validate     只校验配置，不写入配置档。
  build-local  写入 firmware.env，并在隔离目录中编译。
  build-cloud  校验并准备云构建请求；不会推送。

选项：
  --lang zh|en                 本次调用的语言（默认：zh）
  --config FILE                KEY=VALUE 配置文件
  --profile ID                 配置档名称（默认：legacy）
  --mcu ID                     esp32 或 esp32s3
  --board ID                   catalog 开发板标识
  --display ID                 catalog 显示屏标识
  --input ID                   catalog 输入设备标识
  --modules ID[,ID...]         可选模块
  --gpio ROLE=PIN              覆盖已声明的开发板 GPIO 角色
  --confirm-dangerous-gpio     允许危险 GPIO 覆盖
  -h, --help                   显示此帮助

无参数运行时会先选择语言，再进入交互式配置。
USAGE
}

set_language() {
    case "$1" in
        zh|en) LANGUAGE="$1" ;;
        *) die "invalid language: $1 (use zh or en)" ;;
    esac
}

filter_language_options() {
    FILTERED_ARGS=()
    while [[ $# -gt 0 ]]; do
        if [[ "$1" == --lang ]]; then
            [[ $# -ge 2 ]] || die "--lang requires zh or en"
            set_language "$2"
            shift 2
        else
            FILTERED_ARGS+=("$1")
            shift
        fi
    done
}

is_id() {
    [[ "$1" =~ ^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$ ]]
}

is_value() {
    [[ "$1" =~ ^[A-Za-z0-9,._:/-]*$ ]]
}

is_pin() {
    [[ "$1" =~ ^[0-9]{1,2}$ ]]
}

read_kv_file() {
    local file="$1"
    local map_name="$2"
    local -n map="$map_name"
    local line key value

    [[ -f "$file" ]] || die "missing file: $file"
    map=()
    while IFS= read -r line || [[ -n "$line" ]]; do
        line="${line%$'\r'}"
        [[ -z "$line" || "$line" == \#* ]] && continue
        [[ "$line" == *=* ]] || die "malformed KEY=VALUE record in $file"
        key="${line%%=*}"
        value="${line#*=}"
        [[ "$key" =~ ^[A-Z][A-Z0-9_]*$ ]] || die "invalid key $key in $file"
        is_value "$value" || die "invalid value for $key in $file"
        [[ -z "${map[$key]+x}" ]] || die "duplicate key $key in $file"
        map["$key"]="$value"
    done < "$file"
}

require_keys() {
    local map_name="$1"
    shift
    local -n map="$map_name"
    local key candidate found
    for key in "${!map[@]}"; do
        found=false
        for candidate in "$@"; do
            if [[ "$key" == "$candidate" ]]; then
                found=true
                break
            fi
        done
        [[ "$found" == true ]] || die "unknown key $key"
    done
}

require_key() {
    local map_name="$1"
    local key="$2"
    local -n map="$map_name"
    [[ -n "${map[$key]+x}" ]] || die "missing key $key"
}

load_record() {
    local kind="$1"
    local id="$2"
    local file

    is_id "$id" || die "invalid $kind identifier: $id"
    file="$CATALOG_DIR/$kind/$id.env"
    read_kv_file "$file" RECORD
    require_key RECORD SCHEMA_VERSION
    [[ "${RECORD[SCHEMA_VERSION]}" == "$SCHEMA_VERSION" ]] || die "unsupported catalog schema in $file"
    require_key RECORD ID
    [[ "${RECORD[ID]}" == "$id" ]] || die "catalog id mismatch in $file"
}

csv_has() {
    local list="$1"
    local wanted="$2"
    local item
    IFS=, read -r -a items <<< "$list"
    for item in "${items[@]}"; do
        [[ "$item" == "$wanted" ]] && return 0
    done
    return 1
}

sorted_csv() {
    local list="$1"
    local item
    [[ -n "$list" ]] || return 0
    IFS=, read -r -a items <<< "$list"
    for item in "${items[@]}"; do
        [[ -n "$item" ]] || continue
        printf '%s\n' "$item"
    done | LC_ALL=C sort -u | paste -sd, -
}

set_defaults() {
    CFG=()
    CFG[SCHEMA_VERSION]="$SCHEMA_VERSION"
    CFG[PROFILE]=legacy
    CFG[MCU]=esp32
    CFG[BOARD]=esp32-wroom-32e-legacy
    CFG[DISPLAY]=st7789-spi
    CFG[INPUT]=xpt2046-spi
    CFG[MODULES]=bms,gps,controller,audio,network,ota,cast
    CFG[CONFIRM_DANGEROUS_GPIO]=NO
}

load_user_config() {
    local file="$1"
    local -A input
    local key

    read_kv_file "$file" input
    require_key input SCHEMA_VERSION
    [[ "${input[SCHEMA_VERSION]}" == "$SCHEMA_VERSION" ]] || die "unsupported configuration schema"
    for key in "${!input[@]}"; do
        case "$key" in
            SCHEMA_VERSION|PROFILE|MCU|BOARD|DISPLAY|INPUT|MODULES|CONFIRM_DANGEROUS_GPIO)
                CFG["$key"]="${input[$key]}"
                ;;
            GPIO_*)
                [[ "$key" =~ ^GPIO_[A-Z][A-Z0-9_]*$ ]] || die "invalid GPIO override key $key"
                CFG["$key"]="${input[$key]}"
                ;;
            *)
                die "unknown configuration key $key"
                ;;
        esac
    done
}

parse_options() {
    local option value
    while [[ $# -gt 0 ]]; do
        option="$1"
        case "$option" in
            --config)
                [[ $# -ge 2 ]] || die "--config requires a file"
                load_user_config "$2"
                shift 2
                ;;
            --profile|--mcu|--board|--display|--input|--modules)
                [[ $# -ge 2 ]] || die "$option requires a value"
                value="$2"
                case "$option" in
                    --profile) CFG[PROFILE]="$value" ;;
                    --mcu) CFG[MCU]="$value" ;;
                    --board) CFG[BOARD]="$value" ;;
                    --display) CFG[DISPLAY]="$value" ;;
                    --input) CFG[INPUT]="$value" ;;
                    --modules) CFG[MODULES]="$value" ;;
                esac
                shift 2
                ;;
            --gpio)
                [[ $# -ge 2 && "$2" == *=* ]] || die "--gpio requires ROLE=PIN"
                local role="${2%%=*}"
                local pin="${2#*=}"
                [[ "$role" =~ ^[A-Z][A-Z0-9_]*$ ]] || die "invalid GPIO role $role"
                is_pin "$pin" || die "invalid GPIO pin $pin"
                CFG["GPIO_$role"]="$pin"
                shift 2
                ;;
            --confirm-dangerous-gpio)
                CFG[CONFIRM_DANGEROUS_GPIO]=YES
                shift
                ;;
            -h|--help)
                usage
                exit 0
                ;;
            *)
                die "unknown option: $option"
                ;;
        esac
    done
}

validate_modules() {
    local requested="${CFG[MODULES]}"
    local module
    MODULE_STATE=()
    SELECTED_MODULES=()
    IFS=, read -r -a modules <<< "$requested"
    for module in "${modules[@]}"; do
        [[ -n "$module" ]] || continue
        visit_module "$module"
    done
    CFG[MODULES]="$(printf '%s\n' "${SELECTED_MODULES[@]}" | LC_ALL=C sort -u | paste -sd, -)"
}

visit_module() {
    local module="$1"
    local dependencies capabilities conflicts dependency capability conflict

    is_id "$module" || die "invalid module id: $module"
    case "${MODULE_STATE[$module]:-}" in
        done) return ;;
        visiting) die "module dependency cycle at $module" ;;
    esac
    MODULE_STATE["$module"]=visiting
    load_record module "$module"
    require_keys RECORD SCHEMA_VERSION ID REQUIRES_CAPABILITIES REQUIRES_MODULES CONFLICTS COMPONENTS
    dependencies="${RECORD[REQUIRES_MODULES]}"
    capabilities="${RECORD[REQUIRES_CAPABILITIES]}"
    conflicts="${RECORD[CONFLICTS]}"
    IFS=, read -r -a items <<< "$capabilities"
    for capability in "${items[@]}"; do
        [[ -z "$capability" ]] || csv_has "$MCU_CAPABILITIES" "$capability" || die "$module requires capability $capability"
    done
    IFS=, read -r -a items <<< "$dependencies"
    for dependency in "${items[@]}"; do
        [[ -z "$dependency" ]] || visit_module "$dependency"
    done
    IFS=, read -r -a items <<< "$conflicts"
    for conflict in "${items[@]}"; do
        [[ -z "$conflict" || -z "${MODULE_STATE[$conflict]:-}" ]] || die "$module conflicts with $conflict"
    done
    MODULE_STATE["$module"]=done
    SELECTED_MODULES+=("$module")
}

load_gpio_list() {
    local kind="$1"
    local list="$2"
    local pair role pin
    IFS=, read -r -a pairs <<< "$list"
    for pair in "${pairs[@]}"; do
        [[ -n "$pair" ]] || continue
        [[ "$pair" == *:* ]] || die "invalid board GPIO entry $pair"
        role="${pair%%:*}"
        pin="${pair#*:}"
        [[ "$role" =~ ^[A-Z][A-Z0-9_]*$ ]] || die "invalid board GPIO role $role"
        is_pin "$pin" || die "invalid board GPIO pin $pin"
        [[ -z "${GPIO_VALUES[$role]+x}" ]] || die "duplicate board GPIO role $role"
        GPIO_VALUES["$role"]="$pin"
        BOARD_GPIO["$role"]="$pin"
        GPIO_KINDS["$role"]="$kind"
    done
}

validate_gpio() {
    local role pin prior_role prior_pin dangerous
    GPIO_VALUES=()
    GPIO_KINDS=()
    BOARD_GPIO=()
    load_gpio_list input "$BOARD_INPUT_GPIO"
    load_gpio_list output "$BOARD_OUTPUT_GPIO"
    for role in "${!CFG[@]}"; do
        [[ "$role" == GPIO_* ]] || continue
        role="${role#GPIO_}"
        [[ -n "${GPIO_VALUES[$role]+x}" ]] || die "GPIO override names an unknown board role: $role"
        GPIO_VALUES["$role"]="${CFG[GPIO_$role]}"
    done
    for role in "${!GPIO_VALUES[@]}"; do
        pin="${GPIO_VALUES[$role]}"
        is_pin "$pin" || die "invalid GPIO pin $pin for $role"
        (( pin <= MCU_GPIO_MAX )) || die "GPIO $pin for $role is unavailable on ${CFG[MCU]}"
        if [[ "${GPIO_KINDS[$role]}" == output ]] && csv_has "$MCU_INPUT_ONLY" "$pin"; then
            die "GPIO $pin is input-only and cannot drive $role"
        fi
        if csv_has "$MCU_DANGEROUS_GPIO" "$pin" && [[ "$pin" != "${BOARD_GPIO[$role]}" ]]; then
            [[ "${CFG[CONFIRM_DANGEROUS_GPIO]}" == YES ]] || die "GPIO $pin for $role is dangerous; pass --confirm-dangerous-gpio"
        fi
        for prior_role in "${!GPIO_VALUES[@]}"; do
            [[ "$prior_role" == "$role" ]] && continue
            prior_pin="${GPIO_VALUES[$prior_role]}"
            [[ "$prior_pin" != "$pin" ]] || die "GPIO $pin is assigned to both $role and $prior_role"
        done
    done
}

validate_config() {
    local path
    is_id "${CFG[PROFILE]}" || die "invalid profile name"
    is_id "${CFG[MCU]}" || die "invalid MCU id"
    is_id "${CFG[BOARD]}" || die "invalid board id"
    is_id "${CFG[DISPLAY]}" || die "invalid display id"
    is_id "${CFG[INPUT]}" || die "invalid input id"
    [[ "${CFG[CONFIRM_DANGEROUS_GPIO]}" == YES || "${CFG[CONFIRM_DANGEROUS_GPIO]}" == NO ]] || die "CONFIRM_DANGEROUS_GPIO must be YES or NO"

    read_kv_file "$CATALOG_DIR/schema.env" RECORD
    [[ "${RECORD[SCHEMA_VERSION]:-}" == "$SCHEMA_VERSION" ]] || die "unsupported catalog schema"
    load_record mcu "${CFG[MCU]}"
    require_keys RECORD SCHEMA_VERSION ID CAPABILITIES DISPLAY_BUSES GPIO_MAX INPUT_ONLY DANGEROUS_GPIO
    MCU_CAPABILITIES="${RECORD[CAPABILITIES]}"
    MCU_DISPLAY_BUSES="${RECORD[DISPLAY_BUSES]}"
    MCU_GPIO_MAX="${RECORD[GPIO_MAX]}"
    MCU_INPUT_ONLY="${RECORD[INPUT_ONLY]}"
    MCU_DANGEROUS_GPIO="${RECORD[DANGEROUS_GPIO]}"

    load_record board "${CFG[BOARD]}"
    require_keys RECORD SCHEMA_VERSION ID MCU DISPLAY_BUS INPUT_BUS FLASH_MB PSRAM_MB PARTITIONS BUILD_READY INPUT_GPIO OUTPUT_GPIO APPROVED_DANGEROUS_GPIO
    [[ "${RECORD[MCU]}" == "${CFG[MCU]}" ]] || die "board ${CFG[BOARD]} requires ${RECORD[MCU]}"
    BOARD_DISPLAY_BUS="${RECORD[DISPLAY_BUS]}"
    BOARD_INPUT_BUS="${RECORD[INPUT_BUS]}"
    BOARD_PARTITIONS="${RECORD[PARTITIONS]}"
    BOARD_BUILD_READY="${RECORD[BUILD_READY]}"
    BOARD_INPUT_GPIO="${RECORD[INPUT_GPIO]}"
    BOARD_OUTPUT_GPIO="${RECORD[OUTPUT_GPIO]}"
    [[ "$BOARD_PARTITIONS" == partitions.csv || "$BOARD_PARTITIONS" == firmware/partitions/* ]] || die "unsupported partition path: $BOARD_PARTITIONS"
    [[ "$BOARD_PARTITIONS" != *..* ]] || die "partition path traversal is not allowed"
    path="$ROOT/$BOARD_PARTITIONS"
    [[ -f "$path" ]] || die "board partition file is missing: $BOARD_PARTITIONS"

    load_record display "${CFG[DISPLAY]}"
    require_keys RECORD SCHEMA_VERSION ID BUS
    [[ "${RECORD[BUS]}" == "$BOARD_DISPLAY_BUS" ]] || die "display ${CFG[DISPLAY]} does not match board bus $BOARD_DISPLAY_BUS"
    csv_has "$MCU_DISPLAY_BUSES" "${RECORD[BUS]}" || die "${CFG[MCU]} does not support display bus ${RECORD[BUS]}"

    load_record input "${CFG[INPUT]}"
    require_keys RECORD SCHEMA_VERSION ID BUS
    [[ "${RECORD[BUS]}" == "$BOARD_INPUT_BUS" ]] || die "input ${CFG[INPUT]} does not match board bus $BOARD_INPUT_BUS"

    validate_modules
    validate_gpio
}

write_profile() {
    local profile="${CFG[PROFILE]}"
    local profile_dir="$BUILD_ROOT/$profile"
    local temporary backup partition_source role module main_requires audio_feature bms_feature controller_feature gps_feature network_feature ota_feature trimming

    mkdir -p "$BUILD_ROOT"
    temporary="$(mktemp -d "$BUILD_ROOT/.${profile}.tmp.XXXXXX")"
    mkdir -p "$temporary/generated"
    write_firmware_env "$temporary/firmware.env"
    main_requires="esp_bms_idf_runtime;esp_bms_lvgl_bridge;esp_bms_lvgl_ui;lvgl;esp_lvgl_adapter"
    audio_feature=0
    bms_feature=0
    controller_feature=0
    gps_feature=0
    network_feature=0
    ota_feature=0
    trimming="audio-component-excluded;legacy-runtime-untrimmed"
    if csv_has "${CFG[MODULES]}" gps; then
        main_requires="esp_bms_gps;${main_requires}"
        gps_feature=1
        trimming="gps-component-enabled;legacy-runtime-partially-untrimmed"
    fi
    if csv_has "${CFG[MODULES]}" audio; then
        main_requires="esp_bms_audio_feedback;${main_requires}"
        audio_feature=1
        trimming="audio-enabled;legacy-runtime-untrimmed"
    fi
    if csv_has "${CFG[MODULES]}" bms; then
        main_requires="esp_bms_bms_ble;${main_requires}"
        bms_feature=1
        trimming="bms-component-enabled;legacy-runtime-partially-untrimmed"
    fi
    if csv_has "${CFG[MODULES]}" controller; then
        main_requires="esp_bms_controller_ble;${main_requires}"
        controller_feature=1
        trimming="controller-component-enabled;legacy-runtime-partially-untrimmed"
    fi
    if csv_has "${CFG[MODULES]}" network; then
        main_requires="esp_bms_network;${main_requires}"
        network_feature=1
        trimming="network-component-enabled;legacy-runtime-partially-untrimmed"
    fi
    if csv_has "${CFG[MODULES]}" ota; then
        main_requires="esp_bms_ota;${main_requires}"
        ota_feature=1
        trimming="ota-component-enabled;legacy-runtime-partially-untrimmed"
    fi
    {
        printf 'set(ESP_BMS_PROFILE_ID "%s")\n' "$profile"
        printf 'set(ESP_BMS_SELECTED_MODULES "%s")\n' "${CFG[MODULES]}"
        printf 'set(ESP_BMS_PROFILE_TRIMMING_READY FALSE)\n'
        printf 'set(ESP_BMS_FEATURE_AUDIO %s CACHE BOOL "Firmware profile audio feature" FORCE)\n' "$audio_feature"
        printf 'set(ESP_BMS_FEATURE_BMS %s CACHE BOOL "Firmware profile BMS feature" FORCE)\n' "$bms_feature"
        printf 'set(ESP_BMS_FEATURE_CONTROLLER %s CACHE BOOL "Firmware profile controller feature" FORCE)\n' "$controller_feature"
        printf 'set(ESP_BMS_FEATURE_GPS %s CACHE BOOL "Firmware profile GPS feature" FORCE)\n' "$gps_feature"
        printf 'set(ESP_BMS_FEATURE_NETWORK %s CACHE BOOL "Firmware profile network feature" FORCE)\n' "$network_feature"
        printf 'set(ESP_BMS_FEATURE_OTA %s CACHE BOOL "Firmware profile OTA feature" FORCE)\n' "$ota_feature"
        printf 'set(ESP_BMS_PROFILE_MAIN_REQUIRES "%s" CACHE STRING "Firmware profile component closure" FORCE)\n' "$main_requires"
    } > "$temporary/generated/profile.cmake"
    {
        printf 'MODULES=%s\n' "${CFG[MODULES]}"
        for module in $(printf '%s\n' "${SELECTED_MODULES[@]}" | LC_ALL=C sort -u); do
            load_record module "$module"
            printf 'MODULE_%s_COMPONENTS=%s\n' "$module" "${RECORD[COMPONENTS]}"
        done
    } > "$temporary/generated/modules.env"
    cp "$ROOT/sdkconfig.defaults" "$temporary/sdkconfig.defaults"
    partition_source="$ROOT/$BOARD_PARTITIONS"
    cp "$partition_source" "$temporary/partitions.csv"
    {
        printf 'PROFILE=%s\n' "$profile"
        printf 'MCU=%s\n' "${CFG[MCU]}"
        printf 'BOARD=%s\n' "${CFG[BOARD]}"
        printf 'BUILD_READY=%s\n' "$BOARD_BUILD_READY"
        printf 'MODULES=%s\n' "${CFG[MODULES]}"
        printf 'TRIMMING=%s\n' "$trimming"
        printf 'NOTE=Generated selection will become the component closure after runtime extraction.\n'
    } > "$temporary/report.txt"
    if [[ -e "$profile_dir" ]]; then
        backup="$BUILD_ROOT/.${profile}.previous.$(date +%s)"
        mv "$profile_dir" "$backup"
        printf '%s\n' "$(message_text "previous profile preserved at ${backup#$ROOT/}")" >&2
    fi
    mv "$temporary" "$profile_dir"
    printf '%s\n' "$(message_text "profile: ${profile_dir#$ROOT/}")"
}

write_firmware_env() {
    local output_file="$1"
    {
        printf 'SCHEMA_VERSION=%s\n' "$SCHEMA_VERSION"
        printf 'PROFILE=%s\n' "$profile"
        printf 'MCU=%s\n' "${CFG[MCU]}"
        printf 'BOARD=%s\n' "${CFG[BOARD]}"
        printf 'DISPLAY=%s\n' "${CFG[DISPLAY]}"
        printf 'INPUT=%s\n' "${CFG[INPUT]}"
        printf 'MODULES=%s\n' "${CFG[MODULES]}"
        printf 'CONFIRM_DANGEROUS_GPIO=%s\n' "${CFG[CONFIRM_DANGEROUS_GPIO]}"
        for role in $(printf '%s\n' "${!GPIO_VALUES[@]}" | LC_ALL=C sort); do
            printf 'GPIO_%s=%s\n' "$role" "${GPIO_VALUES[$role]}"
        done
    } > "$output_file"
}

write_config() {
    local profile="${CFG[PROFILE]}"
    local profile_dir="$BUILD_ROOT/$profile"
    local temporary backup
    mkdir -p "$BUILD_ROOT"
    temporary="$(mktemp -d "$BUILD_ROOT/.${profile}.tmp.XXXXXX")"
    write_firmware_env "$temporary/firmware.env"
    if [[ -e "$profile_dir" ]]; then
        backup="$BUILD_ROOT/.${profile}.previous.$(date +%s)"
        mv "$profile_dir" "$backup"
        printf '%s\n' "$(message_text "previous profile preserved at ${backup#$ROOT/}")" >&2
    fi
    mv "$temporary" "$profile_dir"
    printf '%s\n' "$(message_text "config: ${profile_dir#$ROOT/}/firmware.env")"
}

run_doctor() {
    local missing=0 command ninja_path
    for command in git cmake python3; do
        if command -v "$command" >/dev/null 2>&1; then
            printf '%s\n' "$(message_text "ok: $command=$(command -v "$command")")"
        else
            printf '%s\n' "$(message_text "missing: $command")" >&2
            missing=1
        fi
    done
    ninja_path="$(command -v ninja || true)"
    if [[ -z "$ninja_path" ]]; then
        ninja_path="$(find "$HOME/.espressif/tools/ninja" -type f -name ninja -perm -u+x -print -quit 2>/dev/null || true)"
    fi
    if [[ -n "$ninja_path" ]]; then
        printf '%s\n' "$(message_text "ok: ninja=$ninja_path")"
    else
        printf '%s\n' "$(message_text 'missing: ninja')" >&2
        missing=1
    fi
    if [[ -f "${IDF_PATH:-}/export.sh" || -f "$HOME/esp/esp-idf-v5.5.4/export.sh" ]]; then
        printf '%s\n' "$(message_text 'ok: ESP-IDF export script')"
    else
        printf '%s\n' "$(message_text 'missing: ESP-IDF 5.5.4 export.sh')" >&2
        missing=1
    fi
    df -Pk "$ROOT" | awk 'NR == 2 { printf "%s\n", $4 }' | while IFS= read -r available; do
        printf '%s\n' "$(message_text "disk-kb-available: $available")"
    done
    (( missing == 0 ))
}

choose_interactive_language() {
    local answer
    while true; do
        printf '%s\n' '请选择语言 / Select language'
        printf '%s\n' '  1) 简体中文'
        printf '%s\n' '  2) English'
        read -r -p '输入 1 或 2 / Enter 1 or 2: ' answer
        case "$answer" in
            1|zh|ZH) LANGUAGE=zh; return ;;
            2|en|EN) LANGUAGE=en; return ;;
            *) printf '%s\n' '请输入 1、2、zh 或 en。 / Enter 1, 2, zh, or en.' >&2 ;;
        esac
    done
}

run_interactive() {
    local profile mcu board display input modules
    choose_interactive_language
    read -r -p "$(message_text 'Profile [legacy]: ')" profile
    read -r -p "$(message_text 'MCU [esp32]: ')" mcu
    read -r -p "$(message_text 'Board [esp32-wroom-32e-legacy]: ')" board
    read -r -p "$(message_text 'Display [st7789-spi]: ')" display
    read -r -p "$(message_text 'Input [xpt2046-spi]: ')" input
    read -r -p "$(message_text 'Modules [bms,gps,controller,audio,network,ota,cast]: ')" modules
    set_defaults
    [[ -z "$profile" ]] || CFG[PROFILE]="$profile"
    [[ -z "$mcu" ]] || CFG[MCU]="$mcu"
    [[ -z "$board" ]] || CFG[BOARD]="$board"
    [[ -z "$display" ]] || CFG[DISPLAY]="$display"
    [[ -z "$input" ]] || CFG[INPUT]="$input"
    [[ -z "$modules" ]] || CFG[MODULES]="$modules"
    validate_config
    write_config
}

main() {
    filter_language_options "$@"
    set -- "${FILTERED_ARGS[@]}"
    local command="${1:-}"
    [[ -n "$command" ]] || { run_interactive; return; }
    shift
    case "$command" in
        doctor)
            [[ $# -eq 0 ]] || die "doctor does not accept options"
            run_doctor
            ;;
        configure|validate|build-local|build-cloud|compile-local)
            set_defaults
            parse_options "$@"
            validate_config
            if [[ "$command" == validate ]]; then
                printf '%s\n' "$(message_text "valid: profile=${CFG[PROFILE]} modules=${CFG[MODULES]}")"
                return
            fi
            if [[ "$command" == configure ]]; then
                write_config
            elif [[ "$command" == build-local ]]; then
                write_config
                exec env FIRMWARE_LANG="$LANGUAGE" "$ROOT/scripts/build-profile.sh" --config "$BUILD_ROOT/${CFG[PROFILE]}/firmware.env"
            elif [[ "$command" == compile-local ]]; then
                write_profile
                [[ "$BOARD_BUILD_READY" == YES ]] || die "board ${CFG[BOARD]} is not build-ready yet"
                ESP_BMS_PROFILE_FILE="$BUILD_ROOT/${CFG[PROFILE]}/generated/profile.cmake" \
                    "$ROOT/scripts/esp-idf-env.sh" -B "$BUILD_ROOT/${CFG[PROFILE]}/idf-build" \
                    -DSDKCONFIG="$BUILD_ROOT/${CFG[PROFILE]}/sdkconfig" \
                    -DSDKCONFIG_DEFAULTS="$BUILD_ROOT/${CFG[PROFILE]}/sdkconfig.defaults" \
                    -DESP_BMS_PROFILE_FILE="$BUILD_ROOT/${CFG[PROFILE]}/generated/profile.cmake" build
            elif [[ "$command" == build-cloud ]]; then
                write_config
                printf '%s\n' "$(message_text 'cloud build request prepared; workflow dispatch belongs to 07-21-build-cloud-verification')" >&2
                return 3
            fi
            ;;
        -h|--help|help)
            usage
            ;;
        *)
            die "unknown command: $command"
            ;;
    esac
}

main "$@"
