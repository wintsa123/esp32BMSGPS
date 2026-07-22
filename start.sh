#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CATALOG_DIR="${FIRMWARE_CATALOG_DIR:-$ROOT/firmware/catalog}"
BUILD_ROOT="${FIRMWARE_BUILD_ROOT:-$ROOT/firmware-builds}"
# ESP-IDF's Xtensa toolchain cannot parse non-ASCII paths in generated specs.
# Keep its CMake tree on the ASCII volume, then publish only the firmware here.
IDF_BUILD_ROOT="${ESP_BMS_IDF_BUILD_ROOT:-/tmp/esp32-bms-gps-idf-builds/$UID}"
FIRMWARE_OUTPUT_ROOT="${FIRMWARE_OUTPUT_ROOT:-$ROOT/output}"
readonly SCHEMA_VERSION=1

declare -A CFG RECORD MODULE_STATE GPIO_VALUES GPIO_KINDS BOARD_GPIO REQUIRED_GPIO_KINDS MODULE_GPIO_ROLE_STATE CUSTOM_MODULE_ROLE_STATE
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
    text="${text//Profile/配置名称}"
    text="${text//Board/开发板}"
    text="${text//Display/显示屏}"
    text="${text//Input/输入设备}"
    text="${text//Modules/模块}"
    text="${text//Dashboard UIs/仪表界面}"
    text="${text//profile=/配置档=}"
    text="${text//modules=/模块=}"
    text="${text//dashboards=/仪表=}"
    printf '%s' "$text"
}

die() {
    printf '%s\n' "$(message_text "error: $*")" >&2
    exit 2
}

is_ascii_install_dir() {
    [[ "$1" =~ ^/[A-Za-z0-9._/-]+$ ]]
}

install_host_prerequisites() {
    local command system
    local -a missing=()
    for command in git python3; do
        command -v "$command" >/dev/null 2>&1 || missing+=("$command")
    done
    ((${#missing[@]} == 0)) && return

    system="$(uname -s)"
    if [[ "$LANGUAGE" == en ]]; then
        printf 'Installing %s prerequisites: %s\n' "$system" "${missing[*]}"
    else
        printf '正在安装 %s 编译前置条件：%s\n' "$system" "${missing[*]}"
    fi
    case "$system" in
        Linux)
            if command -v apt-get >/dev/null 2>&1; then
                sudo apt-get update
                sudo apt-get install -y git python3 python3-venv
            elif command -v dnf >/dev/null 2>&1; then
                sudo dnf install -y git python3 python3-virtualenv
            elif command -v pacman >/dev/null 2>&1; then
                sudo pacman -Sy --needed git python python-virtualenv
            elif command -v zypper >/dev/null 2>&1; then
                sudo zypper --non-interactive install git python3 python3-virtualenv
            else
                die 'unsupported Linux package manager; install git, python3, and python3-venv first'
            fi
            ;;
        Darwin)
            command -v brew >/dev/null 2>&1 || die 'Homebrew is required to install missing macOS build prerequisites'
            brew install git python
            ;;
        *) die "install-idf is unsupported on $system; use start.ps1 on Windows" ;;
    esac
    for command in git python3; do
        command -v "$command" >/dev/null 2>&1 || die "missing required command after installation: $command"
    done
}

install_idf() {
    local install_dir='' default_dir config_dir answer
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --dir)
                [[ $# -ge 2 ]] || die '--dir requires a directory'
                install_dir="$2"
                shift 2
                ;;
            *) die "install-idf does not accept option: $1" ;;
        esac
    done
    default_dir="$HOME/esp/esp-idf-v6.0.2"
    if [[ -z "$install_dir" ]]; then
        is_interactive_terminal || die 'install-idf requires --dir outside an interactive terminal'
        if [[ "$LANGUAGE" == en ]]; then
            read -r -p "ESP-IDF installation directory [$default_dir]: " answer
        else
            read -r -p "ESP-IDF 安装目录 [$default_dir]：" answer
        fi
        install_dir="${answer:-$default_dir}"
    fi
    is_ascii_install_dir "$install_dir" || die 'ESP-IDF installation directory must be an absolute ASCII path without spaces'
    [[ ! -e "$install_dir" ]] || die "installation directory already exists: $install_dir"

    install_host_prerequisites
    git clone --branch v6.0.2 --depth 1 --recursive --shallow-submodules \
        https://github.com/espressif/esp-idf.git "$install_dir"
    bash "$install_dir/install.sh" esp32 esp32s3

    config_dir="${XDG_CONFIG_HOME:-$HOME/.config}/esp32-bms-gps"
    mkdir -p "$config_dir"
    printf '%s\n' "$install_dir" > "$config_dir/idf-path"
    export IDF_PATH="$install_dir"
    "$ROOT/scripts/esp-idf-env.sh" --version >/dev/null
    if [[ "$LANGUAGE" == en ]]; then
        printf 'ESP-IDF v6.0.2 installed at %s\nProject environment configured at %s/idf-path\n' "$install_dir" "$config_dir"
    else
        printf 'ESP-IDF v6.0.2 已安装到 %s\n项目环境已配置到 %s/idf-path\n' "$install_dir" "$config_dir"
    fi
}

