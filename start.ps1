param(
    [Parameter(Position = 0)]
    [string]$Command,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Arguments
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$CatalogDir = if ($env:FIRMWARE_CATALOG_DIR) { $env:FIRMWARE_CATALOG_DIR } else { Join-Path $Root 'firmware/catalog' }
$BuildRoot = if ($env:FIRMWARE_BUILD_ROOT) { $env:FIRMWARE_BUILD_ROOT } else { Join-Path $Root 'firmware-builds' }
$SchemaVersion = '1'
$script:Language = if ($env:FIRMWARE_LANG -in @('zh', 'en')) { $env:FIRMWARE_LANG } else { 'zh' }

function Convert-LocalizedText([string]$Text) {
    if ($script:Language -eq 'en') { return $Text }
    $Translations = [ordered]@{
        'error: ' = '错误：'; 'ok: ' = '正常：'; 'missing: ' = '缺少：'; 'valid: ' = '校验通过：'
        'profile: ' = '配置档：'; 'config: ' = '配置：'; 'normalized: ' = '标准化配置：'
        'previous profile preserved at ' = '已保留先前配置档：'
        'cloud build request prepared; workflow dispatch belongs to ' = '云构建请求已准备；工作流分派属于 '
        'missing ' = '缺少 '; 'unknown ' = '未知 '; 'invalid ' = '无效的 '; 'unsupported ' = '不支持的 '
        'duplicate ' = '重复的 '; 'malformed ' = '格式错误的 '; 'configuration' = '配置'; 'catalog' = '目录'
        'schema' = '模式'; 'record' = '记录'; 'file' = '文件'; 'key' = '键'; 'value' = '值'
        'module' = '模块'; 'capability' = '能力'; 'board' = '开发板'; 'display' = '显示屏'; 'input' = '输入'
        'profile' = '配置档'; 'option' = '选项'; 'command' = '命令'; 'partition' = '分区'; 'path' = '路径'
        'requires' = '需要'; 'conflicts with' = '与 '; 'assigned to both' = '同时分配给 '
        'is unavailable on' = '在以下芯片不可用：'; 'is input-only and cannot drive' = '仅可输入，不能驱动 '
        'is dangerous; pass' = '是危险引脚；请传入 '; 'does not accept options' = '不接受选项'
        'is not build-ready yet' = '尚未具备本地构建条件'; 'Profile' = '配置名称'; 'Board' = '开发板'
        'Display' = '显示屏'; 'Input' = '输入设备'; 'Modules' = '模块'; 'profile=' = '配置档='; 'modules=' = '模块='
    }
    foreach ($Source in $Translations.Keys) { $Text = $Text.Replace($Source, $Translations[$Source]) }
    return $Text
}

function Write-LocalizedOutput([string]$Text) { Write-Output (Convert-LocalizedText $Text) }
function Write-LocalizedError([string]$Text) { Write-Error (Convert-LocalizedText $Text) }
function Fail([string]$Message) { throw (Convert-LocalizedText "error: $Message") }

function Set-Language([string]$Value) {
    if ($Value -notin @('zh', 'en')) { Fail "invalid language: $Value (use zh or en)" }
    $script:Language = $Value
}

function Remove-LanguageOptions([string[]]$Items) {
    $Remaining = [System.Collections.Generic.List[string]]::new()
    for ($Index = 0; $Index -lt $Items.Count; ) {
        if ($Items[$Index] -eq '--lang') {
            if ($Index + 1 -ge $Items.Count) { Fail '--lang requires zh or en' }
            Set-Language $Items[$Index + 1]
            $Index += 2
        } else {
            $Remaining.Add($Items[$Index])
            $Index++
        }
    }
    return $Remaining.ToArray()
}

function Show-Usage {
if ($script:Language -eq 'en') {
@'
Usage: .\start.cmd <command> [options]

ESP32 BMS GPS Firmware Configurator
Build a firmware plan from the bundled hardware and module catalog.

Commands:
  doctor       Check the local ESP-IDF build prerequisites.
  configure    Validate a configuration and generate a profile.
  validate     Validate a configuration without writing a profile.
  build-local  Generate a profile, then build it in an isolated directory.
  build-cloud  Validate and prepare a cloud-build request; it never pushes.

Options: --lang zh|en --config FILE --profile ID --mcu ID --board ID --display ID --input ID
         --modules ID[,ID...] --gpio ROLE=PIN --confirm-dangerous-gpio

Run without arguments to choose a language, then configure interactively.
'@ | Write-Output
return
}
@'
用法：.\start.cmd <命令> [选项]

ESP32 BMS GPS 固件定制器
从内置硬件和功能目录选择方案，生成定制固件配置。

命令：
  doctor       检查本地 ESP-IDF 构建前置条件。
  configure    校验配置并生成配置档。
  validate     只校验配置，不写入配置档。
  build-local  生成配置档，并在隔离目录中构建。
  build-cloud  校验并准备云构建请求；不会推送。

选项：--lang zh|en --config FILE --profile ID --mcu ID --board ID --display ID --input ID
       --modules ID[,ID...] --gpio ROLE=PIN --confirm-dangerous-gpio

无参数运行时会先选择语言，再进入交互式配置。
'@ | Write-Output
}

function Test-Id([string]$Value) { return $Value -match '^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$' }
function Test-Value([string]$Value) { return $Value -match '^[A-Za-z0-9,._:/-]*$' }
function Test-Pin([string]$Value) { return $Value -match '^[0-9]{1,2}$' }

function Read-KeyValue([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { Fail "missing file: $Path" }
    $Map = @{}
    foreach ($RawLine in [System.IO.File]::ReadAllLines($Path)) {
        $Line = $RawLine.TrimEnd("`r")
        if ($Line.Length -eq 0 -or $Line.StartsWith('#')) { continue }
        if ($Line -notmatch '^(?<key>[A-Z][A-Z0-9_]*)=(?<value>.*)$') { Fail "malformed KEY=VALUE record in $Path" }
        $Key = $Matches.key
        $Value = $Matches.value
        if (-not (Test-Value $Value)) { Fail "invalid value for $Key in $Path" }
        if ($Map.ContainsKey($Key)) { Fail "duplicate key $Key in $Path" }
        $Map[$Key] = $Value
    }
    return $Map
}

function Assert-Keys([hashtable]$Map, [string[]]$Allowed) {
    foreach ($Key in $Map.Keys) {
        if ($Allowed -notcontains $Key) { Fail "unknown key $Key" }
    }
}

function Require-Key([hashtable]$Map, [string]$Key) {
    if (-not $Map.ContainsKey($Key)) { Fail "missing key $Key" }
}

function Get-Record([string]$Kind, [string]$Id) {
    if (-not (Test-Id $Id)) { Fail "invalid $Kind identifier: $Id" }
    $Record = Read-KeyValue (Join-Path $CatalogDir "$Kind/$Id.env")
    Require-Key $Record 'SCHEMA_VERSION'
    Require-Key $Record 'ID'
    if ($Record.SCHEMA_VERSION -ne $SchemaVersion) { Fail "unsupported catalog schema for $Id" }
    if ($Record.ID -ne $Id) { Fail "catalog id mismatch for $Id" }
    return $Record
}

function Split-Csv([string]$Value) {
    if ([string]::IsNullOrEmpty($Value)) { return @() }
    return @($Value.Split(',') | Where-Object { $_.Length -gt 0 })
}

function Test-CsvContains([string]$List, [string]$Value) { return (Split-Csv $List) -contains $Value }
function ConvertTo-SortedCsv([string[]]$Values) { return (($Values | Where-Object { $_.Length -gt 0 } | Sort-Object -Unique) -join ',') }

function New-DefaultConfig {
    return [ordered]@{
        SCHEMA_VERSION = $SchemaVersion
        PROFILE = 'legacy'
        MCU = 'esp32'
        BOARD = 'esp32-wroom-32e-legacy'
        DISPLAY = 'st7789-spi'
        INPUT = 'xpt2046-spi'
        MODULES = 'bms,gps,controller,audio,network,ota,cast'
        CONFIRM_DANGEROUS_GPIO = 'NO'
    }
}

function Import-UserConfig([hashtable]$Config, [string]$Path) {
    $Input = Read-KeyValue $Path
    Require-Key $Input 'SCHEMA_VERSION'
    if ($Input.SCHEMA_VERSION -ne $SchemaVersion) { Fail 'unsupported configuration schema' }
    foreach ($Key in $Input.Keys) {
        if ($Key -in @('SCHEMA_VERSION', 'PROFILE', 'MCU', 'BOARD', 'DISPLAY', 'INPUT', 'MODULES', 'CONFIRM_DANGEROUS_GPIO') -or $Key -match '^GPIO_[A-Z][A-Z0-9_]*$') {
            $Config[$Key] = $Input[$Key]
        } else {
            Fail "unknown configuration key $Key"
        }
    }
}

function Parse-Options([hashtable]$Config, [string[]]$Items) {
    for ($Index = 0; $Index -lt $Items.Count; ) {
        $Option = $Items[$Index]
        switch ($Option) {
            '--config' {
                if ($Index + 1 -ge $Items.Count) { Fail '--config requires a file' }
                Import-UserConfig $Config $Items[$Index + 1]
                $Index += 2
            }
            '--profile' { if ($Index + 1 -ge $Items.Count) { Fail '--profile requires a value' }; $Config.PROFILE = $Items[$Index + 1]; $Index += 2 }
            '--mcu' { if ($Index + 1 -ge $Items.Count) { Fail '--mcu requires a value' }; $Config.MCU = $Items[$Index + 1]; $Index += 2 }
            '--board' { if ($Index + 1 -ge $Items.Count) { Fail '--board requires a value' }; $Config.BOARD = $Items[$Index + 1]; $Index += 2 }
            '--display' { if ($Index + 1 -ge $Items.Count) { Fail '--display requires a value' }; $Config.DISPLAY = $Items[$Index + 1]; $Index += 2 }
            '--input' { if ($Index + 1 -ge $Items.Count) { Fail '--input requires a value' }; $Config.INPUT = $Items[$Index + 1]; $Index += 2 }
            '--modules' { if ($Index + 1 -ge $Items.Count) { Fail '--modules requires a value' }; $Config.MODULES = $Items[$Index + 1]; $Index += 2 }
            '--gpio' {
                if ($Index + 1 -ge $Items.Count -or $Items[$Index + 1] -notmatch '^(?<role>[A-Z][A-Z0-9_]*)=(?<pin>[0-9]{1,2})$') { Fail '--gpio requires ROLE=PIN' }
                $Config["GPIO_$($Matches.role)"] = $Matches.pin
                $Index += 2
            }
            '--confirm-dangerous-gpio' { $Config.CONFIRM_DANGEROUS_GPIO = 'YES'; $Index++ }
            '-h' { Show-Usage; exit 0 }
            '--help' { Show-Usage; exit 0 }
            default { Fail "unknown option: $Option" }
        }
    }
}

function Visit-Module([string]$Module) {
    if (-not (Test-Id $Module)) { Fail "invalid module id: $Module" }
    if ($script:ModuleState[$Module] -eq 'done') { return }
    if ($script:ModuleState[$Module] -eq 'visiting') { Fail "module dependency cycle at $Module" }
    $script:ModuleState[$Module] = 'visiting'
    $Record = Get-Record 'module' $Module
    Assert-Keys $Record @('SCHEMA_VERSION', 'ID', 'REQUIRES_CAPABILITIES', 'REQUIRES_MODULES', 'CONFLICTS', 'COMPONENTS')
    foreach ($Capability in Split-Csv $Record.REQUIRES_CAPABILITIES) {
        if (-not (Test-CsvContains $script:MCUCapabilities $Capability)) { Fail "$Module requires capability $Capability" }
    }
    foreach ($Dependency in Split-Csv $Record.REQUIRES_MODULES) { Visit-Module $Dependency }
    foreach ($Conflict in Split-Csv $Record.CONFLICTS) {
        if ($script:ModuleState.ContainsKey($Conflict)) { Fail "$Module conflicts with $Conflict" }
    }
    $script:ModuleState[$Module] = 'done'
    $script:SelectedModules += $Module
}

function Load-GpioList([hashtable]$Values, [hashtable]$Kinds, [hashtable]$Defaults, [string]$Kind, [string]$List) {
    foreach ($Pair in Split-Csv $List) {
        if ($Pair -notmatch '^(?<role>[A-Z][A-Z0-9_]*)\:(?<pin>[0-9]{1,2})$') { Fail "invalid board GPIO entry $Pair" }
        if ($Values.ContainsKey($Matches.role)) { Fail "duplicate board GPIO role $($Matches.role)" }
        $Values[$Matches.role] = $Matches.pin
        $Defaults[$Matches.role] = $Matches.pin
        $Kinds[$Matches.role] = $Kind
    }
}

function Validate-Config([hashtable]$Config) {
    foreach ($Key in @('PROFILE', 'MCU', 'BOARD', 'DISPLAY', 'INPUT')) { if (-not (Test-Id $Config[$Key])) { Fail "invalid $Key" } }
    if ($Config.CONFIRM_DANGEROUS_GPIO -notin @('YES', 'NO')) { Fail 'CONFIRM_DANGEROUS_GPIO must be YES or NO' }
    $Schema = Read-KeyValue (Join-Path $CatalogDir 'schema.env')
    if ($Schema.SCHEMA_VERSION -ne $SchemaVersion) { Fail 'unsupported catalog schema' }

    $Mcu = Get-Record 'mcu' $Config.MCU
    Assert-Keys $Mcu @('SCHEMA_VERSION', 'ID', 'CAPABILITIES', 'DISPLAY_BUSES', 'GPIO_MAX', 'INPUT_ONLY', 'DANGEROUS_GPIO')
    $script:MCUCapabilities = $Mcu.CAPABILITIES
    $script:MCUDisplayBuses = $Mcu.DISPLAY_BUSES
    $script:MCUGpioMax = [int]$Mcu.GPIO_MAX
    $script:MCUInputOnly = $Mcu.INPUT_ONLY
    $script:MCUDangerous = $Mcu.DANGEROUS_GPIO

    $Board = Get-Record 'board' $Config.BOARD
    Assert-Keys $Board @('SCHEMA_VERSION', 'ID', 'MCU', 'DISPLAY_BUS', 'INPUT_BUS', 'FLASH_MB', 'PSRAM_MB', 'PARTITIONS', 'BUILD_READY', 'INPUT_GPIO', 'OUTPUT_GPIO', 'APPROVED_DANGEROUS_GPIO')
    if ($Board.MCU -ne $Config.MCU) { Fail "board $($Config.BOARD) requires $($Board.MCU)" }
    $script:BoardDisplayBus = $Board.DISPLAY_BUS
    $script:BoardInputBus = $Board.INPUT_BUS
    $script:BoardPartitions = $Board.PARTITIONS
    $script:BoardBuildReady = $Board.BUILD_READY
    if ($Board.PARTITIONS -ne 'partitions.csv' -and -not $Board.PARTITIONS.StartsWith('firmware/partitions/')) { Fail "unsupported partition path: $($Board.PARTITIONS)" }
    if ($Board.PARTITIONS.Contains('..')) { Fail 'partition path traversal is not allowed' }
    if (-not (Test-Path -LiteralPath (Join-Path $Root $Board.PARTITIONS) -PathType Leaf)) { Fail "board partition file is missing: $($Board.PARTITIONS)" }

    $Display = Get-Record 'display' $Config.DISPLAY
    Assert-Keys $Display @('SCHEMA_VERSION', 'ID', 'BUS')
    if ($Display.BUS -ne $Board.DISPLAY_BUS -or -not (Test-CsvContains $Mcu.DISPLAY_BUSES $Display.BUS)) { Fail 'display bus is incompatible' }
    $Input = Get-Record 'input' $Config.INPUT
    Assert-Keys $Input @('SCHEMA_VERSION', 'ID', 'BUS')
    if ($Input.BUS -ne $Board.INPUT_BUS) { Fail 'input bus is incompatible' }

    $script:ModuleState = @{}
    $script:SelectedModules = @()
    foreach ($Module in Split-Csv $Config.MODULES) { Visit-Module $Module }
    $Config.MODULES = ConvertTo-SortedCsv $script:SelectedModules

    $script:GpioValues = @{}
    $script:GpioKinds = @{}
    $script:BoardGpio = @{}
    Load-GpioList $script:GpioValues $script:GpioKinds $script:BoardGpio 'input' $Board.INPUT_GPIO
    Load-GpioList $script:GpioValues $script:GpioKinds $script:BoardGpio 'output' $Board.OUTPUT_GPIO
    foreach ($Key in @($Config.Keys)) {
        if ($Key -like 'GPIO_*') {
            $Role = $Key.Substring(5)
            if (-not $script:GpioValues.ContainsKey($Role)) { Fail "GPIO override names an unknown board role: $Role" }
            $script:GpioValues[$Role] = $Config[$Key]
        }
    }
    foreach ($Role in $script:GpioValues.Keys) {
        $Pin = [int]$script:GpioValues[$Role]
        if ($Pin -gt $script:MCUGpioMax) { Fail "GPIO $Pin for $Role is unavailable on $($Config.MCU)" }
        if ($script:GpioKinds[$Role] -eq 'output' -and (Test-CsvContains $script:MCUInputOnly "$Pin")) { Fail "GPIO $Pin is input-only and cannot drive $Role" }
        if ((Test-CsvContains $script:MCUDangerous "$Pin") -and $script:BoardGpio[$Role] -ne "$Pin" -and $Config.CONFIRM_DANGEROUS_GPIO -ne 'YES') { Fail "GPIO $Pin for $Role is dangerous; pass --confirm-dangerous-gpio" }
        foreach ($OtherRole in $script:GpioValues.Keys) {
            if ($OtherRole -ne $Role -and $script:GpioValues[$OtherRole] -eq "$Pin") { Fail "GPIO $Pin is assigned to both $Role and $OtherRole" }
        }
    }
}

function Write-Utf8NoBom([string]$Path, [string]$Content) {
    [System.IO.File]::WriteAllText($Path, $Content, (New-Object System.Text.UTF8Encoding($false)))
}

function Write-Profile([hashtable]$Config) {
    New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null
    $Profile = $Config.PROFILE
    $Temp = Join-Path $BuildRoot ".${Profile}.tmp.$([guid]::NewGuid().ToString('N'))"
    $ProfileDir = Join-Path $BuildRoot $Profile
    New-Item -ItemType Directory -Path (Join-Path $Temp 'generated') -Force | Out-Null
    $Lines = @("SCHEMA_VERSION=$SchemaVersion", "PROFILE=$Profile", "MCU=$($Config.MCU)", "BOARD=$($Config.BOARD)", "DISPLAY=$($Config.DISPLAY)", "INPUT=$($Config.INPUT)", "MODULES=$($Config.MODULES)", "CONFIRM_DANGEROUS_GPIO=$($Config.CONFIRM_DANGEROUS_GPIO)")
    foreach ($Role in ($script:GpioValues.Keys | Sort-Object)) { $Lines += "GPIO_$Role=$($script:GpioValues[$Role])" }
    Write-Utf8NoBom (Join-Path $Temp 'normalized.env') (($Lines -join "`n") + "`n")
    $MainRequires = @('esp_bms_idf_runtime', 'esp_bms_lvgl_bridge', 'esp_bms_lvgl_ui', 'lvgl', 'esp_lvgl_adapter')
    $AudioFeature = 0
    $BmsFeature = 0
    $ControllerFeature = 0
    $GpsFeature = 0
    $NetworkFeature = 0
    $OtaFeature = 0
    $Trimming = 'audio-component-excluded;legacy-runtime-untrimmed'
    if ((Split-Csv $Config.MODULES) -contains 'gps') {
        $MainRequires = @('esp_bms_gps') + $MainRequires
        $GpsFeature = 1
        $Trimming = 'gps-component-enabled;legacy-runtime-partially-untrimmed'
    }
    if ((Split-Csv $Config.MODULES) -contains 'audio') {
        $MainRequires = @('esp_bms_audio_feedback') + $MainRequires
        $AudioFeature = 1
        $Trimming = 'audio-enabled;legacy-runtime-untrimmed'
    }
    if ((Split-Csv $Config.MODULES) -contains 'bms') {
        $MainRequires = @('esp_bms_bms_ble') + $MainRequires
        $BmsFeature = 1
        $Trimming = 'bms-component-enabled;legacy-runtime-partially-untrimmed'
    }
    if ((Split-Csv $Config.MODULES) -contains 'controller') {
        $MainRequires = @('esp_bms_controller_ble') + $MainRequires
        $ControllerFeature = 1
        $Trimming = 'controller-component-enabled;legacy-runtime-partially-untrimmed'
    }
    if ((Split-Csv $Config.MODULES) -contains 'network') {
        $MainRequires = @('esp_bms_network') + $MainRequires
        $NetworkFeature = 1
        $Trimming = 'network-component-enabled;legacy-runtime-partially-untrimmed'
    }
    if ((Split-Csv $Config.MODULES) -contains 'ota') {
        $MainRequires = @('esp_bms_ota') + $MainRequires
        $OtaFeature = 1
        $Trimming = 'ota-component-enabled;legacy-runtime-partially-untrimmed'
    }
    $Cmake = @(
        "set(ESP_BMS_PROFILE_ID `"$Profile`")"
        "set(ESP_BMS_SELECTED_MODULES `"$($Config.MODULES)`")"
        'set(ESP_BMS_PROFILE_TRIMMING_READY FALSE)'
        "set(ESP_BMS_FEATURE_AUDIO $AudioFeature CACHE BOOL `"Firmware profile audio feature`" FORCE)"
        "set(ESP_BMS_FEATURE_BMS $BmsFeature CACHE BOOL `"Firmware profile BMS feature`" FORCE)"
        "set(ESP_BMS_FEATURE_CONTROLLER $ControllerFeature CACHE BOOL `"Firmware profile controller feature`" FORCE)"
        "set(ESP_BMS_FEATURE_GPS $GpsFeature CACHE BOOL `"Firmware profile GPS feature`" FORCE)"
        "set(ESP_BMS_FEATURE_NETWORK $NetworkFeature CACHE BOOL `"Firmware profile network feature`" FORCE)"
        "set(ESP_BMS_FEATURE_OTA $OtaFeature CACHE BOOL `"Firmware profile OTA feature`" FORCE)"
        "set(ESP_BMS_PROFILE_MAIN_REQUIRES `"$($MainRequires -join ';')`" CACHE STRING `"Firmware profile component closure`" FORCE)"
    )
    Write-Utf8NoBom (Join-Path $Temp 'generated/profile.cmake') (($Cmake -join "`n") + "`n")
    $ModuleLines = @("MODULES=$($Config.MODULES)")
    foreach ($Module in ($script:SelectedModules | Sort-Object -Unique)) {
        $Record = Get-Record 'module' $Module
        $ModuleLines += "MODULE_$Module`_COMPONENTS=$($Record.COMPONENTS)"
    }
    Write-Utf8NoBom (Join-Path $Temp 'generated/modules.env') (($ModuleLines -join "`n") + "`n")
    Copy-Item -LiteralPath (Join-Path $Root 'sdkconfig.defaults') -Destination (Join-Path $Temp 'sdkconfig.defaults')
    Copy-Item -LiteralPath (Join-Path $Root $script:BoardPartitions) -Destination (Join-Path $Temp 'partitions.csv')
    $Report = @("PROFILE=$Profile", "MCU=$($Config.MCU)", "BOARD=$($Config.BOARD)", "BUILD_READY=$script:BoardBuildReady", "MODULES=$($Config.MODULES)", "TRIMMING=$Trimming", 'NOTE=Generated selection will become the component closure after runtime extraction.')
    Write-Utf8NoBom (Join-Path $Temp 'report.txt') (($Report -join "`n") + "`n")
    if (Test-Path -LiteralPath $ProfileDir) {
        $Backup = Join-Path $BuildRoot ".${Profile}.previous.$([DateTimeOffset]::UtcNow.ToUnixTimeSeconds())"
        Move-Item -LiteralPath $ProfileDir -Destination $Backup
        Write-LocalizedError "previous profile preserved at $($Backup.Substring($Root.Length + 1))"
    }
    Move-Item -LiteralPath $Temp -Destination $ProfileDir
    Write-LocalizedOutput "profile: $($ProfileDir.Substring($Root.Length + 1))"
    Write-LocalizedOutput "normalized: $($ProfileDir.Substring($Root.Length + 1))/normalized.env"
}