usage() {
    if [[ "$LANGUAGE" == en ]]; then
        cat <<'USAGE'
Usage: ./start.sh <command> [options]

ESP32 BMS GPS Firmware Configurator
Build a firmware plan from the bundled hardware and module catalog.

Commands:
  doctor       Check the local ESP-IDF build prerequisites.
  install-idf  Install ESP-IDF v6.0.2 and configure this project's environment.
  configure    Validate a configuration and write one firmware.env file.
  validate     Validate a configuration without writing a profile.
  build-local  Write firmware.env, then compile it in an isolated directory.
  build-cloud  Validate and trigger a cloud build; it never pushes.

Options:
  --lang zh|en                 Language for this invocation (default: zh)
  --config FILE                KEY=VALUE configuration file
  --profile ID                 Profile name (default: legacy)
  --mcu ID                     esp32 or esp32s3
  --board ID                   Catalog board identifier
  --display ID                 Catalog display identifier
  --input ID                   Catalog input identifier
  --board-name ID              Name for BOARD=custom
  --display-name ID            Name for DISPLAY=custom
  --input-name ID              Name for INPUT=custom
  --display-bus SPI|I80        Display bus for custom hardware
  --input-bus SPI|I2C|NONE     Input bus for custom hardware
  --flash-mb N                 Flash size for custom hardware
  --psram-mb N                 PSRAM size for custom hardware
  --partitions PATH            Existing partition CSV for custom hardware
  --modules ID[,ID...]         Optional modules
  --dashboards ID[,ID...]      Included dashboard UIs (s1000rr,controller,fireblade)
  --gpio ROLE=PIN              Override a declared board GPIO role
  --input-gpio ROLE=PIN        Declare a custom board input GPIO role
  --output-gpio ROLE=PIN       Declare a custom board output GPIO role
  --confirm-dangerous-gpio     Allow a dangerous overridden GPIO
  -h, --help                   Show this help

install-idf options:
  --dir DIR                    Absolute ASCII installation directory

Run without arguments to choose a language, then configure interactively.
USAGE
        return
    fi
    cat <<'USAGE'
用法：./start.sh <命令> [选项]

ESP32 BMS GPS 固件定制器
从内置硬件和功能目录选择方案，生成定制固件配置。

命令：
  doctor       检查本地 ESP-IDF 构建前置条件。
  install-idf  安装 ESP-IDF v6.0.2 并配置本项目环境。
  configure    校验配置并写入一个 firmware.env 文件。
  validate     只校验配置，不写入配置档。
  build-local  写入 firmware.env，并在隔离目录中编译。
  build-cloud  校验并触发云端构建；不会推送。

选项：
  --lang zh|en                 本次调用的语言（默认：zh）
  --config FILE                KEY=VALUE 配置文件
  --profile ID                 配置档名称（默认：legacy）
  --mcu ID                     esp32 或 esp32s3
  --board ID                   catalog 开发板标识
  --display ID                 catalog 显示屏标识
  --input ID                   catalog 输入设备标识
  --board-name ID              BOARD=custom 时的开发板名称
  --display-name ID            DISPLAY=custom 时的显示屏名称
  --input-name ID              INPUT=custom 时的输入设备名称
  --display-bus SPI|I80        自定义硬件的显示总线
  --input-bus SPI|I2C|NONE     自定义硬件的输入总线
  --flash-mb N                 自定义硬件的 Flash 容量
  --psram-mb N                 自定义硬件的 PSRAM 容量
  --partitions PATH            自定义硬件使用的已有分区 CSV
  --modules ID[,ID...]         可选模块
  --dashboards ID[,ID...]      编入的仪表 UI（s1000rr、controller、fireblade）
  --gpio ROLE=PIN              覆盖已声明的开发板 GPIO 角色
  --input-gpio ROLE=PIN        声明自定义开发板输入 GPIO 角色
  --output-gpio ROLE=PIN       声明自定义开发板输出 GPIO 角色
  --confirm-dangerous-gpio     允许危险 GPIO 覆盖
  -h, --help                   显示此帮助

install-idf 选项：
  --dir DIR                    绝对 ASCII 安装目录

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

is_unsigned_integer() {
    [[ "$1" =~ ^[0-9]+$ ]]
}

append_gpio_declaration() {
    local key="$1" role="$2" pin="$3" existing="${CFG[$1]}"
    [[ "$role" =~ ^[A-Z][A-Z0-9_]*$ ]] || die "invalid GPIO role $role"
    is_pin "$pin" || die "invalid GPIO pin $pin"
    CFG["$key"]="${existing:+${existing},}${role}:${pin}"
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
    CFG[PROFILE]=esp32s3-n16r8-st7796u-gt1151
    CFG[MCU]=esp32s3
    CFG[BOARD]=esp32s3-n16r8-st7796u-gt1151
    CFG[DISPLAY]=st7796u-i80
    CFG[INPUT]=gt1151-i2c
    CFG[BOARD_NAME]=''
    CFG[DISPLAY_NAME]=''
    CFG[INPUT_NAME]=''
    CFG[DISPLAY_BUS]=''
    CFG[INPUT_BUS]=''
    CFG[FLASH_MB]=''
    CFG[PSRAM_MB]=''
    CFG[PARTITIONS]=''
    CFG[INPUT_GPIO]=''
    CFG[OUTPUT_GPIO]=''
    CFG[MODULES]=bms,controller,network,ota,cast
    CFG[DASHBOARDS]=s1000rr,controller,fireblade
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
            SCHEMA_VERSION|PROFILE|MCU|BOARD|DISPLAY|INPUT|MODULES|DASHBOARDS|CONFIRM_DANGEROUS_GPIO|BOARD_NAME|DISPLAY_NAME|INPUT_NAME|DISPLAY_BUS|INPUT_BUS|FLASH_MB|PSRAM_MB|PARTITIONS|INPUT_GPIO|OUTPUT_GPIO)
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
            --profile|--mcu|--board|--display|--input|--modules|--dashboards|--board-name|--display-name|--input-name|--display-bus|--input-bus|--flash-mb|--psram-mb|--partitions)
                [[ $# -ge 2 ]] || die "$option requires a value"
                value="$2"
                case "$option" in
                    --profile) CFG[PROFILE]="$value" ;;
                    --mcu) CFG[MCU]="$value" ;;
                    --board) CFG[BOARD]="$value" ;;
                    --display) CFG[DISPLAY]="$value" ;;
                    --input) CFG[INPUT]="$value" ;;
                    --modules) CFG[MODULES]="$value" ;;
                    --dashboards) CFG[DASHBOARDS]="$value" ;;
                    --board-name) CFG[BOARD_NAME]="$value" ;;
                    --display-name) CFG[DISPLAY_NAME]="$value" ;;
                    --input-name) CFG[INPUT_NAME]="$value" ;;
                    --display-bus) CFG[DISPLAY_BUS]="$value" ;;
                    --input-bus) CFG[INPUT_BUS]="$value" ;;
                    --flash-mb) CFG[FLASH_MB]="$value" ;;
                    --psram-mb) CFG[PSRAM_MB]="$value" ;;
                    --partitions) CFG[PARTITIONS]="$value" ;;
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
            --input-gpio|--output-gpio)
                [[ $# -ge 2 && "$2" == *=* ]] || die "$option requires ROLE=PIN"
                local custom_role="${2%%=*}"
                local custom_pin="${2#*=}"
                if [[ "$option" == --input-gpio ]]; then
                    append_gpio_declaration INPUT_GPIO "$custom_role" "$custom_pin"
                else
                    append_gpio_declaration OUTPUT_GPIO "$custom_role" "$custom_pin"
                fi
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

validate_dashboards() {
    local dashboard
    local -a dashboards=()

    IFS=, read -r -a dashboards <<< "${CFG[DASHBOARDS]}"
    SELECTED_DASHBOARDS=()
    for dashboard in "${dashboards[@]}"; do
        [[ -n "$dashboard" ]] || continue
        is_id "$dashboard" || die "invalid dashboard id: $dashboard"
        load_record dashboard "$dashboard"
        require_keys RECORD SCHEMA_VERSION ID
        SELECTED_DASHBOARDS+=("$dashboard")
    done
    CFG[DASHBOARDS]="$(printf '%s\n' "${SELECTED_DASHBOARDS[@]}" | LC_ALL=C sort -u | paste -sd, -)"
    [[ -n "${CFG[DASHBOARDS]}" ]] || die 'select at least one dashboard UI'
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
    require_keys RECORD SCHEMA_VERSION ID REQUIRES_CAPABILITIES REQUIRES_MODULES REQUIRES_INPUT_GPIO REQUIRES_OUTPUT_GPIO CONFLICTS COMPONENTS
    dependencies="${RECORD[REQUIRES_MODULES]}"
    capabilities="${RECORD[REQUIRES_CAPABILITIES]}"
    conflicts="${RECORD[CONFLICTS]}"
    IFS=, read -r -a items <<< "$capabilities"
    for capability in "${items[@]}"; do
        [[ -z "$capability" ]] || csv_has "$MCU_CAPABILITIES" "$capability" || die "$module requires capability $capability"
    done
    validate_module_gpio_roles "$module" input "${RECORD[REQUIRES_INPUT_GPIO]}"
    validate_module_gpio_roles "$module" output "${RECORD[REQUIRES_OUTPUT_GPIO]}"
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

validate_module_gpio_roles() {
    local module="$1" expected_kind="$2" list="$3" role
    IFS=, read -r -a items <<< "$list"
    for role in "${items[@]}"; do
        [[ -z "$role" ]] && continue
        [[ -n "${GPIO_VALUES[$role]+x}" ]] || die "$module requires $expected_kind GPIO role $role"
        [[ "${GPIO_KINDS[$role]}" == "$expected_kind" ]] || die "$module requires $expected_kind GPIO role $role"
    done
}

require_gpio_role() {
    local kind="$1" role="$2" current_kind
    [[ "$kind" == input || "$kind" == output ]] || die "invalid GPIO direction for $role"
    [[ "$role" =~ ^[A-Z][A-Z0-9_]*$ ]] || die "invalid GPIO role $role"
    current_kind="${REQUIRED_GPIO_KINDS[$role]:-}"
    [[ -z "$current_kind" || "$current_kind" == "$kind" ]] || die "GPIO role $role has incompatible directions"
    REQUIRED_GPIO_KINDS["$role"]="$kind"
}

collect_module_gpio_roles() {
    local module="$1" dependency role
    [[ -z "${MODULE_GPIO_ROLE_STATE[$module]:-}" ]] || return 0
    MODULE_GPIO_ROLE_STATE["$module"]=seen
    load_record module "$module"
    IFS=, read -r -a items <<< "${RECORD[REQUIRES_INPUT_GPIO]}"
    for role in "${items[@]}"; do [[ -z "$role" ]] || require_gpio_role input "$role"; done
    IFS=, read -r -a items <<< "${RECORD[REQUIRES_OUTPUT_GPIO]}"
    for role in "${items[@]}"; do [[ -z "$role" ]] || require_gpio_role output "$role"; done
    IFS=, read -r -a items <<< "${RECORD[REQUIRES_MODULES]}"
    for dependency in "${items[@]}"; do [[ -z "$dependency" ]] || collect_module_gpio_roles "$dependency"; done
}

collect_required_gpio_roles() {
    local role module entry kind
    REQUIRED_GPIO_KINDS=()
    MODULE_GPIO_ROLE_STATE=()
    if [[ "${CFG[BOARD]}" == custom ]]; then
        csv_has "${CFG[MODULES]}" audio && die "custom board audio requires a catalog board hardware profile"
        while IFS= read -r entry; do
            [[ -n "$entry" ]] || continue
            kind="${entry%%:*}"
            role="${entry#*:}"
            require_gpio_role "$kind" "$role"
        done < <(custom_board_gpio_requirements)
        return
    fi

    case "$BOARD_DISPLAY_BUS" in
        SPI)
            for role in TFT_MOSI TFT_SCLK TFT_CS TFT_DC; do require_gpio_role output "$role"; done
            ;;
        I80)
            for ((index = 0; index < BOARD_DISPLAY_DATA_WIDTH; index++)); do require_gpio_role output "TFT_D$index"; done
            for role in TFT_WR TFT_CS TFT_DC; do require_gpio_role output "$role"; done
            ;;
    esac
    if [[ "${CFG[INPUT]}" != none ]]; then
        case "$BOARD_INPUT_BUS" in
            SPI)
                for role in TOUCH_MISO; do require_gpio_role input "$role"; done
                for role in TOUCH_MOSI TOUCH_CS TOUCH_SCLK; do require_gpio_role output "$role"; done
                ;;
            I2C)
                for role in TOUCH_SDA TOUCH_SCL; do require_gpio_role output "$role"; done
                ;;
        esac
        if [[ "${CFG[INPUT]}" != custom ]]; then
            load_record input "${CFG[INPUT]}"
            [[ "${RECORD[USE_IRQ]}" != 1 ]] || require_gpio_role input TOUCH_INT
        fi
    fi
    if board_declares_gpio_role BATTERY_ADC; then
        require_gpio_role input BATTERY_ADC
    fi
    IFS=, read -r -a items <<< "${CFG[MODULES]}"
    for module in "${items[@]}"; do [[ -z "$module" ]] || collect_module_gpio_roles "$module"; done
    if csv_has "${CFG[MODULES]}" audio; then
        case "$BOARD_AUDIO_BACKEND" in
            DAC)
                for role in AUDIO_DAC AUDIO_ENABLE; do require_gpio_role output "$role"; done
                ;;
            I2S)
                for role in I2S_BCLK I2S_LRCK I2S_DATA AMP_SHDN; do require_gpio_role output "$role"; done
                ;;
            NONE)
                die "board ${CFG[BOARD]} does not provide an audio hardware profile"
                ;;
            *)
                die "unsupported audio backend: $BOARD_AUDIO_BACKEND"
                ;;
        esac
    fi
}