function Invoke-Doctor {
    $Missing = $false
    foreach ($Name in @('git', 'cmake', 'python')) {
        $CommandInfo = Get-Command $Name -ErrorAction SilentlyContinue
        if ($null -eq $CommandInfo) { Write-LocalizedError "missing: $Name"; $Missing = $true } else { Write-LocalizedOutput "ok: $Name=$($CommandInfo.Source)" }
    }
    $Ninja = Get-Command ninja -ErrorAction SilentlyContinue
    if ($null -eq $Ninja) {
        $NinjaRoot = Join-Path $env:USERPROFILE '.espressif/tools/ninja'
        if (Test-Path -LiteralPath $NinjaRoot) { $Ninja = Get-ChildItem -LiteralPath $NinjaRoot -Filter ninja.exe -File -Recurse | Select-Object -First 1 }
    }
    if ($null -eq $Ninja) {
        Write-LocalizedError 'missing: ninja'
        $Missing = $true
    } elseif ($Ninja -is [System.Management.Automation.CommandInfo]) {
        Write-LocalizedOutput "ok: ninja=$($Ninja.Source)"
    } else {
        Write-LocalizedOutput "ok: ninja=$($Ninja.FullName)"
    }
    $IdfExport = if ($env:IDF_PATH) { Join-Path $env:IDF_PATH 'export.ps1' } else { Join-Path $env:USERPROFILE 'esp/esp-idf-v5.5.4/export.ps1' }
    if (Test-Path -LiteralPath $IdfExport) { Write-LocalizedOutput 'ok: ESP-IDF export script' } else { Write-LocalizedError 'missing: ESP-IDF 5.5.4 export.ps1'; $Missing = $true }
    if ($Missing) { exit 1 }
}

function Select-InteractiveLanguage {
    Write-Output '=== ESP32 BMS GPS Firmware Configurator / ESP32 BMS GPS 固件定制器 ==='
    while ($true) {
        Write-Output '请选择语言 / Select language'
        Write-Output '  1) 简体中文'
        Write-Output '  2) English'
        $Answer = Read-Host '输入 1 或 2 / Enter 1 or 2'
        switch ($Answer) {
            { $_ -in @('1', 'zh', 'ZH') } { $script:Language = 'zh'; return }
            { $_ -in @('2', 'en', 'EN') } { $script:Language = 'en'; return }
            default { [Console]::Error.WriteLine('请输入 1、2、zh 或 en。 / Enter 1, 2, zh, or en.') }
        }
    }
}

function Show-InteractiveTitle {
    if ($script:Language -eq 'en') {
        Write-Host ''
        Write-Host '========================================'
        Write-Host ' ESP32 BMS GPS Firmware Configurator'
        Write-Host ' Choose hardware and optional features to create a firmware plan.'
        Write-Host '========================================'
    } else {
        Write-Host ''
        Write-Host '========================================'
        Write-Host ' ESP32 BMS GPS 固件定制器'
        Write-Host ' 选择硬件与可选功能，生成固件构建方案。'
        Write-Host '========================================'
    }
}