board_declares_gpio_role() {
    local role="$1" pair
    for pair in ${BOARD_INPUT_GPIO//,/ } ${BOARD_OUTPUT_GPIO//,/ }; do
        [[ "${pair%%:*}" == "$role" ]] && return 0
    done
    return 1
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
        [[ -n "${REQUIRED_GPIO_KINDS[$role]+x}" ]] || continue
        [[ "${REQUIRED_GPIO_KINDS[$role]}" == "$kind" ]] || die "board GPIO role $role has the wrong direction"
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
    collect_required_gpio_roles
    load_gpio_list input "$BOARD_INPUT_GPIO"
    load_gpio_list output "$BOARD_OUTPUT_GPIO"
    for role in "${!CFG[@]}"; do
        [[ "$role" == GPIO_* ]] || continue
        role="${role#GPIO_}"
        if [[ -z "${REQUIRED_GPIO_KINDS[$role]+x}" ]]; then
            [[ "${CFG[BOARD]}" != custom ]] && board_declares_gpio_role "$role" && continue
            die "GPIO override names an unknown required role: $role"
        fi
        GPIO_VALUES["$role"]="${CFG[GPIO_$role]}"
    done
    for role in "${!REQUIRED_GPIO_KINDS[@]}"; do
        [[ -n "${GPIO_VALUES[$role]+x}" ]] || die "missing required ${REQUIRED_GPIO_KINDS[$role]} GPIO role $role"
        GPIO_KINDS["$role"]="${REQUIRED_GPIO_KINDS[$role]}"
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
    local path display_bus input_bus
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

    if [[ "${CFG[BOARD]}" == custom ]]; then
        is_id "${CFG[BOARD_NAME]}" || die "custom board requires a valid BOARD_NAME"
        is_unsigned_integer "${CFG[FLASH_MB]}" && (( 10#${CFG[FLASH_MB]} > 0 )) || die "custom board requires a positive FLASH_MB"
        is_unsigned_integer "${CFG[PSRAM_MB]}" || die "custom board requires a non-negative PSRAM_MB"
        BOARD_DISPLAY_BUS="${CFG[DISPLAY_BUS]}"
        BOARD_DISPLAY_DATA_WIDTH="${BOARD_DISPLAY_DATA_WIDTH:-8}"
        BOARD_INPUT_BUS="${CFG[INPUT_BUS]}"
        BOARD_PARTITIONS="${CFG[PARTITIONS]}"
        BOARD_BUILD_READY=NO
        BOARD_AUDIO_BACKEND=NONE
        BOARD_AUDIO_DAC_CHANNEL=0
        BOARD_AUDIO_ENABLE_ACTIVE_LEVEL=0
        BOARD_INPUT_GPIO="${CFG[INPUT_GPIO]}"
        BOARD_OUTPUT_GPIO="${CFG[OUTPUT_GPIO]}"
    else
        load_record board "${CFG[BOARD]}"
        require_keys RECORD SCHEMA_VERSION ID MCU DISPLAY_BUS DISPLAY_DATA_WIDTH INPUT_BUS FLASH_MB PSRAM_MB PARTITIONS BUILD_READY AUDIO_BACKEND AUDIO_DAC_CHANNEL AUDIO_ENABLE_ACTIVE_LEVEL INPUT_GPIO OUTPUT_GPIO APPROVED_DANGEROUS_GPIO
        [[ "${RECORD[MCU]}" == "${CFG[MCU]}" ]] || die "board ${CFG[BOARD]} requires ${RECORD[MCU]}"
        BOARD_DISPLAY_BUS="${RECORD[DISPLAY_BUS]}"
        BOARD_DISPLAY_DATA_WIDTH="${RECORD[DISPLAY_DATA_WIDTH]}"
        BOARD_INPUT_BUS="${RECORD[INPUT_BUS]}"
        BOARD_PARTITIONS="${RECORD[PARTITIONS]}"
        BOARD_BUILD_READY="${RECORD[BUILD_READY]}"
        BOARD_AUDIO_BACKEND="${RECORD[AUDIO_BACKEND]}"
        BOARD_AUDIO_DAC_CHANNEL="${RECORD[AUDIO_DAC_CHANNEL]}"
        BOARD_AUDIO_ENABLE_ACTIVE_LEVEL="${RECORD[AUDIO_ENABLE_ACTIVE_LEVEL]}"
        BOARD_INPUT_GPIO="${RECORD[INPUT_GPIO]}"
        BOARD_OUTPUT_GPIO="${RECORD[OUTPUT_GPIO]}"
    fi
    display_bus="$BOARD_DISPLAY_BUS"
    input_bus="$BOARD_INPUT_BUS"
    [[ -n "$display_bus" ]] || die "custom board requires DISPLAY_BUS"
    csv_has "$MCU_DISPLAY_BUSES" "$display_bus" || die "${CFG[MCU]} does not support display bus $display_bus"
    [[ "$input_bus" == SPI || "$input_bus" == I2C || "$input_bus" == NONE ]] || die "unsupported input bus: $input_bus"
    case "$BOARD_AUDIO_BACKEND" in
        DAC) [[ "$BOARD_AUDIO_DAC_CHANNEL" == 1 || "$BOARD_AUDIO_DAC_CHANNEL" == 2 ]] || die "DAC board requires AUDIO_DAC_CHANNEL 1 or 2" ;;
        I2S|NONE) [[ "$BOARD_AUDIO_DAC_CHANNEL" == 0 ]] || die "$BOARD_AUDIO_BACKEND board requires AUDIO_DAC_CHANNEL 0" ;;
        *) die "unsupported audio backend: $BOARD_AUDIO_BACKEND" ;;
    esac
    [[ "$BOARD_AUDIO_ENABLE_ACTIVE_LEVEL" == 0 || "$BOARD_AUDIO_ENABLE_ACTIVE_LEVEL" == 1 ]] || die "AUDIO_ENABLE_ACTIVE_LEVEL must be 0 or 1"
    [[ "$BOARD_PARTITIONS" == partitions.csv || "$BOARD_PARTITIONS" == firmware/partitions/* ]] || die "unsupported partition path: $BOARD_PARTITIONS"
    [[ "$BOARD_PARTITIONS" != *..* ]] || die "partition path traversal is not allowed"
    path="$ROOT/$BOARD_PARTITIONS"
    [[ -f "$path" ]] || die "board partition file is missing: $BOARD_PARTITIONS"

    if [[ "${CFG[DISPLAY]}" == custom ]]; then
        is_id "${CFG[DISPLAY_NAME]}" || die "custom display requires a valid DISPLAY_NAME"
    else
        load_record display "${CFG[DISPLAY]}"
        require_keys RECORD SCHEMA_VERSION ID BUS DATA_WIDTH DRIVER WIDTH HEIGHT PIXEL_CLOCK_HZ ROTATION RGB_ORDER INVERT_COLOR SPI_MODE I80_SWAP_COLOR_BYTES I80_PCLK_ACTIVE_NEG I80_PCLK_IDLE_LOW BACKLIGHT_ON_LEVEL POWER_ON_DELAY_MS
        [[ "${RECORD[BUS]}" == "$BOARD_DISPLAY_BUS" ]] || die "display ${CFG[DISPLAY]} does not match board bus $BOARD_DISPLAY_BUS"
        if [[ "${CFG[BOARD]}" == custom ]]; then
            BOARD_DISPLAY_DATA_WIDTH="${RECORD[DATA_WIDTH]}"
        else
            [[ "${RECORD[DATA_WIDTH]}" == "$BOARD_DISPLAY_DATA_WIDTH" ]] || die "display ${CFG[DISPLAY]} does not match board data width $BOARD_DISPLAY_DATA_WIDTH"
        fi
    fi

    if [[ "${CFG[INPUT]}" == custom ]]; then
        is_id "${CFG[INPUT_NAME]}" || die "custom input requires a valid INPUT_NAME"
    elif [[ "${CFG[INPUT]}" == none ]]; then
        [[ "$BOARD_INPUT_BUS" == NONE ]] || die "input none requires board input bus NONE"
    else
        load_record input "${CFG[INPUT]}"
        require_keys RECORD SCHEMA_VERSION ID BUS DRIVER USE_IRQ SWAP_XY MIRROR_X MIRROR_Y I2C_ADDRESS I2C_CLOCK_HZ I2C_CONTROL_PHASE_BYTES I2C_DC_BIT_OFFSET I2C_CMD_BITS I2C_PARAM_BITS I2C_DISABLE_CONTROL_PHASE I2C_INTERNAL_PULLUP RESET_LEVEL IRQ_LEVEL
        [[ "${RECORD[BUS]}" == "$BOARD_INPUT_BUS" ]] || die "input ${CFG[INPUT]} does not match board bus $BOARD_INPUT_BUS"
    fi

    validate_gpio
    validate_modules
    validate_dashboards
    [[ "${CFG[BOARD]}" != custom ]] || validate_custom_board_gpio_roles
}

custom_required_gpio_roles() {
    local module="$1" dependency role
    [[ -z "${CUSTOM_MODULE_ROLE_STATE[$module]:-}" ]] || return
    CUSTOM_MODULE_ROLE_STATE["$module"]=seen
    load_record module "$module"
    IFS=, read -r -a items <<< "${RECORD[REQUIRES_INPUT_GPIO]}"
    for role in "${items[@]}"; do [[ -z "$role" ]] || printf 'input:%s\n' "$role"; done
    IFS=, read -r -a items <<< "${RECORD[REQUIRES_OUTPUT_GPIO]}"
    for role in "${items[@]}"; do [[ -z "$role" ]] || printf 'output:%s\n' "$role"; done
    IFS=, read -r -a items <<< "${RECORD[REQUIRES_MODULES]}"
    for dependency in "${items[@]}"; do [[ -z "$dependency" ]] || custom_required_gpio_roles "$dependency"; done
}

validate_custom_board_gpio_roles() {
    local kind role
    while IFS= read -r kind; do
        [[ -n "$kind" ]] || continue
        role="${kind#*:}"
        kind="${kind%%:*}"
        [[ -n "${GPIO_VALUES[$role]+x}" && "${GPIO_KINDS[$role]}" == "$kind" ]] || die "custom board requires $kind GPIO role $role"
    done < <(custom_board_gpio_requirements)
}

write_profile() {
    local profile="${CFG[PROFILE]}"
    local profile_dir="$BUILD_ROOT/$profile"
    local temporary backup partition_source sdkconfig_source role module main_requires audio_feature bms_feature controller_feature gps_feature network_feature ota_feature dashboard_s1000rr_feature dashboard_controller_feature dashboard_fireblade_feature trimming

    mkdir -p "$BUILD_ROOT"
    temporary="$(mktemp -d "$BUILD_ROOT/.${profile}.tmp.XXXXXX")"
    mkdir -p "$temporary/generated"
    write_firmware_env "$temporary/firmware.env"
    python3 "$ROOT/scripts/generate-hardware-config.py" \
        --catalog "$CATALOG_DIR" \
        --firmware-env "$temporary/firmware.env" \
        --output "$temporary/generated/esp_bms_profile_hardware.h"
    main_requires="esp_bms_idf_runtime;esp_bms_lvgl_bridge;esp_bms_lvgl_ui;lvgl;esp_lvgl_adapter"
    audio_feature=0
    bms_feature=0
    controller_feature=0
    gps_feature=0
    network_feature=0
    ota_feature=0
    dashboard_s1000rr_feature=0
    dashboard_controller_feature=0
    dashboard_fireblade_feature=0
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
    csv_has "${CFG[DASHBOARDS]}" s1000rr && dashboard_s1000rr_feature=1
    csv_has "${CFG[DASHBOARDS]}" controller && dashboard_controller_feature=1
    csv_has "${CFG[DASHBOARDS]}" fireblade && dashboard_fireblade_feature=1
    {
        printf 'set(ESP_BMS_PROFILE_ID "%s")\n' "$profile"
        printf 'set(ESP_BMS_SELECTED_MODULES "%s")\n' "${CFG[MODULES]}"
        printf 'set(ESP_BMS_SELECTED_DASHBOARDS "%s")\n' "${CFG[DASHBOARDS]}"
        printf 'set(ESP_BMS_PROFILE_TRIMMING_READY FALSE)\n'
        printf 'set(ESP_BMS_FEATURE_AUDIO %s CACHE BOOL "Firmware profile audio feature" FORCE)\n' "$audio_feature"
        printf 'set(ESP_BMS_FEATURE_BMS %s CACHE BOOL "Firmware profile BMS feature" FORCE)\n' "$bms_feature"
        printf 'set(ESP_BMS_FEATURE_CONTROLLER %s CACHE BOOL "Firmware profile controller feature" FORCE)\n' "$controller_feature"
        printf 'set(ESP_BMS_FEATURE_GPS %s CACHE BOOL "Firmware profile GPS feature" FORCE)\n' "$gps_feature"
        printf 'set(ESP_BMS_FEATURE_NETWORK %s CACHE BOOL "Firmware profile network feature" FORCE)\n' "$network_feature"
        printf 'set(ESP_BMS_FEATURE_OTA %s CACHE BOOL "Firmware profile OTA feature" FORCE)\n' "$ota_feature"
        printf 'set(ESP_BMS_FEATURE_DASHBOARD_S1000RR %s CACHE BOOL "Firmware profile S1000RR dashboard" FORCE)\n' "$dashboard_s1000rr_feature"
        printf 'set(ESP_BMS_FEATURE_DASHBOARD_CONTROLLER %s CACHE BOOL "Firmware profile controller dashboard" FORCE)\n' "$dashboard_controller_feature"
        printf 'set(ESP_BMS_FEATURE_DASHBOARD_FIREBLADE %s CACHE BOOL "Firmware profile Fireblade dashboard" FORCE)\n' "$dashboard_fireblade_feature"
        printf 'set(ESP_BMS_PROFILE_MAIN_REQUIRES "%s" CACHE STRING "Firmware profile component closure" FORCE)\n' "$main_requires"
    } > "$temporary/generated/profile.cmake"
    {
        printf 'MODULES=%s\n' "${CFG[MODULES]}"
        printf 'DASHBOARDS=%s\n' "${CFG[DASHBOARDS]}"
        for module in $(printf '%s\n' "${SELECTED_MODULES[@]}" | LC_ALL=C sort -u); do
            load_record module "$module"
            printf 'MODULE_%s_COMPONENTS=%s\n' "$module" "${RECORD[COMPONENTS]}"
        done
    } > "$temporary/generated/modules.env"
    sdkconfig_source="$ROOT/sdkconfig.defaults.${CFG[MCU]}"
    [[ -f "$sdkconfig_source" ]] || sdkconfig_source="$ROOT/sdkconfig.defaults"
    {
        sed '/^CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=/d' "$sdkconfig_source"
        printf 'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="%s"\n' "$profile_dir/partitions.csv"
    } > "$temporary/sdkconfig.defaults"
    partition_source="$ROOT/$BOARD_PARTITIONS"
    cp "$partition_source" "$temporary/partitions.csv"
    {
        printf 'PROFILE=%s\n' "$profile"
        printf 'MCU=%s\n' "${CFG[MCU]}"
        printf 'BOARD=%s\n' "${CFG[BOARD]}"
        printf 'BUILD_READY=%s\n' "$BOARD_BUILD_READY"
        printf 'MODULES=%s\n' "${CFG[MODULES]}"
        printf 'DASHBOARDS=%s\n' "${CFG[DASHBOARDS]}"
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
        printf 'DASHBOARDS=%s\n' "${CFG[DASHBOARDS]}"
        printf 'CONFIRM_DANGEROUS_GPIO=%s\n' "${CFG[CONFIRM_DANGEROUS_GPIO]}"
        for key in BOARD_NAME DISPLAY_NAME INPUT_NAME DISPLAY_BUS INPUT_BUS FLASH_MB PSRAM_MB PARTITIONS INPUT_GPIO OUTPUT_GPIO; do
            [[ -n "${CFG[$key]}" ]] && printf '%s=%s\n' "$key" "${CFG[$key]}"
        done
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
    local missing=0 command tool_path tools_root
    for command in git python3; do
        if command -v "$command" >/dev/null 2>&1; then
            printf '%s\n' "$(message_text "ok: $command=$(command -v "$command")")"
        else
            printf '%s\n' "$(message_text "missing: $command")" >&2
            missing=1
        fi
    done
    tools_root="${IDF_TOOLS_PATH:-$HOME/.espressif}/tools"
    for command in cmake ninja; do
        tool_path="$(command -v "$command" || true)"
        if [[ -z "$tool_path" ]]; then
            tool_path="$(find "$tools_root/$command" -type f -name "$command" -perm -u+x -print -quit 2>/dev/null || true)"
        fi
        if [[ -n "$tool_path" ]]; then
            printf '%s\n' "$(message_text "ok: $command=$tool_path")"
        else
            printf '%s\n' "$(message_text "missing: $command")" >&2
            missing=1
        fi
    done
    if idf_version="$ROOT/scripts/esp-idf-env.sh --version" 2>&1; then
        printf '%s\n' "$(message_text "ok: $idf_version")"
    else
        printf '%s\n' "$(message_text "missing: $idf_version")" >&2
        missing=1
    fi
    df -Pk "$ROOT" | awk 'NR == 2 { printf "%s\n", $4 }' | while IFS= read -r available; do
        printf '%s\n' "$(message_text "disk-kb-available: $available")"
    done
    (( missing == 0 ))
}

dispatch_cloud_build() {
    env FIRMWARE_LANG="$LANGUAGE" python3 "$ROOT/scripts/dispatch-cloud-build.py" --config "$1"
}

choose_interactive_language() {
    local answer
    printf '%s\n' '=== ESP32 BMS GPS Firmware Configurator / ESP32 BMS GPS 固件定制器 ==='
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

print_interactive_title() {
    if [[ "$LANGUAGE" == en ]]; then
        cat <<'TITLE'

========================================
 ESP32 BMS GPS Firmware Configurator
 Choose hardware and optional features to create a firmware plan.
========================================
TITLE
    else
        cat <<'TITLE'

========================================
 ESP32 BMS GPS 固件定制器
 选择硬件与可选功能，生成固件构建方案。
========================================
TITLE
    fi
}

catalog_option_description() {
    local kind="$1" id="$2" zh en
    case "$kind:$id" in
        mcu:esp32) zh='ESP32，最多 39 路 GPIO，支持 SPI 显示'; en='ESP32, up to GPIO39, SPI display support' ;;
        mcu:esp32s3) zh='ESP32-S3，最多 48 路 GPIO，支持 SPI / I80 显示'; en='ESP32-S3, up to GPIO48, SPI / I80 display support' ;;
        board:esp32-wroom-32e-legacy) zh='ESP32-WROOM-32E，4MB Flash，可本地构建（推荐）'; en='ESP32-WROOM-32E, 4MB Flash, build-ready (recommended)' ;;
        board:esp32s3-n16r8-st7796u-gt1151) zh='慧勤智远 ESP32-S3 N16R8，ST7796U / GT1151，16MB Flash / 8MB PSRAM，可本地构建'; en='Huiqin Zhiyuan ESP32-S3 N16R8, ST7796U / GT1151, 16MB Flash / 8MB PSRAM, build-ready' ;;
        board:custom) zh='自定义开发板：选择 MCU、显示屏并填写 GPIO'; en='Custom board: choose MCU, display, and GPIO pins' ;;
        display:st7789-spi) zh='ST7789 SPI 显示屏'; en='ST7789 SPI display' ;;
        display:ili9488-i80) zh='ILI9488 8080 并行显示屏'; en='ILI9488 I80 parallel display' ;;
        display:st7796u-i80) zh='ST7796U 16 位 8080 并行显示屏（320 × 480）'; en='ST7796U 16-bit I80 parallel display (320 × 480)' ;;
        display:custom) zh='自定义显示屏：填写名称并选择总线'; en='Custom display: name it and choose its bus' ;;
        input:xpt2046-spi) zh='XPT2046 SPI 触摸屏'; en='XPT2046 SPI touch input' ;;
        input:ft6336u-i2c) zh='FT6336U I2C 触摸屏'; en='FT6336U I2C touch input' ;;
        input:gt1151-i2c) zh='GT1151 I2C 电容触摸屏'; en='GT1151 I2C capacitive touch input' ;;
        input:custom) zh='自定义输入设备：填写名称并选择总线'; en='Custom input: name it and choose its bus' ;;
        input:none) zh='不使用输入设备'; en='No input device' ;;
        module:bms) zh='BMS 蓝牙连接'; en='BMS Bluetooth connection' ;;
        module:gps) zh='GPS 定位与测速'; en='GPS positioning and speed' ;;
        module:controller) zh='控制器蓝牙'; en='Controller Bluetooth' ;;
        module:audio) zh='音频提示'; en='Audio feedback' ;;
        module:network) zh='Wi-Fi、设置热点与本地网页'; en='Wi-Fi, setup AP, and local web UI' ;;
        module:ota) zh='本地 Web OTA 更新（自动需要 network）'; en='Local web OTA update (automatically requires network)' ;;
        module:cast) zh='实验性手机投屏（当前使用 legacy runtime）'; en='Experimental phone casting (uses the legacy runtime)' ;;
        dashboard:s1000rr) zh='宝马 S1000RR 速度仪表'; en='BMW S1000RR speed dashboard' ;;
        dashboard:controller) zh='控制器监控仪表'; en='Controller monitoring dashboard' ;;
        dashboard:fireblade) zh='本田火刃仪表'; en='Honda Fireblade dashboard' ;;
        *) zh='目录中的可选项'; en='Catalog option' ;;
    esac
    [[ "$LANGUAGE" == en ]] && printf '%s' "$en" || printf '%s' "$zh"
}

catalog_ids() {
    local kind="$1" file id
    for file in "$CATALOG_DIR/$kind"/*.env; do
        [[ -f "$file" ]] || continue
        id="${file##*/}"
        id="${id%.env}"
        is_id "$id" && printf '%s\n' "$id"
    done | LC_ALL=C sort -u
}

catalog_ids_matching() {
    local kind="$1" key="$2" expected="$3" file id
    local -A record=()
    for file in "$CATALOG_DIR/$kind"/*.env; do
        [[ -f "$file" ]] || continue
        id="${file##*/}"
        id="${id%.env}"
        is_id "$id" || continue
        read_kv_file "$file" record
        [[ "${record[$key]:-}" == "$expected" ]] && printf '%s\n' "$id"
    done | LC_ALL=C sort -u
}