function Get-CatalogIds([string]$Kind) {
    $Directory = Join-Path $CatalogDir $Kind
    if (-not (Test-Path -LiteralPath $Directory -PathType Container)) { return @() }
    return @(
        Get-ChildItem -LiteralPath $Directory -Filter '*.env' -File |
            ForEach-Object { $_.BaseName } |
            Where-Object { Test-Id $_ } |
            Sort-Object -Unique
    )
}

function Get-CatalogIdsMatching([string]$Kind, [string]$Key, [string]$Expected) {
    $Result = [System.Collections.Generic.List[string]]::new()
    foreach ($Id in (Get-CatalogIds $Kind)) {
        $Record = Get-Record $Kind $Id
        if ($Record[$Key] -eq $Expected) { $Result.Add($Id) }
    }
    return @($Result | Sort-Object -Unique)
}

function Get-CatalogOptionDescription([string]$Kind, [string]$Id) {
    $Labels = @{
        'board/esp32-wroom-32e-legacy' = @('ESP32-WROOM-32E，4MB Flash，可本地构建（推荐）', 'ESP32-WROOM-32E, 4MB Flash, build-ready (recommended)')
        'board/esp32s3-wroom-1-n16r8-i80' = @('ESP32-S3-WROOM-1，16MB Flash / 8MB PSRAM，尚未适配本地构建', 'ESP32-S3-WROOM-1, 16MB Flash / 8MB PSRAM, local build not ready')
        'display/st7789-spi' = @('ST7789 SPI 显示屏', 'ST7789 SPI display')
        'display/ili9488-i80' = @('ILI9488 8080 并行显示屏', 'ILI9488 I80 parallel display')
        'input/xpt2046-spi' = @('XPT2046 SPI 触摸屏', 'XPT2046 SPI touch input')
        'input/ft6336u-i2c' = @('FT6336U I2C 触摸屏', 'FT6336U I2C touch input')
        'module/bms' = @('BMS 蓝牙连接', 'BMS Bluetooth connection')
        'module/gps' = @('GPS 定位与测速', 'GPS positioning and speed')
        'module/controller' = @('控制器蓝牙', 'Controller Bluetooth')
        'module/audio' = @('音频提示', 'Audio feedback')
        'module/network' = @('Wi-Fi、设置热点与本地网页', 'Wi-Fi, setup AP, and local web UI')
        'module/ota' = @('本地 Web OTA 更新（自动需要 network）', 'Local web OTA update (automatically requires network)')
        'module/cast' = @('手机投屏（当前仍使用 legacy runtime）', 'Phone casting (currently uses legacy runtime)')
    }
    $Label = $Labels["$Kind/$Id"]
    if ($null -eq $Label) { return $(if ($script:Language -eq 'en') { 'Catalog option' } else { '目录中的可选项' }) }
    if ($script:Language -eq 'en') { return $Label[1] }
    return $Label[0]
}

function Select-CatalogOption([string]$Kind, [string]$Title, [string]$Default, [string[]]$Options) {
    if ($Options.Count -eq 0) { Fail "no compatible $Kind catalog options" }
    while ($true) {
        Write-Host ''
        Write-Host (Convert-LocalizedText $Title)
        for ($Index = 0; $Index -lt $Options.Count; $Index++) {
            $Option = $Options[$Index]
            $Mark = if ($Option -eq $Default) { ' *' } else { '' }
            Write-Host ("  {0}) {1} — {2}{3}" -f ($Index + 1), $Option, (Get-CatalogOptionDescription $Kind $Option), $Mark)
        }
        $Prompt = if ($script:Language -eq 'en') { "Enter a number or ID [$Default]" } else { "输入编号或 ID [$Default]" }
        $Answer = Read-Host $Prompt
        if ([string]::IsNullOrWhiteSpace($Answer)) { $Answer = $Default }
        $Number = 0
        if ([int]::TryParse($Answer, [ref]$Number) -and $Number -ge 1 -and $Number -le $Options.Count) { return $Options[$Number - 1] }
        if ($Options -contains $Answer) { return $Answer }
        [Console]::Error.WriteLine($(if ($script:Language -eq 'en') { 'Invalid selection; please try again.' } else { '无效选择，请重新输入。' }))
    }
}