catalog_display_ids_supported_by_mcu() {
    local mcu="$1" file id bus mcu_display_buses
    local -A display_record=()
    load_record mcu "$mcu"
    mcu_display_buses="${RECORD[DISPLAY_BUSES]}"
    for file in "$CATALOG_DIR/display"/*.env; do
        [[ -f "$file" ]] || continue
        id="${file##*/}"
        id="${id%.env}"
        read_kv_file "$file" display_record
        bus="${display_record[BUS]:-}"
        csv_has "$mcu_display_buses" "$bus" && printf '%s\n' "$id"
    done | LC_ALL=C sort -u
}

choose_catalog_option() {
    local kind="$1" title="$2" default="$3" answer option index
    shift 3
    local -a choices=("$@")
    ((${#choices[@]} > 0)) || die "no compatible $kind catalog options"
    while true; do
        printf '\n%s\n' "$(message_text "$title")"
        for index in "${!choices[@]}"; do
            option="${choices[$index]}"
            printf '  %d) %s — %s%s\n' "$((index + 1))" "$option" "$(catalog_option_description "$kind" "$option")" "$([[ "$option" == "$default" ]] && printf ' *')"
        done
        if [[ "$LANGUAGE" == en ]]; then
            read -r -p "Enter a number or ID [$default]: " answer
        else
            read -r -p "输入编号或 ID [$default]：" answer
        fi
        [[ -n "$answer" ]] || answer="$default"
        if [[ "$answer" =~ ^[0-9]+$ ]] && ((10#$answer >= 1 && 10#$answer <= ${#choices[@]})); then
            MENU_SELECTION="${choices[$((10#$answer - 1))]}"
            return
        fi
        for option in "${choices[@]}"; do
            if [[ "$answer" == "$option" ]]; then
                MENU_SELECTION="$option"
                return
            fi
        done
        [[ "$LANGUAGE" == en ]] && printf '%s\n' 'Invalid selection; please try again.' >&2 || printf '%s\n' '无效选择，请重新输入。' >&2
    done
}

choose_value_option() {
    local title="$1" default="$2" answer option index
    shift 2
    local -a choices=("$@")
    ((${#choices[@]} > 0)) || die "no $title options"
    while true; do
        printf '\n%s\n' "$(message_text "$title")"
        for index in "${!choices[@]}"; do
            option="${choices[$index]}"
            printf '  %d) %s%s\n' "$((index + 1))" "$option" "$([[ "$option" == "$default" ]] && printf ' *')"
        done
        if [[ "$LANGUAGE" == en ]]; then
            read -r -p "Enter a number or value [$default]: " answer
        else
            read -r -p "输入编号或值 [$default]：" answer
        fi
        [[ -n "$answer" ]] || answer="$default"
        if [[ "$answer" =~ ^[0-9]+$ ]] && ((10#$answer >= 1 && 10#$answer <= ${#choices[@]})); then
            MENU_SELECTION="${choices[$((10#$answer - 1))]}"
            return
        fi
        for option in "${choices[@]}"; do
            [[ "$answer" == "$option" ]] && { MENU_SELECTION="$option"; return; }
        done
        [[ "$LANGUAGE" == en ]] && printf '%s\n' 'Invalid selection; please try again.' >&2 || printf '%s\n' '无效选择，请重新输入。' >&2
    done
}

prompt_custom_id() {
    local key="$1" zh_prompt="$2" en_prompt="$3" answer
    while true; do
        if [[ "$LANGUAGE" == en ]]; then
            read -r -p "$en_prompt: " answer
        else
            read -r -p "$zh_prompt：" answer
        fi
        is_id "$answer" && { CFG["$key"]="$answer"; return; }
        [[ "$LANGUAGE" == en ]] && printf '%s\n' 'Use 1-64 ASCII letters, numbers, hyphens, or underscores.' >&2 || printf '%s\n' '请使用 1–64 位 ASCII 字母、数字、连字符或下划线。' >&2
    done
}

prompt_custom_number() {
    local key="$1" default="$2" zh_prompt="$3" en_prompt="$4" answer
    while true; do
        if [[ "$LANGUAGE" == en ]]; then
            read -r -p "$en_prompt [$default]: " answer
        else
            read -r -p "$zh_prompt [$default]：" answer
        fi
        [[ -n "$answer" ]] || answer="$default"
        is_unsigned_integer "$answer" && { CFG["$key"]="$answer"; return; }
        [[ "$LANGUAGE" == en ]] && printf '%s\n' 'Enter a non-negative integer.' >&2 || printf '%s\n' '请输入非负整数。' >&2
    done
}

custom_board_gpio_requirements() {
    local module
    CUSTOM_MODULE_ROLE_STATE=()
    case "$BOARD_DISPLAY_BUS" in
        SPI) printf '%s\n' output:TFT_MOSI output:TFT_SCLK output:TFT_CS output:TFT_DC output:TFT_BACKLIGHT ;;
        I80)
            printf '%s\n' output:TFT_D0 output:TFT_D1 output:TFT_D2 output:TFT_D3 output:TFT_D4 output:TFT_D5 output:TFT_D6 output:TFT_D7
            [[ "$BOARD_DISPLAY_DATA_WIDTH" != 16 ]] || printf '%s\n' output:TFT_D8 output:TFT_D9 output:TFT_D10 output:TFT_D11 output:TFT_D12 output:TFT_D13 output:TFT_D14 output:TFT_D15
            printf '%s\n' output:TFT_WR output:TFT_RD output:TFT_CS output:TFT_DC output:TFT_RESET output:TFT_BACKLIGHT
            ;;
    esac
    case "$BOARD_INPUT_BUS" in
        SPI) printf '%s\n' input:TOUCH_IRQ input:TOUCH_MISO output:TOUCH_MOSI output:TOUCH_CS output:TOUCH_SCLK ;;
        I2C) printf '%s\n' input:TOUCH_IRQ output:TOUCH_SDA output:TOUCH_SCL ;;
    esac
    IFS=, read -r -a items <<< "${CFG[MODULES]}"
    for module in "${items[@]}"; do
        [[ -z "$module" ]] || custom_required_gpio_roles "$module"
    done
}

configure_custom_board_gpio() {
    local entry kind role answer input_fd
    local -A seen=()
    CFG[INPUT_GPIO]=''
    CFG[OUTPUT_GPIO]=''
    exec {input_fd}<&0
    while IFS= read -r entry; do
        [[ -n "$entry" ]] || continue
        kind="${entry%%:*}"
        role="${entry#*:}"
        [[ -z "${seen[$kind:$role]+x}" ]] || continue
        seen[$kind:$role]=1
        while true; do
            if [[ "$LANGUAGE" == en ]]; then
                read -r -u "$input_fd" -p "$role GPIO (0-$MCU_GPIO_MAX): " answer
            else
                read -r -u "$input_fd" -p "$role 的 GPIO（0–$MCU_GPIO_MAX）：" answer
            fi
            is_pin "$answer" && { append_gpio_declaration "${kind^^}_GPIO" "$role" "$answer"; break; }
            [[ "$LANGUAGE" == en ]] && printf '%s\n' 'Enter a GPIO number from 0 to 99.' >&2 || printf '%s\n' '请输入 0–99 的 GPIO 编号。' >&2
        done
    done < <(custom_board_gpio_requirements)
    exec {input_fd}<&-
    for entry in "${CFG[INPUT_GPIO]}" "${CFG[OUTPUT_GPIO]}"; do
        IFS=, read -r -a items <<< "$entry"
        for role in "${items[@]}"; do
            answer="${role#*:}"
            if csv_has "$MCU_DANGEROUS_GPIO" "$answer"; then
                if [[ "$LANGUAGE" == en ]]; then
                    read -r -p "GPIO $answer is a boot-sensitive pin. Confirm its use? [y/N]: " role
                else
                    read -r -p "GPIO $answer 是启动敏感引脚，确认使用吗？[y/N]：" role
                fi
                [[ "$role" =~ ^[Yy]$ ]] || die "dangerous GPIO $answer was not confirmed"
                CFG[CONFIRM_DANGEROUS_GPIO]=YES
            fi
        done
    done
}

configure_custom_board() {
    local -a choices=()
    CFG[BOARD]=custom
    prompt_custom_id BOARD_NAME '自定义开发板名称（ASCII）' 'Custom board name (ASCII)'
    mapfile -t choices < <(catalog_ids mcu)
    choose_catalog_option mcu 'MCU' "${CFG[MCU]}" "${choices[@]}"
    CFG[MCU]="$MENU_SELECTION"
    load_record mcu "${CFG[MCU]}"
    MCU_DISPLAY_BUSES="${RECORD[DISPLAY_BUSES]}"
    MCU_GPIO_MAX="${RECORD[GPIO_MAX]}"
    MCU_DANGEROUS_GPIO="${RECORD[DANGEROUS_GPIO]}"
    prompt_custom_number FLASH_MB 4 'Flash 容量（MB）' 'Flash size (MB)'
    prompt_custom_number PSRAM_MB 0 'PSRAM 容量（MB）' 'PSRAM size (MB)'
    CFG[PARTITIONS]=partitions.csv

    mapfile -t choices < <(catalog_display_ids_supported_by_mcu "${CFG[MCU]}")
    choices+=(custom)
    choose_catalog_option display 'Display' custom "${choices[@]}"
    CFG[DISPLAY]="$MENU_SELECTION"
    if [[ "${CFG[DISPLAY]}" == custom ]]; then
        prompt_custom_id DISPLAY_NAME '自定义显示屏名称（ASCII）' 'Custom display name (ASCII)'
        read -r -a choices <<< "${MCU_DISPLAY_BUSES//,/ }"
        choose_value_option 'Display bus' "${choices[0]}" "${choices[@]}"
        CFG[DISPLAY_BUS]="$MENU_SELECTION"
        if [[ "${CFG[DISPLAY_BUS]}" == I80 ]]; then
            choose_value_option 'I80 data width' 8 8 16
            BOARD_DISPLAY_DATA_WIDTH="$MENU_SELECTION"
        else
            BOARD_DISPLAY_DATA_WIDTH=0
        fi
    else
        load_record display "${CFG[DISPLAY]}"
        CFG[DISPLAY_BUS]="${RECORD[BUS]}"
        BOARD_DISPLAY_DATA_WIDTH="${RECORD[DATA_WIDTH]}"
    fi

    mapfile -t choices < <(catalog_ids input)
    choices+=(custom none)
    choose_catalog_option input 'Input' custom "${choices[@]}"
    CFG[INPUT]="$MENU_SELECTION"
    if [[ "${CFG[INPUT]}" == custom ]]; then
        prompt_custom_id INPUT_NAME '自定义输入设备名称（ASCII）' 'Custom input name (ASCII)'
        choose_value_option 'Input bus' SPI SPI I2C NONE
        CFG[INPUT_BUS]="$MENU_SELECTION"
    elif [[ "${CFG[INPUT]}" == none ]]; then
        CFG[INPUT_BUS]=NONE
    else
        load_record input "${CFG[INPUT]}"
        CFG[INPUT_BUS]="${RECORD[BUS]}"
    fi
    BOARD_DISPLAY_BUS="${CFG[DISPLAY_BUS]}"
    BOARD_INPUT_BUS="${CFG[INPUT_BUS]}"
    choose_module_options
    CFG[MODULES]="$MENU_SELECTION"
    choose_dashboard_options
    CFG[DASHBOARDS]="$MENU_SELECTION"
    configure_custom_board_gpio
}

is_interactive_terminal() {
    [[ -t 0 && -t 1 ]]
}

flash_built_firmware() {
    local build_dir="$1" answer port
    local -a flash_args=(-B "$build_dir")

    if [[ "$LANGUAGE" == en ]]; then
        read -r -p 'Flash target: 1) Local serial (default) 2) Remote RFC2217 [1]: ' answer
    else
        read -r -p '烧录目标：1) 本地串口（默认）2) 远程 RFC2217 [1]：' answer
    fi
    case "$answer" in
        ''|1|local)
            if [[ "$LANGUAGE" == en ]]; then
                read -r -p 'Local serial port (for example /dev/ttyUSB0; blank uses the ESP-IDF default): ' port
            else
                read -r -p '本地串口（例如 /dev/ttyUSB0；留空使用 ESP-IDF 默认值）：' port
            fi
            [[ -z "$port" ]] || flash_args+=(-p "$port")
            ;;
        2|remote)
            if [[ "$LANGUAGE" == en ]]; then
                read -r -p 'Remote RFC2217 URL: ' port
            else
                read -r -p '远程 RFC2217 地址：' port
            fi
            [[ "$port" == rfc2217://* ]] || die 'remote flash requires an rfc2217:// URL'
            flash_args+=(-p "$port" -b 115200)
            ;;
        *)
            [[ "$LANGUAGE" == en ]] && printf '%s\n' 'Flash canceled: invalid target.' >&2 || printf '%s\n' '已取消烧录：目标无效。' >&2
            return
            ;;
    esac
    "$ROOT/scripts/esp-idf-env.sh" "${flash_args[@]}" flash
}

choose_module_options_with_keyboard() {
    local key suffix option index pointer mark
    local cursor=0
    local -a choices=()
    local -A selected=()

    mapfile -t choices < <(catalog_ids module)
    ((${#choices[@]} > 0)) || die 'no module catalog options'
    for option in "${choices[@]}"; do
        if csv_has "${CFG[MODULES]}" "$option"; then
            selected["$option"]=1
        fi
    done

    while true; do
        printf '\033[2J\033[H'
        print_interactive_title
        printf '\n%s\n' "$(message_text 'Modules')"
        for index in "${!choices[@]}"; do
            option="${choices[$index]}"
            pointer=' '
            (( index == cursor )) && pointer='>'
            mark='[ ]'
            [[ -n "${selected[$option]+x}" ]] && mark='[x]'
            printf '  %s %s %d) %s — %s\n' "$pointer" "$mark" "$((index + 1))" "$option" "$(catalog_option_description module "$option")"
        done
        if [[ "$LANGUAGE" == en ]]; then
            printf '%s\n' 'Use Up/Down to move, Space to toggle, Enter to continue.'
        else
            printf '%s\n' '使用 ↑/↓ 移动，空格切换，回车下一步。'
        fi

        key=''
        IFS= read -rsn1 key || {
            MENU_SELECTION="${CFG[MODULES]}"
            return
        }
        if [[ "$key" == $'\e' ]]; then
            suffix=''
            IFS= read -rsn2 -t 0.1 suffix || true
            key+="$suffix"
        fi
        case "$key" in
            $'\e[A'|$'\eOA')
                if (( cursor == 0 )); then cursor=$((${#choices[@]} - 1)); else cursor=$((cursor - 1)); fi
                ;;
            $'\e[B'|$'\eOB')
                cursor=$(((cursor + 1) % ${#choices[@]}))
                ;;
            ' ')
                option="${choices[$cursor]}"
                if [[ -n "${selected[$option]+x}" ]]; then unset "selected[$option]"; else selected["$option"]=1; fi
                ;;
            ''|$'\r')
                if ((${#selected[@]} == 0)); then
                    MENU_SELECTION=''
                else
                    MENU_SELECTION="$(printf '%s\n' "${!selected[@]}" | LC_ALL=C sort | paste -sd, -)"
                fi
                return
                ;;
        esac
    done
}

choose_module_options() {
    local answer entry option candidate index
    local -a choices=() selected=() entries=()
    if is_interactive_terminal; then
        choose_module_options_with_keyboard
        return
    fi
    mapfile -t choices < <(catalog_ids module)
    while true; do
        printf '\n%s\n' "$(message_text 'Modules')"
        printf '  0) %s\n' "$([[ "$LANGUAGE" == en ]] && printf 'No optional modules' || printf '不启用可选功能')"
        for index in "${!choices[@]}"; do
            option="${choices[$index]}"
            printf '  %d) %s — %s%s\n' "$((index + 1))" "$option" "$(catalog_option_description module "$option")" "$([[ ",${CFG[MODULES]}," == *",$option,"* ]] && printf ' *')"
        done
        if [[ "$LANGUAGE" == en ]]; then
            read -r -p "Enter comma-separated numbers or IDs [${CFG[MODULES]}]: " answer
        else
            read -r -p "输入以逗号分隔的编号或 ID [${CFG[MODULES]}]：" answer
        fi
        [[ -n "$answer" ]] || { MENU_SELECTION="${CFG[MODULES]}"; return; }
        [[ "$answer" == 0 ]] && { MENU_SELECTION=''; return; }
        IFS=, read -r -a entries <<< "$answer"
        selected=()
        for entry in "${entries[@]}"; do
            entry="${entry//[[:space:]]/}"
            candidate=''
            if [[ "$entry" =~ ^[0-9]+$ ]] && ((10#$entry >= 1 && 10#$entry <= ${#choices[@]})); then
                candidate="${choices[$((10#$entry - 1))]}"
            else
                for option in "${choices[@]}"; do
                    [[ "$entry" == "$option" ]] && { candidate="$option"; break; }
                done
            fi
            [[ -n "$candidate" ]] || {
                [[ "$LANGUAGE" == en ]] && printf '%s\n' 'Invalid module selection; please try again.' >&2 || printf '%s\n' '无效功能选择，请重新输入。' >&2
                selected=()
                break
            }
            selected+=("$candidate")
        done
        ((${#selected[@]} > 0)) || continue
        MENU_SELECTION="$(printf '%s\n' "${selected[@]}" | LC_ALL=C sort -u | paste -sd, -)"
        return
    done
}

choose_dashboard_options() {
    local answer entry option candidate index
    local -a choices=() selected=() entries=()

    mapfile -t choices < <(catalog_ids dashboard)
    ((${#choices[@]} > 0)) || die 'no dashboard catalog options'
    while true; do
        printf '\n%s\n' "$(message_text 'Dashboard UIs')"
        for index in "${!choices[@]}"; do
            option="${choices[$index]}"
            printf '  %d) %s — %s%s\n' "$((index + 1))" "$option" "$(catalog_option_description dashboard "$option")" "$([[ ",${CFG[DASHBOARDS]}," == *",$option,"* ]] && printf ' *')"
        done
        if [[ "$LANGUAGE" == en ]]; then
            read -r -p "Enter comma-separated numbers or IDs [${CFG[DASHBOARDS]}]: " answer
        else
            read -r -p "输入以逗号分隔的编号或 ID [${CFG[DASHBOARDS]}]：" answer
        fi
        [[ -n "$answer" ]] || { MENU_SELECTION="${CFG[DASHBOARDS]}"; return; }
        IFS=, read -r -a entries <<< "$answer"
        selected=()
        for entry in "${entries[@]}"; do
            entry="${entry//[[:space:]]/}"
            candidate=''
            if [[ "$entry" =~ ^[0-9]+$ ]] && ((10#$entry >= 1 && 10#$entry <= ${#choices[@]})); then
                candidate="${choices[$((10#$entry - 1))]}"
            else
                for option in "${choices[@]}"; do
                    [[ "$entry" == "$option" ]] && { candidate="$option"; break; }
                done
            fi
            [[ -n "$candidate" ]] || {
                [[ "$LANGUAGE" == en ]] && printf '%s\n' 'Invalid dashboard selection; please try again.' >&2 || printf '%s\n' '无效仪表选择，请重新输入。' >&2
                selected=()
                break
            }
            selected+=("$candidate")
        done
        ((${#selected[@]} > 0)) || continue
        MENU_SELECTION="$(printf '%s\n' "${selected[@]}" | LC_ALL=C sort -u | paste -sd, -)"
        return
    done
}

set_interactive_profile_name() {
    local source="${CFG[BOARD]}" answer
    [[ "$source" == custom ]] && source="${CFG[BOARD_NAME]}"
    [[ -n "$source" ]] || source="${CFG[MCU]}"
    CFG[PROFILE]="$source"
    [[ "${1:-}" == prompt ]] || return 0
    is_interactive_terminal || return 0
    while true; do
        if [[ "$LANGUAGE" == en ]]; then
            read -r -p "Configuration name [$source]: " answer
        else
            read -r -p "配置名称 [$source]：" answer
        fi
        [[ -z "$answer" ]] && return
        is_id "$answer" && { CFG[PROFILE]="$answer"; return; }
        [[ "$LANGUAGE" == en ]] && printf '%s\n' 'Use 1-64 ASCII letters, numbers, _ or -.' >&2 || printf '%s\n' '请输入 1–64 个 ASCII 字母、数字、_ 或 -。' >&2
    done
}

configure_missing_board_gpio() {
    local role answer
    local -a missing=()

    load_record mcu "${CFG[MCU]}"
    MCU_CAPABILITIES="${RECORD[CAPABILITIES]}"
    MCU_DISPLAY_BUSES="${RECORD[DISPLAY_BUSES]}"
    MCU_GPIO_MAX="${RECORD[GPIO_MAX]}"
    MCU_INPUT_ONLY="${RECORD[INPUT_ONLY]}"
    MCU_DANGEROUS_GPIO="${RECORD[DANGEROUS_GPIO]}"
    load_record board "${CFG[BOARD]}"
    BOARD_DISPLAY_BUS="${RECORD[DISPLAY_BUS]}"
    BOARD_DISPLAY_DATA_WIDTH="${RECORD[DISPLAY_DATA_WIDTH]}"
    BOARD_INPUT_BUS="${RECORD[INPUT_BUS]}"
    BOARD_AUDIO_BACKEND="${RECORD[AUDIO_BACKEND]}"
    BOARD_AUDIO_DAC_CHANNEL="${RECORD[AUDIO_DAC_CHANNEL]}"
    BOARD_AUDIO_ENABLE_ACTIVE_LEVEL="${RECORD[AUDIO_ENABLE_ACTIVE_LEVEL]}"
    BOARD_INPUT_GPIO="${RECORD[INPUT_GPIO]}"
    BOARD_OUTPUT_GPIO="${RECORD[OUTPUT_GPIO]}"
    collect_required_gpio_roles
    GPIO_VALUES=()
    GPIO_KINDS=()
    BOARD_GPIO=()
    load_gpio_list input "$BOARD_INPUT_GPIO"
    load_gpio_list output "$BOARD_OUTPUT_GPIO"
    for role in $(printf '%s\n' "${!REQUIRED_GPIO_KINDS[@]}" | LC_ALL=C sort); do
        [[ -n "${GPIO_VALUES[$role]+x}" ]] || missing+=("$role")
    done
    for role in "${missing[@]}"; do
        while true; do
            if [[ "$LANGUAGE" == en ]]; then
                read -r -p "$role GPIO (0-$MCU_GPIO_MAX): " answer
            else
                read -r -p "$role 的 GPIO（0–$MCU_GPIO_MAX）：" answer
            fi
            is_pin "$answer" || {
                [[ "$LANGUAGE" == en ]] && printf '%s\n' 'Enter a GPIO number from 0 to 99.' >&2 || printf '%s\n' '请输入 0–99 的 GPIO 编号。' >&2
                continue
            }
            CFG["GPIO_$role"]="$answer"
            if csv_has "$MCU_DANGEROUS_GPIO" "$answer"; then
                if [[ "$LANGUAGE" == en ]]; then
                    read -r -p "GPIO $answer is a boot-sensitive pin. Confirm its use? [y/N]: " answer
                else
                    read -r -p "GPIO $answer 是启动敏感引脚，确认使用吗？[y/N]：" answer
                fi
                [[ "$answer" =~ ^[Yy]$ ]] || die "dangerous GPIO $answer was not confirmed"
                CFG[CONFIRM_DANGEROUS_GPIO]=YES
            fi
            break
        done
    done
}

saved_profile_paths() {
    local candidate profile firmware_env
    [[ -d "$BUILD_ROOT" ]] || return
    for candidate in "$BUILD_ROOT"/*; do
        [[ -d "$candidate" && ! -L "$candidate" ]] || continue
        profile="${candidate##*/}"
        is_id "$profile" || continue
        firmware_env="$candidate/firmware.env"
        [[ -f "$firmware_env" && ! -L "$firmware_env" ]] || continue
        if (set_defaults; load_user_config "$firmware_env"; validate_config) >/dev/null 2>&1; then
            printf '%s\n' "$profile"
        fi
    done | LC_ALL=C sort -u
}

choose_board_or_saved_profile() {
    local answer option index
    local -a boards=() saved=() choices=()
    mapfile -t boards < <(catalog_ids board)
    boards+=(custom)
    mapfile -t saved < <(saved_profile_paths)
    choices=("${boards[@]}")
    for option in "${saved[@]}"; do choices+=("saved:$option"); done
    while true; do
        printf '\n%s\n' "$(message_text 'Board')"
        for index in "${!choices[@]}"; do
            option="${choices[$index]}"
            if [[ "$option" == saved:* ]]; then
                printf '  %d) [%s] %s\n' "$((index + 1))" "$([[ "$LANGUAGE" == en ]] && printf 'Saved configuration' || printf '已保存配置')" "${option#saved:}"
            else
                printf '  %d) %s — %s%s\n' "$((index + 1))" "$option" "$(catalog_option_description board "$option")" "$([[ "$option" == "${CFG[BOARD]}" ]] && printf ' *')"
            fi
        done
        if [[ "$LANGUAGE" == en ]]; then
            read -r -p "Enter a number or ID [${CFG[BOARD]}]: " answer
        else
            read -r -p "输入编号或 ID [${CFG[BOARD]}]：" answer
        fi
        [[ -n "$answer" ]] || answer="${CFG[BOARD]}"
        if [[ "$answer" =~ ^[0-9]+$ ]] && ((10#$answer >= 1 && 10#$answer <= ${#choices[@]})); then
            MENU_SELECTION="${choices[$((10#$answer - 1))]}"
            return
        fi
        for option in "${choices[@]}"; do
            [[ "$answer" == "$option" || ("$option" == saved:* && "$answer" == "${option#saved:}") ]] && { MENU_SELECTION="$option"; return; }
        done
        [[ "$LANGUAGE" == en ]] && printf '%s\n' 'Invalid selection; please try again.' >&2 || printf '%s\n' '无效选择，请重新输入。' >&2
    done
}

show_interactive_summary() {
    if [[ "$LANGUAGE" == en ]]; then
        printf '\nBuild plan\n  Board: %s\n  MCU: %s\n  Display: %s\n  Input: %s\n  Modules: %s\n  Dashboard UIs: %s\n  Output: firmware-builds/%s/\n' \
            "${CFG[BOARD]}" "${CFG[MCU]}" "${CFG[DISPLAY]}" "${CFG[INPUT]}" "${CFG[MODULES]:-(none)}" "${CFG[DASHBOARDS]}" "${CFG[PROFILE]}"
    else
        printf '\n构建方案\n  开发板：%s\n  MCU：%s\n  显示屏：%s\n  输入设备：%s\n  功能模块：%s\n  仪表界面：%s\n  输出目录：firmware-builds/%s/\n' \
            "${CFG[BOARD]}" "${CFG[MCU]}" "${CFG[DISPLAY]}" "${CFG[INPUT]}" "${CFG[MODULES]:-（无）}" "${CFG[DASHBOARDS]}" "${CFG[PROFILE]}"
    fi
}

confirm_interactive_plan() {
    local answer
    if [[ "$LANGUAGE" == en ]]; then
        read -r -p 'Create this configuration? [Y/n]: ' answer
    else
        read -r -p '确认生成此配置？[Y/n]：' answer
    fi
    [[ -z "$answer" || "$answer" =~ ^[Yy]$ ]]
}

select_post_config_action() {
    local answer
    while true; do
        if [[ "$LANGUAGE" == en ]]; then
            printf '\n%s\n' 'Next step'
            printf '%s\n' '  1) Build locally'
            printf '%s\n' '  2) Prepare an online build request'
            printf '%s\n' '  0) Keep the generated configuration and exit'
            if ! read -r -p 'Choose [0]: ' answer; then NEXT_ACTION=config; return; fi
        else
            printf '\n%s\n' '下一步'
            printf '%s\n' '  1) 本地编译'
            printf '%s\n' '  2) 准备在线构建请求'
            printf '%s\n' '  0) 保留生成的配置并退出'
            if ! read -r -p '请选择 [0]：' answer; then NEXT_ACTION=config; return; fi
        fi
        case "$answer" in
            ''|0|config) NEXT_ACTION=config; return ;;
            1|local|build-local) NEXT_ACTION=local; return ;;
            2|cloud|online|build-cloud) NEXT_ACTION=cloud; return ;;
            *) [[ "$LANGUAGE" == en ]] && printf '%s\n' 'Invalid next step; please try again.' >&2 || printf '%s\n' '无效的下一步选择，请重新输入。' >&2 ;;
        esac
    done
}

run_interactive() {
    local -a choices=()
    choose_interactive_language
    set_defaults
    print_interactive_title
    choose_board_or_saved_profile
    if [[ "$MENU_SELECTION" == saved:* ]]; then
        local saved_profile="${MENU_SELECTION#saved:}"
        set_defaults
        load_user_config "$BUILD_ROOT/$saved_profile/firmware.env"
        validate_config
        show_interactive_summary
        exec env FIRMWARE_LANG="$LANGUAGE" "$ROOT/scripts/build-profile.sh" --config "$BUILD_ROOT/$saved_profile/firmware.env"
    fi
    CFG[BOARD]="$MENU_SELECTION"
    if [[ "${CFG[BOARD]}" == custom ]]; then
        configure_custom_board
    else
        load_record board "${CFG[BOARD]}"
        CFG[MCU]="${RECORD[MCU]}"
        CFG[DISPLAY_BUS]="${RECORD[DISPLAY_BUS]}"
        CFG[INPUT_BUS]="${RECORD[INPUT_BUS]}"
        mapfile -t choices < <(catalog_ids_matching display BUS "${RECORD[DISPLAY_BUS]}")
        choices+=(custom)
        [[ " ${choices[*]} " == *" ${CFG[DISPLAY]} "* ]] || CFG[DISPLAY]="${choices[0]}"
        choose_catalog_option display 'Display' "${CFG[DISPLAY]}" "${choices[@]}"
        CFG[DISPLAY]="$MENU_SELECTION"
        [[ "${CFG[DISPLAY]}" != custom ]] || prompt_custom_id DISPLAY_NAME '自定义显示屏名称（ASCII）' 'Custom display name (ASCII)'
        mapfile -t choices < <(catalog_ids_matching input BUS "${RECORD[INPUT_BUS]}")
        choices+=(custom)
        [[ " ${choices[*]} " == *" ${CFG[INPUT]} "* ]] || CFG[INPUT]="${choices[0]}"
        choose_catalog_option input 'Input' "${CFG[INPUT]}" "${choices[@]}"
        CFG[INPUT]="$MENU_SELECTION"
        [[ "${CFG[INPUT]}" != custom ]] || prompt_custom_id INPUT_NAME '自定义输入设备名称（ASCII）' 'Custom input name (ASCII)'
        choose_module_options
        CFG[MODULES]="$MENU_SELECTION"
        choose_dashboard_options
        CFG[DASHBOARDS]="$MENU_SELECTION"
        configure_missing_board_gpio
    fi
    set_interactive_profile_name
    validate_config
    show_interactive_summary
    if ! confirm_interactive_plan; then
        [[ "$LANGUAGE" == en ]] && printf '%s\n' 'Configuration canceled.' || printf '%s\n' '已取消生成配置。'
        return
    fi
    select_post_config_action
    case "$NEXT_ACTION" in
        config)
            write_config
            ;;
        local)
            local temporary_build_root original_build_root build_status answer
            mkdir -p "$ROOT/output"
            temporary_build_root="$(mktemp -d "$ROOT/output/.config.XXXXXX")"
            original_build_root="$BUILD_ROOT"
            BUILD_ROOT="$temporary_build_root"
            write_config >/dev/null
            if env FIRMWARE_LANG="$LANGUAGE" FIRMWARE_BUILD_ROOT="$temporary_build_root" "$ROOT/start.sh" compile-local --config "$temporary_build_root/${CFG[PROFILE]}/firmware.env"; then
                build_status=0
            else
                build_status=$?
            fi
            BUILD_ROOT="$original_build_root"
            rm -rf "$temporary_build_root"
            (( build_status == 0 )) || return "$build_status"
            if is_interactive_terminal; then
                if [[ "$LANGUAGE" == en ]]; then
                    read -r -p 'Save this configuration after building? [y/N]: ' answer
                else
                    read -r -p '编译完成后保存此配置吗？[y/N]：' answer
                fi
                if [[ "$answer" =~ ^[Yy]$ ]]; then
                    set_interactive_profile_name prompt
                    write_config
                fi
            fi
            ;;
        cloud)
            write_config
            dispatch_cloud_build "$BUILD_ROOT/${CFG[PROFILE]}/firmware.env"
            return
            ;;
    esac
}

main() {
    filter_language_options "$@"
    set -- "${FILTERED_ARGS[@]}"
    local command="${1:-}"
    [[ -n "$command" ]] || { run_interactive; return $?; }
    shift
    case "$command" in
        install-idf)
            install_idf "$@"
            ;;
        doctor)
            [[ $# -eq 0 ]] || die "doctor does not accept options"
            run_doctor
            ;;
        configure|validate|build-local|build-cloud|compile-local)
            set_defaults
            parse_options "$@"
            validate_config
            if [[ "$command" == validate ]]; then
                printf '%s\n' "$(message_text "valid: profile=${CFG[PROFILE]} modules=${CFG[MODULES]} dashboards=${CFG[DASHBOARDS]}")"
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
                    "$ROOT/scripts/esp-idf-env.sh" -B "$IDF_BUILD_ROOT/${CFG[PROFILE]}/idf-build" \
                    -DIDF_TARGET="${CFG[MCU]}" \
                    -DSDKCONFIG="$BUILD_ROOT/${CFG[PROFILE]}/sdkconfig" \
                    -DSDKCONFIG_DEFAULTS="$BUILD_ROOT/${CFG[PROFILE]}/sdkconfig.defaults" \
                    -DESP_BMS_PROFILE_FILE="$BUILD_ROOT/${CFG[PROFILE]}/generated/profile.cmake" build
                local build_dir="$IDF_BUILD_ROOT/${CFG[PROFILE]}/idf-build" firmware_path output_firmware_path answer
                firmware_path="$build_dir/esp32_bms_gps_idf.bin"
                [[ -f "$firmware_path" ]] || die "build completed but firmware is missing: $firmware_path"
                output_firmware_path="$FIRMWARE_OUTPUT_ROOT/${CFG[PROFILE]}/esp32_bms_gps_idf.bin"
                mkdir -p "${output_firmware_path%/*}"
                cp "$firmware_path" "$output_firmware_path"
                if [[ "$LANGUAGE" == en ]]; then
                    printf 'Build completed\n  Firmware: %s\n' "$output_firmware_path"
                else
                    printf '编译完成\n  固件地址：%s\n' "$output_firmware_path"
                fi
                if is_interactive_terminal; then
                    if [[ "$LANGUAGE" == en ]]; then
                        read -r -p 'Flash this firmware now? [y/N]: ' answer
                    else
                        read -r -p '现在烧录这个固件吗？[y/N]：' answer
                    fi
                    [[ "$answer" =~ ^[Yy]$ ]] && flash_built_firmware "$build_dir"
                fi
            elif [[ "$command" == build-cloud ]]; then
                write_config
                dispatch_cloud_build "$BUILD_ROOT/${CFG[PROFILE]}/firmware.env"
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