function Select-ModuleOptions([string]$Default) {
    $Options = @(Get-CatalogIds 'module')
    while ($true) {
        Write-Host ''
        Write-Host (Convert-LocalizedText 'Modules')
        Write-Host $(if ($script:Language -eq 'en') { '  0) No optional modules' } else { '  0) 不启用可选功能' })
        for ($Index = 0; $Index -lt $Options.Count; $Index++) {
            $Option = $Options[$Index]
            $Mark = if (",$Default," -like "*,$Option,*") { ' *' } else { '' }
            Write-Host ("  {0}) {1} — {2}{3}" -f ($Index + 1), $Option, (Get-CatalogOptionDescription 'module' $Option), $Mark)
        }
        $Prompt = if ($script:Language -eq 'en') { "Enter comma-separated numbers or IDs [$Default]" } else { "输入以逗号分隔的编号或 ID [$Default]" }
        $Answer = Read-Host $Prompt
        if ([string]::IsNullOrWhiteSpace($Answer)) { return $Default }
        if ($Answer.Trim() -eq '0') { return '' }
        $Selected = [System.Collections.Generic.List[string]]::new()
        $Valid = $true
        foreach ($Entry in ($Answer.Split(',') | ForEach-Object { $_.Trim() })) {
            $Number = 0
            if ([int]::TryParse($Entry, [ref]$Number) -and $Number -ge 1 -and $Number -le $Options.Count) {
                $Selected.Add($Options[$Number - 1])
            } elseif ($Options -contains $Entry) {
                $Selected.Add($Entry)
            } else {
                $Valid = $false
                break
            }
        }
        if ($Valid -and $Selected.Count -gt 0) { return ConvertTo-SortedCsv $Selected.ToArray() }
        [Console]::Error.WriteLine($(if ($script:Language -eq 'en') { 'Invalid module selection; please try again.' } else { '无效功能选择，请重新输入。' }))
    }
}

function Set-InteractiveProfileName([hashtable]$Config) {
    $Source = $Config.BOARD
    if ([string]::IsNullOrEmpty($Source) -or $Source -like 'custom-*') { $Source = $Config.MCU }
    $Config.PROFILE = $Source
}

function Show-InteractiveSummary([hashtable]$Config) {
    $Modules = if ([string]::IsNullOrEmpty($Config.MODULES)) { if ($script:Language -eq 'en') { '(none)' } else { '（无）' } } else { $Config.MODULES }
    if ($script:Language -eq 'en') {
        Write-Host "`nBuild plan"
        Write-Host "  Board: $($Config.BOARD)"
        Write-Host "  MCU: $($Config.MCU)"
        Write-Host "  Display: $($Config.DISPLAY)"
        Write-Host "  Input: $($Config.INPUT)"
        Write-Host "  Modules: $Modules"
        Write-Host "  Output: firmware-builds/$($Config.PROFILE)/"
    } else {
        Write-Host "`n构建方案"
        Write-Host "  开发板：$($Config.BOARD)"
        Write-Host "  MCU：$($Config.MCU)"
        Write-Host "  显示屏：$($Config.DISPLAY)"
        Write-Host "  输入设备：$($Config.INPUT)"
        Write-Host "  功能模块：$Modules"
        Write-Host "  输出目录：firmware-builds/$($Config.PROFILE)/"
    }
}

function Confirm-InteractivePlan {
    $Prompt = if ($script:Language -eq 'en') { 'Create this configuration? [Y/n]' } else { '确认生成此配置？[Y/n]' }
    $Answer = Read-Host $Prompt
    return [string]::IsNullOrEmpty($Answer) -or $Answer -match '^[Yy]$'
}

function Invoke-Interactive {
    $Config = New-DefaultConfig
    Select-InteractiveLanguage
    Show-InteractiveTitle
    $Config.BOARD = Select-CatalogOption 'board' 'Board' $Config.BOARD @(Get-CatalogIds 'board')
    $Board = Get-Record 'board' $Config.BOARD
    $Config.MCU = $Board.MCU
    $DisplayOptions = @(Get-CatalogIdsMatching 'display' 'BUS' $Board.DISPLAY_BUS)
    if ($DisplayOptions -notcontains $Config.DISPLAY) { $Config.DISPLAY = $DisplayOptions[0] }
    $Config.DISPLAY = Select-CatalogOption 'display' 'Display' $Config.DISPLAY $DisplayOptions
    $InputOptions = @(Get-CatalogIdsMatching 'input' 'BUS' $Board.INPUT_BUS)
    if ($InputOptions -notcontains $Config.INPUT) { $Config.INPUT = $InputOptions[0] }
    $Config.INPUT = Select-CatalogOption 'input' 'Input' $Config.INPUT $InputOptions
    $Config.MODULES = Select-ModuleOptions $Config.MODULES
    Set-InteractiveProfileName $Config
    Validate-Config $Config
    Show-InteractiveSummary $Config
    if (-not (Confirm-InteractivePlan)) {
        Write-Host $(if ($script:Language -eq 'en') { 'Configuration canceled.' } else { '已取消生成配置。' })
        return
    }
    Write-Profile $Config
}

try {
    $Arguments = @(Remove-LanguageOptions $Arguments)
    if ([string]::IsNullOrEmpty($Command)) { Invoke-Interactive; exit 0 }
    switch ($Command) {
        'doctor' { if ($Arguments.Count -ne 0) { Fail 'doctor does not accept options' }; Invoke-Doctor; exit 0 }
        'configure' { $Config = New-DefaultConfig; Parse-Options $Config $Arguments; Validate-Config $Config; Write-Profile $Config; exit 0 }
        'validate' { $Config = New-DefaultConfig; Parse-Options $Config $Arguments; Validate-Config $Config; Write-LocalizedOutput "valid: profile=$($Config.PROFILE) modules=$($Config.MODULES)"; exit 0 }
        'build-local' {
            $Config = New-DefaultConfig; Parse-Options $Config $Arguments; Validate-Config $Config; Write-Profile $Config
            if ($script:BoardBuildReady -ne 'YES') { Fail "board $($Config.BOARD) is not build-ready yet" }
            $env:ESP_BMS_PROFILE_FILE = Join-Path $BuildRoot "$($Config.PROFILE)/generated/profile.cmake"
            & (Join-Path $Root 'scripts/esp-idf-env.sh') '-B' (Join-Path $BuildRoot "$($Config.PROFILE)/idf-build") "-DSDKCONFIG=$(Join-Path $BuildRoot "$($Config.PROFILE)/sdkconfig")" "-DSDKCONFIG_DEFAULTS=$(Join-Path $BuildRoot "$($Config.PROFILE)/sdkconfig.defaults")" "-DESP_BMS_PROFILE_FILE=$env:ESP_BMS_PROFILE_FILE" 'build'
            exit $LASTEXITCODE
        }
        'build-cloud' { $Config = New-DefaultConfig; Parse-Options $Config $Arguments; Validate-Config $Config; Write-Profile $Config; Write-LocalizedError 'cloud build request prepared; workflow dispatch belongs to 07-21-build-cloud-verification'; exit 3 }
        'help' { Show-Usage; exit 0 }
        '-h' { Show-Usage; exit 0 }
        '--help' { Show-Usage; exit 0 }
        default { Fail "unknown command: $Command" }
    }
} catch {
    Write-Error $_.Exception.Message
    exit 2
}
