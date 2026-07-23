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
$IdfBuildRoot = if ($env:ESP_BMS_IDF_BUILD_ROOT) {
    $env:ESP_BMS_IDF_BUILD_ROOT
} elseif (-not [string]::IsNullOrWhiteSpace($env:IDF_PATH)) {
    Join-Path (Split-Path -Parent $env:IDF_PATH) 'esp32-bms-gps-idf-builds'
} else {
    Join-Path ([System.IO.Path]::GetTempPath()) 'esp32-bms-gps-idf-builds'
}
$FirmwareOutputRoot = if ($env:FIRMWARE_OUTPUT_ROOT) { $env:FIRMWARE_OUTPUT_ROOT } else { Join-Path $Root 'output' }
$SchemaVersion = '1'
$script:Language = if ($env:FIRMWARE_LANG -in @('zh', 'en')) { $env:FIRMWARE_LANG } else { 'zh' }
$script:BuildExitCode = 0
$script:RequiredIdfVersion = 'ESP-IDF v6.0.2'
. (Join-Path $Root 'scripts/esp-idf-version.ps1')

function Convert-LocalizedText([string]$Text) {
    if ($script:Language -eq 'en') { return $Text }
    $Translations = @(
        'error: |||错误：'; 'ok: |||正常：'; 'missing: |||缺少：'; 'valid: |||校验通过：'
        'profile: |||配置档：'; 'config: |||配置：'; 'normalized: |||标准化配置：'
        'previous profile preserved at |||已保留先前配置档：'
        'cloud build request prepared; workflow dispatch belongs to |||云构建请求已准备；工作流分派属于 '
        'missing |||缺少 '; 'unknown |||未知 '; 'invalid |||无效的 '; 'unsupported |||不支持的 '
        'duplicate |||重复的 '; 'malformed |||格式错误的 '; 'configuration|||配置'; 'catalog|||目录'
        'schema|||模式'; 'record|||记录'; 'file|||文件'; 'key|||键'; 'value|||值'
        'module|||模块'; 'capability|||能力'; 'board|||开发板'; 'display|||显示屏'; 'input|||输入'
        'profile|||配置档'; 'option|||选项'; 'command|||命令'; 'partition|||分区'; 'path|||路径'
        'requires|||需要'; 'conflicts with|||与 '; 'assigned to both|||同时分配给 '
        'is unavailable on|||在以下芯片不可用：'; 'is input-only and cannot drive|||仅可输入，不能驱动 '
        'is dangerous; pass|||是危险引脚；请传入 '; 'does not accept options|||不接受选项'
        'is not build-ready yet|||尚未具备本地构建条件'; 'Profile|||配置名称'; 'Board|||开发板'
        'Display|||显示屏'; 'Input|||输入设备'; 'Modules|||模块'; 'Dashboard UIs|||仪表界面'; 'profile=|||配置档='; 'modules=|||模块='; 'dashboards=|||仪表='
    )
    foreach ($Translation in $Translations) {
        $Source, $Target = $Translation -split '\|\|\|', 2
        $Text = $Text.Replace($Source, $Target)
    }
    return $Text
}

function Write-LocalizedOutput([string]$Text) { Write-Output (Convert-LocalizedText $Text) }
function Write-LocalizedError([string]$Text) { Write-Error (Convert-LocalizedText $Text) }
function Fail([string]$Message) { throw (Convert-LocalizedText "error: $Message") }

function Get-IdfExportScript {
    $IdfRoots = [System.Collections.Generic.List[string]]::new()
    if (-not [string]::IsNullOrWhiteSpace($env:IDF_PATH)) {
        [void]$IdfRoots.Add($env:IDF_PATH)
    }
    foreach ($Scope in @('User', 'Machine')) {
        $PersistedIdfPath = [Environment]::GetEnvironmentVariable('IDF_PATH', $Scope)
        if (-not [string]::IsNullOrWhiteSpace($PersistedIdfPath)) { [void]$IdfRoots.Add($PersistedIdfPath) }
    }

    $RootParent = Split-Path -Parent $Root
    foreach ($Base in @($Root, $RootParent)) {
        [void]$IdfRoots.Add((Join-Path $Base 'esp-idf-v6.0.2'))
    }
    if (-not [string]::IsNullOrWhiteSpace($env:USERPROFILE)) {
        foreach ($RelativePath in @('esp\esp-idf-v6.0.2', 'Desktop\esp-idf-v6.0.2')) {
            [void]$IdfRoots.Add((Join-Path $env:USERPROFILE $RelativePath))
        }
    }
    if (-not [string]::IsNullOrWhiteSpace($env:SystemDrive)) {
        [void]$IdfRoots.Add((Join-Path $env:SystemDrive 'esp\esp-idf-v6.0.2'))
        [void]$IdfRoots.Add((Join-Path $env:SystemDrive 'Espressif\frameworks\esp-idf-v6.0.2'))
    }
    foreach ($Base in @($Root, $RootParent)) {
        [void]$IdfRoots.Add((Join-Path $Base 'esp-idf'))
    }
    if (-not [string]::IsNullOrWhiteSpace($env:USERPROFILE)) {
        foreach ($RelativePath in @('esp\esp-idf', 'Desktop\esp-idf')) {
            [void]$IdfRoots.Add((Join-Path $env:USERPROFILE $RelativePath))
        }
    }
    if (-not [string]::IsNullOrWhiteSpace($env:SystemDrive)) {
        [void]$IdfRoots.Add((Join-Path $env:SystemDrive 'Espressif\frameworks\esp-idf'))
    }
    $IdfCommand = Get-Command idf.py -ErrorAction SilentlyContinue
    if ($null -ne $IdfCommand -and -not [string]::IsNullOrWhiteSpace($IdfCommand.Source) -and (Test-Path -LiteralPath $IdfCommand.Source -PathType Leaf)) {
        $IdfToolsDirectory = Split-Path -Parent $IdfCommand.Source
        if ((Split-Path -Leaf $IdfToolsDirectory) -eq 'tools') { [void]$IdfRoots.Add((Split-Path -Parent $IdfToolsDirectory)) }
    }

    $SearchRoots = [System.Collections.Generic.List[string]]::new()
    foreach ($SearchRoot in @($Root, $RootParent)) { [void]$SearchRoots.Add($SearchRoot) }
    if (-not [string]::IsNullOrWhiteSpace($env:USERPROFILE)) {
        [void]$SearchRoots.Add((Join-Path $env:USERPROFILE 'Desktop'))
        [void]$SearchRoots.Add((Join-Path $env:USERPROFILE 'esp'))
    }
    if (-not [string]::IsNullOrWhiteSpace($env:SystemDrive)) {
        [void]$SearchRoots.Add($env:SystemDrive)
        [void]$SearchRoots.Add((Join-Path $env:SystemDrive 'Espressif\frameworks'))
    }
    foreach ($SearchRoot in ($SearchRoots | Where-Object { Test-Path -LiteralPath $_ -PathType Container } | Select-Object -Unique)) {
        foreach ($Candidate in (Get-ChildItem -LiteralPath $SearchRoot -Directory -Filter 'esp-idf*' -ErrorAction SilentlyContinue)) {
            [void]$IdfRoots.Add($Candidate.FullName)
        }
    }

    foreach ($IdfRoot in ($IdfRoots | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique)) {
        if (-not (Test-EspIdfV602Root $IdfRoot)) { continue }
        $IdfExport = Join-Path $IdfRoot 'export.ps1'
        if (Test-Path -LiteralPath $IdfExport -PathType Leaf) {
            $env:IDF_PATH = [System.IO.Path]::GetFullPath($IdfRoot)
            return $IdfExport
        }
    }
    return $null
}

function Test-IdfExportScript([object]$Path) {
    return ($Path -is [string] -and -not [string]::IsNullOrWhiteSpace([string]$Path) -and (Test-Path -LiteralPath $Path -PathType Leaf))
}

function Ensure-IdfExportScript {
    $IdfExport = [string](Get-IdfExportScript | Select-Object -Last 1)
    if (Test-IdfExportScript $IdfExport) { return ([string]$IdfExport) }
    if (-not (Test-InteractiveTerminal)) { Fail 'missing ESP-IDF v6.0.2 export.ps1; set IDF_PATH or run start.cmd install-idf' }

    $Answer = Read-Host $(if ($script:Language -eq 'en') { 'ESP-IDF v6.0.2 was not found. Install it now? [Y/n]' } else { '未检测到 ESP-IDF v6.0.2，现在安装吗？[Y/n]' })
    if ($Answer -match '^[Nn]$') { Fail 'missing ESP-IDF v6.0.2 export.ps1; run start.cmd install-idf when ready' }
    Install-EspIdf @() | Out-Host
    $IdfExport = [string](Get-IdfExportScript | Select-Object -Last 1)
    if (-not (Test-IdfExportScript $IdfExport)) { Fail 'ESP-IDF installation did not provide export.ps1' }
    return ([string]$IdfExport)
}

function Test-PythonExecutable([string]$Path) {
    try {
        & $Path --version *> $null
        return $?
    } catch {
        return $false
    }
}

function Get-PythonExecutable {
    $Python = Get-Command python -ErrorAction SilentlyContinue
    if ($null -ne $Python) {
        $PythonPath = if ([string]::IsNullOrWhiteSpace($Python.Source)) { $Python.Name } else { $Python.Source }
        if (Test-PythonExecutable $PythonPath) { return $PythonPath }
    }

    $PythonLauncher = Get-Command py -ErrorAction SilentlyContinue
    if ($null -ne $PythonLauncher) {
        $PythonPath = @(& $PythonLauncher.Source -3 -c 'import sys; print(sys.executable)' 2>$null | Select-Object -First 1)
        if ($LASTEXITCODE -eq 0 -and $PythonPath.Count -eq 1 -and (Test-PythonExecutable $PythonPath[0])) { return $PythonPath[0] }
    }

    $PythonEnvRoots = @()
    if (-not [string]::IsNullOrWhiteSpace($env:APPDATA)) { $PythonEnvRoots += (Join-Path $env:APPDATA 'uv/python') }
    if (-not [string]::IsNullOrWhiteSpace($env:USERPROFILE)) { $PythonEnvRoots += (Join-Path $env:USERPROFILE '.espressif/python_env') }
    foreach ($PythonEnvRoot in $PythonEnvRoots) {
        if (Test-Path -LiteralPath $PythonEnvRoot -PathType Container) {
            $Candidates = Get-ChildItem -LiteralPath $PythonEnvRoot -Recurse -Filter python.exe -File -ErrorAction SilentlyContinue |
                Sort-Object FullName -Descending
            foreach ($Candidate in $Candidates) {
                if (Test-PythonExecutable $Candidate.FullName) { return $Candidate.FullName }
            }
        }
    }

    if ($script:Language -eq 'en') { Fail 'missing usable python; install it or run start.cmd install-idf' }
    Fail '缺少可用的 python；请安装 Python 或运行 start.cmd install-idf'
}

function Assert-RequiredIdfVersion {
    $Version = ((& idf.py --version 2>&1) | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $Version -ne $script:RequiredIdfVersion) {
        Fail "unsupported ESP-IDF version: expected $script:RequiredIdfVersion, got $Version"
    }
}

function Test-AsciiInstallDirectory([string]$Path) {
    return $Path -match '^[A-Za-z]:\\[A-Za-z0-9._-]+(?:\\[A-Za-z0-9._-]+)*$'
}

function Get-IdfInstallDirectory([string[]]$Items) {
    $Directory = $null
    for ($Index = 0; $Index -lt $Items.Count; ) {
        if ($Items[$Index] -ne '--dir') { Fail "install-idf does not accept option: $($Items[$Index])" }
        if ($Index + 1 -ge $Items.Count) { Fail '--dir requires a directory' }
        if ($null -ne $Directory) { Fail 'install-idf accepts --dir only once' }
        $Directory = $Items[$Index + 1]
        $Index += 2
    }
    if ([string]::IsNullOrWhiteSpace($Directory)) {
        if (-not (Test-InteractiveTerminal)) { Fail 'install-idf requires --dir outside an interactive terminal' }
        $Default = "$($env:SystemDrive)\esp\esp-idf-v6.0.2"
        $Directory = Read-Host $(if ($script:Language -eq 'en') { "ESP-IDF installation directory [$Default]" } else { "ESP-IDF 安装目录 [$Default]" })
        if ([string]::IsNullOrWhiteSpace($Directory)) { $Directory = $Default }
    }
    $Directory = [System.IO.Path]::GetFullPath($Directory)
    if (-not (Test-AsciiInstallDirectory $Directory)) { Fail 'ESP-IDF installation directory must be an absolute ASCII path without spaces' }
    return $Directory
}

function Install-HostPrerequisites {
    $Missing = @(@('git', 'python') | Where-Object { $null -eq (Get-Command $_ -ErrorAction SilentlyContinue) })
    if ($Missing.Count -eq 0) { return }
    if ($env:OS -ne 'Windows_NT') { Fail 'install-idf on a non-Windows host must use start.sh' }
    if ($null -eq (Get-Command winget -ErrorAction SilentlyContinue)) { Fail 'winget is required to install missing Windows build prerequisites' }
    $PackageIds = @()
    if ($Missing -contains 'git') { $PackageIds += 'Git.Git' }
    if ($Missing -contains 'python') { $PackageIds += 'Python.Python.3.11' }
    Write-Host $(if ($script:Language -eq 'en') { "Installing Windows prerequisites: $($Missing -join ', ')" } else { "正在安装 Windows 编译前置条件：$($Missing -join '、')" })
    foreach ($PackageId in $PackageIds) {
        & winget install --exact --id $PackageId --accept-source-agreements --accept-package-agreements
        if ($LASTEXITCODE -ne 0) { Fail "winget failed to install $PackageId" }
    }
    $MachinePath = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $UserPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = (@($MachinePath, $UserPath, $env:Path) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }) -join ';'
    foreach ($Name in @('git', 'python')) {
        if ($null -eq (Get-Command $Name -ErrorAction SilentlyContinue)) { Fail "missing required command after installation: $Name" }
    }
}

function Install-EspIdf([string[]]$Items) {
    $Directory = Get-IdfInstallDirectory $Items
    if (Test-Path -LiteralPath $Directory) { Fail "installation directory already exists: $Directory" }
    Install-HostPrerequisites
    $CloneParent = Split-Path -Parent $Directory
    $CloneAttempts = 3
    $FailedCloneDirectories = [System.Collections.Generic.List[string]]::new()
    $CloneSucceeded = $false

    for ($Attempt = 1; $Attempt -le $CloneAttempts; $Attempt++) {
        # 在目标目录外克隆，网络中断不会占用最终安装路径。
        $CloneDirectory = Join-Path $CloneParent ".esp32-bms-idf-clone-$([Guid]::NewGuid().ToString('N'))"
        & git clone --branch v6.0.2 --depth 1 --recursive --shallow-submodules https://github.com/espressif/esp-idf.git $CloneDirectory
        if ($LASTEXITCODE -eq 0) {
            Move-Item -LiteralPath $CloneDirectory -Destination $Directory -ErrorAction Stop
            $CloneSucceeded = $true
            break
        }

        if (Test-Path -LiteralPath $CloneDirectory) { [void]$FailedCloneDirectories.Add($CloneDirectory) }
        if ($Attempt -lt $CloneAttempts) {
            Write-Host $(if ($script:Language -eq 'en') { "ESP-IDF clone interrupted; retrying ($($Attempt + 1)/$CloneAttempts)..." } else { "ESP-IDF 克隆连接中断，正在重试（$($Attempt + 1)/$CloneAttempts）..." })
            Start-Sleep -Seconds $Attempt
        }
    }

    if (-not $CloneSucceeded) {
        $FailedPaths = if ($FailedCloneDirectories.Count -gt 0) { $FailedCloneDirectories -join '; ' } else { 'none' }
        if ($script:Language -eq 'en') {
            Fail "failed to clone ESP-IDF v6.0.2 after $CloneAttempts attempts; check the proxy or GitHub route and retry. Partial clones were retained: $FailedPaths"
        }
        Fail "ESP-IDF v6.0.2 克隆在 $CloneAttempts 次尝试后仍失败；请检查代理或 GitHub 网络后重试。未完成的克隆目录已保留：$FailedPaths"
    }
    & (Join-Path $Directory 'install.ps1') esp32 esp32s3
    if ($LASTEXITCODE -ne 0) { Fail 'ESP-IDF tool installation failed' }
    $env:IDF_PATH = $Directory
    [Environment]::SetEnvironmentVariable('IDF_PATH', $Directory, 'User')
    $IdfExport = [string](Get-IdfExportScript | Select-Object -Last 1)
    if (-not (Test-IdfExportScript $IdfExport)) { Fail 'ESP-IDF installation did not provide export.ps1' }
    . ([string]$IdfExport)
    Assert-RequiredIdfVersion
    Write-Host $(if ($script:Language -eq 'en') { "ESP-IDF v6.0.2 installed and IDF_PATH configured: $Directory" } else { "ESP-IDF v6.0.2 已安装，IDF_PATH 已配置：$Directory" })
}

function Set-Language([string]$Value) {
    if ($Value -notin @('zh', 'en')) { Fail "invalid language: $Value (use zh or en)" }
    $script:Language = $Value
}

function Remove-LanguageOptions([string[]]$Items) {
    $Remaining = [System.Collections.Generic.List[string]]::new()
    if ($null -eq $Items) { return $Remaining.ToArray() }
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
  install-idf  Install ESP-IDF v6.0.2 and configure the user environment.
  configure    Validate a configuration and generate a profile.
  validate     Validate a configuration without writing a profile.
  build-local  Generate a profile, then build it in an isolated directory.
  build-cloud  Validate and trigger a cloud build; it never pushes.

Options: --lang zh|en --config FILE --profile ID --mcu ID --board ID --display ID --input ID
         --board-name ID --display-name ID --input-name ID --display-bus SPI|I80 --input-bus SPI|I2C|NONE
         --flash-mb N --psram-mb N --partitions PATH --modules ID[,ID...] --dashboards ID[,ID...] --gpio ROLE=PIN
         --input-gpio ROLE=PIN --output-gpio ROLE=PIN --confirm-dangerous-gpio

install-idf options: --dir DIRECTORY (absolute ASCII path)

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
  install-idf  安装 ESP-IDF v6.0.2 并配置用户环境。
  configure    校验配置并生成配置档。
  validate     只校验配置，不写入配置档。
  build-local  生成配置档，并在隔离目录中构建。
  build-cloud  校验并触发云端构建；不会推送。

选项：--lang zh|en --config FILE --profile ID --mcu ID --board ID --display ID --input ID
       --board-name ID --display-name ID --input-name ID --display-bus SPI|I80 --input-bus SPI|I2C|NONE
       --flash-mb N --psram-mb N --partitions PATH --modules ID[,ID...] --dashboards ID[,ID...] --gpio ROLE=PIN
       --input-gpio ROLE=PIN --output-gpio ROLE=PIN --confirm-dangerous-gpio

install-idf 选项：--dir DIRECTORY（绝对 ASCII 路径）

无参数运行时会先选择语言，再进入交互式配置。
'@ | Write-Output
}

function Test-Id([string]$Value) { return $Value -match '^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$' }
function Test-Value([string]$Value) { return $Value -match '^[A-Za-z0-9,._:/-]*$' }
function Test-Pin([string]$Value) { return $Value -match '^[0-9]{1,2}$' }
function Test-UnsignedInteger([string]$Value) { return $Value -match '^[0-9]+$' }

function Add-GpioDeclaration([System.Collections.IDictionary]$Config, [string]$Key, [string]$Role, [string]$Pin) {
    if ($Role -notmatch '^[A-Z][A-Z0-9_]*$' -or -not (Test-Pin $Pin)) { Fail 'invalid GPIO declaration' }
    $Prefix = if ([string]::IsNullOrEmpty($Config[$Key])) { '' } else { "$($Config[$Key])," }
    $Config[$Key] = "$Prefix$Role`:$Pin"
}

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

function Test-DashboardAvailable([System.Collections.IDictionary]$Config, [string]$Dashboard) {
    $Record = Get-Record 'dashboard' $Dashboard
    Assert-Keys $Record @('SCHEMA_VERSION', 'ID', 'REQUIRES_MODULES', 'REQUIRES_MODULES_ANY')
    foreach ($Module in Split-Csv $Record.REQUIRES_MODULES) {
        if (-not (Test-CsvContains $Config.MODULES $Module)) { return $false }
    }
    $AnyModules = @(Split-Csv $Record.REQUIRES_MODULES_ANY)
    if ($AnyModules.Count -eq 0) { return $true }
    foreach ($Module in $AnyModules) {
        if (Test-CsvContains $Config.MODULES $Module) { return $true }
    }
    return $false
}

function Get-AvailableDashboardIds([System.Collections.IDictionary]$Config) {
    foreach ($Dashboard in @(Get-CatalogIds 'dashboard')) {
        if (Test-DashboardAvailable $Config $Dashboard) { $Dashboard }
    }
}

function Resolve-DashboardSelection([System.Collections.IDictionary]$Config) {
    if ($Config.DASHBOARDS_AUTO -eq 'YES') { $Config.DASHBOARDS = ConvertTo-SortedCsv @(Get-AvailableDashboardIds $Config) }
}

function New-DefaultConfig {
    return [ordered]@{
        SCHEMA_VERSION = $SchemaVersion
        PROFILE = 'esp32s3-n16r8-st7796u-gt1151'
        MCU = 'esp32s3'
        BOARD = 'esp32s3-n16r8-st7796u-gt1151'
        DISPLAY = 'st7796u-i80'
        INPUT = 'gt1151-i2c'
        BOARD_NAME = ''
        DISPLAY_NAME = ''
        INPUT_NAME = ''
        DISPLAY_BUS = ''
        INPUT_BUS = ''
        FLASH_MB = ''
        PSRAM_MB = ''
        PARTITIONS = ''
        INPUT_GPIO = ''
        OUTPUT_GPIO = ''
        MODULES = 'bms,controller,network,ota,cast'
        DASHBOARDS = ''
        DASHBOARDS_AUTO = 'YES'
        CONFIRM_DANGEROUS_GPIO = 'NO'
    }
}

function Import-UserConfig([System.Collections.IDictionary]$Config, [string]$Path) {
    $Input = Read-KeyValue $Path
    Require-Key $Input 'SCHEMA_VERSION'
    if ($Input.SCHEMA_VERSION -ne $SchemaVersion) { Fail 'unsupported configuration schema' }
    foreach ($Key in $Input.Keys) {
        if ($Key -in @('SCHEMA_VERSION', 'PROFILE', 'MCU', 'BOARD', 'DISPLAY', 'INPUT', 'MODULES', 'DASHBOARDS', 'CONFIRM_DANGEROUS_GPIO', 'BOARD_NAME', 'DISPLAY_NAME', 'INPUT_NAME', 'DISPLAY_BUS', 'INPUT_BUS', 'FLASH_MB', 'PSRAM_MB', 'PARTITIONS', 'INPUT_GPIO', 'OUTPUT_GPIO') -or $Key -match '^GPIO_[A-Z][A-Z0-9_]*$') {
            $Config[$Key] = $Input[$Key]
        } else {
            Fail "unknown configuration key $Key"
        }
    }
    if ($Input.Contains('DASHBOARDS')) { $Config.DASHBOARDS_AUTO = 'NO' }
}

function Parse-Options([System.Collections.IDictionary]$Config, [string[]]$Items) {
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
            '--dashboards' { if ($Index + 1 -ge $Items.Count) { Fail '--dashboards requires a value' }; $Config.DASHBOARDS = $Items[$Index + 1]; $Config.DASHBOARDS_AUTO = 'NO'; $Index += 2 }
            '--board-name' { if ($Index + 1 -ge $Items.Count) { Fail '--board-name requires a value' }; $Config.BOARD_NAME = $Items[$Index + 1]; $Index += 2 }
            '--display-name' { if ($Index + 1 -ge $Items.Count) { Fail '--display-name requires a value' }; $Config.DISPLAY_NAME = $Items[$Index + 1]; $Index += 2 }
            '--input-name' { if ($Index + 1 -ge $Items.Count) { Fail '--input-name requires a value' }; $Config.INPUT_NAME = $Items[$Index + 1]; $Index += 2 }
            '--display-bus' { if ($Index + 1 -ge $Items.Count) { Fail '--display-bus requires a value' }; $Config.DISPLAY_BUS = $Items[$Index + 1]; $Index += 2 }
            '--input-bus' { if ($Index + 1 -ge $Items.Count) { Fail '--input-bus requires a value' }; $Config.INPUT_BUS = $Items[$Index + 1]; $Index += 2 }
            '--flash-mb' { if ($Index + 1 -ge $Items.Count) { Fail '--flash-mb requires a value' }; $Config.FLASH_MB = $Items[$Index + 1]; $Index += 2 }
            '--psram-mb' { if ($Index + 1 -ge $Items.Count) { Fail '--psram-mb requires a value' }; $Config.PSRAM_MB = $Items[$Index + 1]; $Index += 2 }
            '--partitions' { if ($Index + 1 -ge $Items.Count) { Fail '--partitions requires a value' }; $Config.PARTITIONS = $Items[$Index + 1]; $Index += 2 }
            '--gpio' {
                if ($Index + 1 -ge $Items.Count -or $Items[$Index + 1] -notmatch '^(?<role>[A-Z][A-Z0-9_]*)=(?<pin>[0-9]{1,2})$') { Fail '--gpio requires ROLE=PIN' }
                $Config["GPIO_$($Matches.role)"] = $Matches.pin
                $Index += 2
            }
            '--input-gpio' {
                if ($Index + 1 -ge $Items.Count -or $Items[$Index + 1] -notmatch '^(?<role>[A-Z][A-Z0-9_]*)=(?<pin>[0-9]{1,2})$') { Fail '--input-gpio requires ROLE=PIN' }
                Add-GpioDeclaration $Config 'INPUT_GPIO' $Matches.role $Matches.pin
                $Index += 2
            }
            '--output-gpio' {
                if ($Index + 1 -ge $Items.Count -or $Items[$Index + 1] -notmatch '^(?<role>[A-Z][A-Z0-9_]*)=(?<pin>[0-9]{1,2})$') { Fail '--output-gpio requires ROLE=PIN' }
                Add-GpioDeclaration $Config 'OUTPUT_GPIO' $Matches.role $Matches.pin
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
    Assert-Keys $Record @('SCHEMA_VERSION', 'ID', 'REQUIRES_CAPABILITIES', 'REQUIRES_MODULES', 'REQUIRES_INPUT_GPIO', 'REQUIRES_OUTPUT_GPIO', 'CONFLICTS', 'COMPONENTS')
    foreach ($Capability in Split-Csv $Record.REQUIRES_CAPABILITIES) {
        if (-not (Test-CsvContains $script:MCUCapabilities $Capability)) { Fail "$Module requires capability $Capability" }
    }
    foreach ($Role in Split-Csv $Record.REQUIRES_INPUT_GPIO) {
        if (-not $script:GpioValues.ContainsKey($Role) -or $script:GpioKinds[$Role] -ne 'input') { Fail "$Module requires input GPIO role $Role" }
    }
    foreach ($Role in Split-Csv $Record.REQUIRES_OUTPUT_GPIO) {
        if (-not $script:GpioValues.ContainsKey($Role) -or $script:GpioKinds[$Role] -ne 'output') { Fail "$Module requires output GPIO role $Role" }
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
        if (-not $script:RequiredGpioKinds.ContainsKey($Matches.role)) { continue }
        if ($script:RequiredGpioKinds[$Matches.role] -ne $Kind) { Fail "board GPIO role $($Matches.role) has the wrong direction" }
        if ($Values.ContainsKey($Matches.role)) { Fail "duplicate board GPIO role $($Matches.role)" }
        $Values[$Matches.role] = $Matches.pin
        $Defaults[$Matches.role] = $Matches.pin
        $Kinds[$Matches.role] = $Kind
    }
}

function Add-RequiredGpioRole([string]$Kind, [string]$Role) {
    if ($Kind -notin @('input', 'output') -or $Role -notmatch '^[A-Z][A-Z0-9_]*$') { Fail "invalid GPIO role $Role" }
    if ($script:RequiredGpioKinds.ContainsKey($Role) -and $script:RequiredGpioKinds[$Role] -ne $Kind) { Fail "GPIO role $Role has incompatible directions" }
    $script:RequiredGpioKinds[$Role] = $Kind
}

function Add-ModuleRequiredGpioRoles([hashtable]$Seen, [string]$Module) {
    if ($Seen.ContainsKey($Module)) { return }
    $Seen[$Module] = $true
    $Record = Get-Record 'module' $Module
    foreach ($Role in Split-Csv $Record.REQUIRES_INPUT_GPIO) { Add-RequiredGpioRole 'input' $Role }
    foreach ($Role in Split-Csv $Record.REQUIRES_OUTPUT_GPIO) { Add-RequiredGpioRole 'output' $Role }
    foreach ($Dependency in Split-Csv $Record.REQUIRES_MODULES) { Add-ModuleRequiredGpioRoles $Seen $Dependency }
}

function Set-RequiredGpioRoles([System.Collections.IDictionary]$Config) {
    $script:RequiredGpioKinds = @{}
    if ($Config.BOARD -eq 'custom') {
        if ((Split-Csv $Config.MODULES) -contains 'audio') { Fail 'custom board audio requires a catalog board hardware profile' }
        foreach ($Requirement in (Get-CustomBoardGpioRequirements $Config)) {
            $Kind, $Role = $Requirement -split ':', 2
            Add-RequiredGpioRole $Kind $Role
        }
        return
    }
    switch ($script:BoardDisplayBus) {
        'SPI' {
            foreach ($Role in @('TFT_MOSI', 'TFT_SCLK', 'TFT_CS', 'TFT_DC')) { Add-RequiredGpioRole 'output' $Role }
            if (Test-BoardDeclaresGpioRole 'TFT_BACKLIGHT' $Board.INPUT_GPIO $Board.OUTPUT_GPIO) {
                Add-RequiredGpioRole 'output' 'TFT_BACKLIGHT'
            }
        }
        'I80' {
            for ($Index = 0; $Index -lt [int]$script:BoardDisplayDataWidth; $Index++) { Add-RequiredGpioRole 'output' "TFT_D$Index" }
            foreach ($Role in @('TFT_WR', 'TFT_CS', 'TFT_DC')) { Add-RequiredGpioRole 'output' $Role }
        }
    }
    if ($Config.INPUT -ne 'none') {
        switch ($script:BoardInputBus) {
            'SPI' {
                Add-RequiredGpioRole 'input' 'TOUCH_MISO'
                foreach ($Role in @('TOUCH_MOSI', 'TOUCH_CS', 'TOUCH_SCLK')) { Add-RequiredGpioRole 'output' $Role }
            }
            'I2C' { foreach ($Role in @('TOUCH_SDA', 'TOUCH_SCL')) { Add-RequiredGpioRole 'output' $Role } }
        }
        if ($Config.INPUT -ne 'custom' -and (Get-Record 'input' $Config.INPUT).USE_IRQ -eq '1') {
            Add-RequiredGpioRole 'input' 'TOUCH_INT'
        }
    }
    $Board = Get-Record 'board' $Config.BOARD
    if (Test-BoardDeclaresGpioRole 'BATTERY_ADC' $Board.INPUT_GPIO $Board.OUTPUT_GPIO) {
        Add-RequiredGpioRole 'input' 'BATTERY_ADC'
    }
    $Seen = @{}
    foreach ($Module in Split-Csv $Config.MODULES) { Add-ModuleRequiredGpioRoles $Seen $Module }
    if ((Split-Csv $Config.MODULES) -contains 'audio') {
        switch ($script:BoardAudioBackend) {
            'DAC' { foreach ($Role in @('AUDIO_DAC', 'AUDIO_ENABLE')) { Add-RequiredGpioRole 'output' $Role } }
            'I2S' { foreach ($Role in @('I2S_BCLK', 'I2S_LRCK', 'I2S_DATA', 'AMP_SHDN')) { Add-RequiredGpioRole 'output' $Role } }
            'NONE' { Fail "board $($Config.BOARD) does not provide an audio hardware profile" }
            default { Fail "unsupported audio backend: $script:BoardAudioBackend" }
        }
    }
}

function Test-BoardDeclaresGpioRole([string]$Role, [string]$InputList, [string]$OutputList) {
    foreach ($Pair in @(Split-Csv $InputList) + @(Split-Csv $OutputList)) {
        if (($Pair -split ':', 2)[0] -eq $Role) { return $true }
    }
    return $false
}

function Add-ModuleGpioRequirements([System.Collections.Generic.List[string]]$Requirements, [hashtable]$Seen, [string]$Module) {
    if ($Seen.ContainsKey($Module)) { return }
    $Seen[$Module] = $true
    $Record = Get-Record 'module' $Module
    foreach ($Role in Split-Csv $Record.REQUIRES_INPUT_GPIO) { $Requirements.Add("input:$Role") }
    foreach ($Role in Split-Csv $Record.REQUIRES_OUTPUT_GPIO) { $Requirements.Add("output:$Role") }
    foreach ($Dependency in Split-Csv $Record.REQUIRES_MODULES) { Add-ModuleGpioRequirements $Requirements $Seen $Dependency }
}

function Get-CustomBoardGpioRequirements([System.Collections.IDictionary]$Config) {
    $Requirements = [System.Collections.Generic.List[string]]::new()
    switch ($script:BoardDisplayBus) {
        'SPI' { foreach ($Role in @('TFT_MOSI', 'TFT_SCLK', 'TFT_CS', 'TFT_DC', 'TFT_BACKLIGHT')) { $Requirements.Add("output:$Role") } }
        'I80' {
            foreach ($Role in @('TFT_D0', 'TFT_D1', 'TFT_D2', 'TFT_D3', 'TFT_D4', 'TFT_D5', 'TFT_D6', 'TFT_D7')) { $Requirements.Add("output:$Role") }
            if ($script:BoardDisplayDataWidth -eq '16') { foreach ($Role in @('TFT_D8', 'TFT_D9', 'TFT_D10', 'TFT_D11', 'TFT_D12', 'TFT_D13', 'TFT_D14', 'TFT_D15')) { $Requirements.Add("output:$Role") } }
            foreach ($Role in @('TFT_WR', 'TFT_RD', 'TFT_CS', 'TFT_DC', 'TFT_RESET', 'TFT_BACKLIGHT')) { $Requirements.Add("output:$Role") }
        }
    }
    switch ($script:BoardInputBus) {
        'SPI' { foreach ($Role in @('TOUCH_IRQ', 'TOUCH_MISO')) { $Requirements.Add("input:$Role") }; foreach ($Role in @('TOUCH_MOSI', 'TOUCH_CS', 'TOUCH_SCLK')) { $Requirements.Add("output:$Role") } }
        'I2C' { $Requirements.Add('input:TOUCH_IRQ'); foreach ($Role in @('TOUCH_SDA', 'TOUCH_SCL')) { $Requirements.Add("output:$Role") } }
    }
    $Seen = @{}
    foreach ($Module in Split-Csv $Config.MODULES) { Add-ModuleGpioRequirements $Requirements $Seen $Module }
    return @($Requirements | Sort-Object -Unique)
}

function Assert-CustomBoardGpioRoles([System.Collections.IDictionary]$Config) {
    foreach ($Requirement in (Get-CustomBoardGpioRequirements $Config)) {
        $Kind, $Role = $Requirement -split ':', 2
        if (-not $script:GpioValues.ContainsKey($Role) -or $script:GpioKinds[$Role] -ne $Kind) { Fail "custom board requires $Kind GPIO role $Role" }
    }
}

function Validate-Config([System.Collections.IDictionary]$Config) {
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

    if ($Config.BOARD -eq 'custom') {
        if (-not (Test-Id $Config.BOARD_NAME)) { Fail 'custom board requires a valid BOARD_NAME' }
        $FlashOk = Test-UnsignedInteger $Config.FLASH_MB
        $PsramOk = Test-UnsignedInteger $Config.PSRAM_MB
        if (-not $FlashOk -or [int]$Config.FLASH_MB -le 0) { Fail 'custom board requires a positive FLASH_MB' }
        if (-not $PsramOk) { Fail 'custom board requires a non-negative PSRAM_MB' }
        $script:BoardDisplayBus = $Config.DISPLAY_BUS
        if ($null -eq $script:BoardDisplayDataWidth) { $script:BoardDisplayDataWidth = '8' }
        $script:BoardInputBus = $Config.INPUT_BUS
        $script:BoardPartitions = $Config.PARTITIONS
        $script:BoardBuildReady = 'NO'
        $script:BoardAudioBackend = 'NONE'
        $script:BoardAudioDacChannel = '0'
        $script:BoardAudioEnableActiveLevel = '0'
        $BoardInputGpio = $Config.INPUT_GPIO
        $BoardOutputGpio = $Config.OUTPUT_GPIO
    } else {
        $Board = Get-Record 'board' $Config.BOARD
        Assert-Keys $Board @('SCHEMA_VERSION', 'ID', 'MCU', 'DISPLAY_BUS', 'DISPLAY_DATA_WIDTH', 'INPUT_BUS', 'FLASH_MB', 'PSRAM_MB', 'PARTITIONS', 'BUILD_READY', 'AUDIO_BACKEND', 'AUDIO_DAC_CHANNEL', 'AUDIO_ENABLE_ACTIVE_LEVEL', 'INPUT_GPIO', 'OUTPUT_GPIO', 'APPROVED_DANGEROUS_GPIO')
        if ($Board.MCU -ne $Config.MCU) { Fail "board $($Config.BOARD) requires $($Board.MCU)" }
        $script:BoardDisplayBus = $Board.DISPLAY_BUS
        $script:BoardDisplayDataWidth = $Board.DISPLAY_DATA_WIDTH
        $script:BoardInputBus = $Board.INPUT_BUS
        $script:BoardPartitions = $Board.PARTITIONS
        $script:BoardBuildReady = $Board.BUILD_READY
        $script:BoardAudioBackend = $Board.AUDIO_BACKEND
        $script:BoardAudioDacChannel = $Board.AUDIO_DAC_CHANNEL
        $script:BoardAudioEnableActiveLevel = $Board.AUDIO_ENABLE_ACTIVE_LEVEL
        $BoardInputGpio = $Board.INPUT_GPIO
        $BoardOutputGpio = $Board.OUTPUT_GPIO
    }
    if ([string]::IsNullOrEmpty($script:BoardDisplayBus)) { Fail 'custom board requires DISPLAY_BUS' }
    if (-not (Test-CsvContains $script:MCUDisplayBuses $script:BoardDisplayBus)) { Fail "display bus $($script:BoardDisplayBus) is unavailable on $($Config.MCU)" }
    if ($script:BoardInputBus -notin @('SPI', 'I2C', 'NONE')) { Fail "unsupported input bus: $($script:BoardInputBus)" }
    if ($script:BoardAudioBackend -eq 'DAC' -and $script:BoardAudioDacChannel -notin @('1', '2')) { Fail 'DAC board requires AUDIO_DAC_CHANNEL 1 or 2' }
    if ($script:BoardAudioBackend -in @('I2S', 'NONE') -and $script:BoardAudioDacChannel -ne '0') { Fail "$script:BoardAudioBackend board requires AUDIO_DAC_CHANNEL 0" }
    if ($script:BoardAudioBackend -notin @('DAC', 'I2S', 'NONE')) { Fail "unsupported audio backend: $script:BoardAudioBackend" }
    if ($script:BoardAudioEnableActiveLevel -notin @('0', '1')) { Fail 'AUDIO_ENABLE_ACTIVE_LEVEL must be 0 or 1' }
    if ($script:BoardPartitions -ne 'partitions.csv' -and -not $script:BoardPartitions.StartsWith('firmware/partitions/')) { Fail "unsupported partition path: $($script:BoardPartitions)" }
    if ($script:BoardPartitions.Contains('..')) { Fail 'partition path traversal is not allowed' }
    if (-not (Test-Path -LiteralPath (Join-Path $Root $script:BoardPartitions) -PathType Leaf)) { Fail "board partition file is missing: $($script:BoardPartitions)" }

    if ($Config.DISPLAY -eq 'custom') {
        if (-not (Test-Id $Config.DISPLAY_NAME)) { Fail 'custom display requires a valid DISPLAY_NAME' }
    } else {
        $Display = Get-Record 'display' $Config.DISPLAY
        Assert-Keys $Display @('SCHEMA_VERSION', 'ID', 'BUS', 'DATA_WIDTH', 'DRIVER', 'WIDTH', 'HEIGHT', 'PIXEL_CLOCK_HZ', 'ROTATION', 'RGB_ORDER', 'INVERT_COLOR', 'SPI_MODE', 'I80_SWAP_COLOR_BYTES', 'I80_PCLK_ACTIVE_NEG', 'I80_PCLK_IDLE_LOW', 'BACKLIGHT_ON_LEVEL', 'POWER_ON_DELAY_MS')
        if ($Display.BUS -ne $script:BoardDisplayBus) { Fail 'display bus is incompatible' }
        if ($Config.BOARD -eq 'custom') {
            $script:BoardDisplayDataWidth = $Display.DATA_WIDTH
        } elseif ($Display.DATA_WIDTH -ne $script:BoardDisplayDataWidth) {
            Fail 'display data width is incompatible'
        }
    }
    if ($Config.INPUT -eq 'custom') {
        if (-not (Test-Id $Config.INPUT_NAME)) { Fail 'custom input requires a valid INPUT_NAME' }
    } elseif ($Config.INPUT -eq 'none') {
        if ($script:BoardInputBus -ne 'NONE') { Fail 'input none requires board input bus NONE' }
    } else {
        $Input = Get-Record 'input' $Config.INPUT
        Assert-Keys $Input @('SCHEMA_VERSION', 'ID', 'BUS', 'DRIVER', 'USE_IRQ', 'SWAP_XY', 'MIRROR_X', 'MIRROR_Y', 'I2C_ADDRESS', 'I2C_CLOCK_HZ', 'I2C_CONTROL_PHASE_BYTES', 'I2C_DC_BIT_OFFSET', 'I2C_CMD_BITS', 'I2C_PARAM_BITS', 'I2C_DISABLE_CONTROL_PHASE', 'I2C_INTERNAL_PULLUP', 'RESET_LEVEL', 'IRQ_LEVEL')
        if ($Input.BUS -ne $script:BoardInputBus) { Fail 'input bus is incompatible' }
    }

    $script:GpioValues = @{}
    $script:GpioKinds = @{}
    $script:BoardGpio = @{}
    Set-RequiredGpioRoles $Config
    Load-GpioList $script:GpioValues $script:GpioKinds $script:BoardGpio 'input' $BoardInputGpio
    Load-GpioList $script:GpioValues $script:GpioKinds $script:BoardGpio 'output' $BoardOutputGpio
    foreach ($Key in @($Config.Keys)) {
        if ($Key -like 'GPIO_*') {
            $Role = $Key.Substring(5)
            if (-not $script:RequiredGpioKinds.ContainsKey($Role)) {
                if ($Config.BOARD -ne 'custom' -and (Test-BoardDeclaresGpioRole $Role $BoardInputGpio $BoardOutputGpio)) { continue }
                Fail "GPIO override names an unknown required role: $Role"
            }
            $script:GpioValues[$Role] = $Config[$Key]
        }
    }
    foreach ($Role in $script:RequiredGpioKinds.Keys) {
        if (-not $script:GpioValues.ContainsKey($Role)) { Fail "missing required $($script:RequiredGpioKinds[$Role]) GPIO role $Role" }
        $script:GpioKinds[$Role] = $script:RequiredGpioKinds[$Role]
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
    $script:ModuleState = @{}
    $script:SelectedModules = @()
    foreach ($Module in Split-Csv $Config.MODULES) { Visit-Module $Module }
    $Config.MODULES = ConvertTo-SortedCsv $script:SelectedModules
    Resolve-DashboardSelection $Config
    $DashboardsAvailable = @(Get-AvailableDashboardIds $Config).Count -gt 0
    $SelectedDashboards = [System.Collections.Generic.List[string]]::new()
    foreach ($Dashboard in Split-Csv $Config.DASHBOARDS) {
        if (-not (Test-Id $Dashboard)) { Fail "invalid dashboard id: $Dashboard" }
        $DashboardRecord = Get-Record 'dashboard' $Dashboard
        Assert-Keys $DashboardRecord @('SCHEMA_VERSION', 'ID', 'REQUIRES_MODULES', 'REQUIRES_MODULES_ANY')
        if (-not (Test-DashboardAvailable $Config $Dashboard)) {
            if ($Dashboard -eq 'controller') { Fail 'controller dashboard requires controller module' }
            Fail 'dashboard UIs require GPS or controller module'
        }
        [void]$SelectedDashboards.Add($Dashboard)
    }
    $Config.DASHBOARDS = ConvertTo-SortedCsv $SelectedDashboards.ToArray()
    if ([string]::IsNullOrEmpty($Config.DASHBOARDS) -and $DashboardsAvailable) { Fail 'select at least one dashboard UI' }
    if ($Config.BOARD -eq 'custom') { Assert-CustomBoardGpioRoles $Config }
}

function Write-Utf8NoBom([string]$Path, [string]$Content) {
    [System.IO.File]::WriteAllText($Path, $Content, (New-Object System.Text.UTF8Encoding($false)))
}

function Write-Profile([System.Collections.IDictionary]$Config) {
    New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null
    $Profile = $Config.PROFILE
    $Temp = Join-Path $BuildRoot ".${Profile}.tmp.$([guid]::NewGuid().ToString('N'))"
    $ProfileDir = Join-Path $BuildRoot $Profile
    New-Item -ItemType Directory -Path (Join-Path $Temp 'generated') -Force | Out-Null
    $Lines = @("SCHEMA_VERSION=$SchemaVersion", "PROFILE=$Profile", "MCU=$($Config.MCU)", "BOARD=$($Config.BOARD)", "DISPLAY=$($Config.DISPLAY)", "INPUT=$($Config.INPUT)", "MODULES=$($Config.MODULES)", "DASHBOARDS=$($Config.DASHBOARDS)", "CONFIRM_DANGEROUS_GPIO=$($Config.CONFIRM_DANGEROUS_GPIO)")
    foreach ($Key in @('BOARD_NAME', 'DISPLAY_NAME', 'INPUT_NAME', 'DISPLAY_BUS', 'INPUT_BUS', 'FLASH_MB', 'PSRAM_MB', 'PARTITIONS', 'INPUT_GPIO', 'OUTPUT_GPIO')) {
        if (-not [string]::IsNullOrEmpty($Config[$Key])) { $Lines += "$Key=$($Config[$Key])" }
    }
    foreach ($Role in ($script:GpioValues.Keys | Sort-Object)) { $Lines += "GPIO_$Role=$($script:GpioValues[$Role])" }
    $FirmwareEnv = (Join-Path $Temp 'firmware.env')
    $FirmwareEnvContent = (($Lines -join "`n") + "`n")
    Write-Utf8NoBom $FirmwareEnv $FirmwareEnvContent
    Write-Utf8NoBom (Join-Path $Temp 'normalized.env') $FirmwareEnvContent
    & (Get-PythonExecutable) (Join-Path $Root 'scripts/generate-hardware-config.py') --catalog $CatalogDir --firmware-env $FirmwareEnv --output (Join-Path $Temp 'generated/esp_bms_profile_hardware.h')
    if ($LASTEXITCODE -ne 0) { throw 'hardware configuration generation failed' }
    $MainRequires = @('esp_bms_idf_runtime', 'esp_bms_lvgl_bridge', 'esp_bms_lvgl_ui', 'lvgl', 'esp_lvgl_adapter')
    $AudioFeature = 0
    $BmsFeature = 0
    $ControllerFeature = 0
    $GpsFeature = 0
    $NetworkFeature = 0
    $OtaFeature = 0
    $DashboardS1000rrFeature = 0
    $DashboardControllerFeature = 0
    $DashboardFirebladeFeature = 0
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
    if ((Split-Csv $Config.DASHBOARDS) -contains 's1000rr') { $DashboardS1000rrFeature = 1 }
    if ((Split-Csv $Config.DASHBOARDS) -contains 'controller') { $DashboardControllerFeature = 1 }
    if ((Split-Csv $Config.DASHBOARDS) -contains 'fireblade') { $DashboardFirebladeFeature = 1 }
    $Cmake = @(
        "set(ESP_BMS_PROFILE_ID `"$Profile`")"
        "set(ESP_BMS_SELECTED_MODULES `"$($Config.MODULES)`")"
        "set(ESP_BMS_SELECTED_DASHBOARDS `"$($Config.DASHBOARDS)`")"
        'set(ESP_BMS_PROFILE_TRIMMING_READY FALSE)'
        "set(ESP_BMS_FEATURE_AUDIO $AudioFeature CACHE BOOL `"Firmware profile audio feature`" FORCE)"
        "set(ESP_BMS_FEATURE_BMS $BmsFeature CACHE BOOL `"Firmware profile BMS feature`" FORCE)"
        "set(ESP_BMS_FEATURE_CONTROLLER $ControllerFeature CACHE BOOL `"Firmware profile controller feature`" FORCE)"
        "set(ESP_BMS_FEATURE_GPS $GpsFeature CACHE BOOL `"Firmware profile GPS feature`" FORCE)"
        "set(ESP_BMS_FEATURE_NETWORK $NetworkFeature CACHE BOOL `"Firmware profile network feature`" FORCE)"
        "set(ESP_BMS_FEATURE_OTA $OtaFeature CACHE BOOL `"Firmware profile OTA feature`" FORCE)"
        "set(ESP_BMS_FEATURE_DASHBOARD_S1000RR $DashboardS1000rrFeature CACHE BOOL `"Firmware profile S1000RR dashboard`" FORCE)"
        "set(ESP_BMS_FEATURE_DASHBOARD_CONTROLLER $DashboardControllerFeature CACHE BOOL `"Firmware profile controller dashboard`" FORCE)"
        "set(ESP_BMS_FEATURE_DASHBOARD_FIREBLADE $DashboardFirebladeFeature CACHE BOOL `"Firmware profile Fireblade dashboard`" FORCE)"
        "set(ESP_BMS_PROFILE_MAIN_REQUIRES `"$($MainRequires -join ';')`" CACHE STRING `"Firmware profile component closure`" FORCE)"
    )
    Write-Utf8NoBom (Join-Path $Temp 'generated/profile.cmake') (($Cmake -join "`n") + "`n")
    $ModuleLines = @("MODULES=$($Config.MODULES)", "DASHBOARDS=$($Config.DASHBOARDS)")
    foreach ($Module in ($script:SelectedModules | Sort-Object -Unique)) {
        $Record = Get-Record 'module' $Module
        $ModuleLines += "MODULE_$Module`_COMPONENTS=$($Record.COMPONENTS)"
    }
    Write-Utf8NoBom (Join-Path $Temp 'generated/modules.env') (($ModuleLines -join "`n") + "`n")
    $SdkconfigDefaults = Join-Path $Root "sdkconfig.defaults.$($Config.MCU)"
    if (-not (Test-Path -LiteralPath $SdkconfigDefaults -PathType Leaf)) { $SdkconfigDefaults = Join-Path $Root 'sdkconfig.defaults' }
    $ProfilePartitionTable = Join-Path $ProfileDir 'partitions.csv'
    $SdkconfigLines = @(
        Get-Content -LiteralPath $SdkconfigDefaults |
            Where-Object { $_ -notmatch '^CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=' }
    )
    $SdkconfigLines += "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=`"$ProfilePartitionTable`""
    Write-Utf8NoBom (Join-Path $Temp 'sdkconfig.defaults') (($SdkconfigLines -join "`n") + "`n")
    Copy-Item -LiteralPath (Join-Path $Root $script:BoardPartitions) -Destination (Join-Path $Temp 'partitions.csv')
    $Report = @("PROFILE=$Profile", "MCU=$($Config.MCU)", "BOARD=$($Config.BOARD)", "BUILD_READY=$script:BoardBuildReady", "MODULES=$($Config.MODULES)", "DASHBOARDS=$($Config.DASHBOARDS)", "TRIMMING=$Trimming", 'NOTE=Generated selection will become the component closure after runtime extraction.')
    Write-Utf8NoBom (Join-Path $Temp 'report.txt') (($Report -join "`n") + "`n")
    if (Test-Path -LiteralPath $ProfileDir) {
        $Backup = Join-Path $BuildRoot ".${Profile}.previous.$([DateTimeOffset]::UtcNow.ToUnixTimeSeconds())"
        Move-Item -LiteralPath $ProfileDir -Destination $Backup
        Write-LocalizedOutput "previous profile preserved at $($Backup.Substring($Root.Length + 1))"
    }
    Move-Item -LiteralPath $Temp -Destination $ProfileDir
    Write-LocalizedOutput "profile: $($ProfileDir.Substring($Root.Length + 1))"
    Write-LocalizedOutput "normalized: $($ProfileDir.Substring($Root.Length + 1))/normalized.env"
}

function Invoke-LocalBuild([System.Collections.IDictionary]$Config) {
    if ($script:BoardBuildReady -ne 'YES') { Fail "board $($Config.BOARD) is not build-ready yet" }
    $ProfileDir = Join-Path $BuildRoot $Config.PROFILE
    $BuildDir = Join-Path (Join-Path $IdfBuildRoot $Config.PROFILE) 'idf-build'
    $env:ESP_BMS_PROFILE_FILE = Join-Path $ProfileDir 'generated/profile.cmake'
    $IdfExport = Ensure-IdfExportScript

    if (-not (Test-IdfExportScript $IdfExport)) { Fail 'ESP-IDF export.ps1 path is invalid' }
    . ([string]$IdfExport)
    if ($null -eq (Get-Command idf.py -ErrorAction SilentlyContinue)) {
        if ($script:Language -eq 'en') {
            Fail 'missing idf.py after ESP-IDF environment setup'
        }
        Fail '导入 ESP-IDF 环境后仍未找到 idf.py'
    }
    Assert-RequiredIdfVersion

    $OriginalLocation = Get-Location
    try {
        Push-Location $Root
        $IdfArgs = @(
            '-B', $BuildDir
            "-DIDF_TARGET=$($Config.MCU)"
            "-DSDKCONFIG=$(Join-Path $ProfileDir 'sdkconfig')"
            "-DSDKCONFIG_DEFAULTS=$(Join-Path $ProfileDir 'sdkconfig.defaults')"
            "-DESP_BMS_PROFILE_FILE=$env:ESP_BMS_PROFILE_FILE"
            'build'
        )
        & idf.py @IdfArgs
        if (Test-Path -LiteralPath Variable:global:LASTEXITCODE) {
            $script:BuildExitCode = $global:LASTEXITCODE
        } else {
            $script:BuildExitCode = 1
            if ($script:Language -eq 'en') {
                Fail 'idf.py did not provide an exit code'
            }
            Fail 'idf.py 未提供退出码'
        }
    }
    finally {
        Set-Location $OriginalLocation
    }

    if ($script:BuildExitCode -eq 0) {
        Show-LocalBuildResult $Config $BuildDir
    }
}

function Get-FirmwareOtaCode([string]$FirmwarePath) {
    $Python = Get-PythonExecutable

    # Keep this byte-for-byte compatible with esp_bms_ota and scripts/build-firmware.py.
    $PythonCode = 'import functools,sys,zlib;firmware=open(sys.argv[1],"rb");crc=functools.reduce(lambda value,chunk:zlib.crc32(chunk,value),iter(lambda:firmware.read(65536),b""),0);print(f"{(crc & 0xffffffff) % 10000:04d}")'
    $CodeLines = @(& $Python -c $PythonCode $FirmwarePath)
    if ($LASTEXITCODE -ne 0 -or $CodeLines.Count -ne 1 -or $CodeLines[0] -notmatch '^[0-9]{4}$') {
        if ($script:Language -eq 'en') { Fail 'failed to calculate the OTA code from the firmware' }
        Fail '无法根据固件计算 OTA 密码'
    }
    return $CodeLines[0]
}

function Show-LocalBuildResult([System.Collections.IDictionary]$Config, [string]$BuildDir) {
    $FirmwarePath = Join-Path $BuildDir 'esp32_bms_gps_idf.bin'
    if (-not (Test-Path -LiteralPath $FirmwarePath -PathType Leaf)) {
        if ($script:Language -eq 'en') { Fail "build completed but firmware is missing: $FirmwarePath" }
        Fail "编译完成但未找到固件：$FirmwarePath"
    }

    $FirmwarePath = (Resolve-Path -LiteralPath $FirmwarePath).Path
    $BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
    $OutputDirectory = Join-Path $FirmwareOutputRoot $Config.PROFILE
    [void](New-Item -ItemType Directory -Path $OutputDirectory -Force)
    $OutputFirmwarePath = Join-Path $OutputDirectory 'esp32_bms_gps_idf.bin'
    Copy-Item -LiteralPath $FirmwarePath -Destination $OutputFirmwarePath -Force
    $FirmwarePath = (Resolve-Path -LiteralPath $OutputFirmwarePath).Path
    $FlashCommand = ".\scripts\flash.ps1 -Port COMx -BuildDir `"$BuildDir`""

    Write-Host ''
    if ($script:Language -eq 'en') {
        Write-Host 'Build completed'
        Write-Host "  Firmware: $FirmwarePath"
        if ((Split-Csv $Config.MODULES) -contains 'ota') {
            Write-Host "  OTA code: $(Get-FirmwareOtaCode $FirmwarePath)"
            Write-Host '  Enter this four-digit code when uploading the firmware in the device web UI.'
        } else {
            Write-Host '  OTA code: OTA is not enabled for this firmware.'
        }
        Write-Host "  Flash command (replace COMx with the actual port): $FlashCommand"
    } else {
        Write-Host '编译完成'
        Write-Host "  固件地址：$FirmwarePath"
        if ((Split-Csv $Config.MODULES) -contains 'ota') {
            Write-Host "  固件 OTA 密码：$(Get-FirmwareOtaCode $FirmwarePath)"
            Write-Host '  请在设备网页上传固件时填写上述四位密码。'
        } else {
            Write-Host '  固件 OTA 密码：此固件未启用 OTA。'
        }
        Write-Host "  烧录命令（将 COMx 替换为实际串口）：$FlashCommand"
    }

    if (-not (Test-InteractiveTerminal)) { return }
    $ConfirmPrompt = if ($script:Language -eq 'en') { 'Flash this firmware now? [y/N]' } else { '现在烧录这个固件吗？[y/N]' }
    if ((Read-Host $ConfirmPrompt) -notmatch '^[Yy]$') { return }
    $FlashScript = Join-Path $Root 'scripts/flash.ps1'
    $Target = Read-Host $(if ($script:Language -eq 'en') { 'Flash target: 1) Local serial (default) 2) Remote RFC2217 [1]' } else { '烧录目标：1) 本地串口（默认）2) 远程 RFC2217 [1]' })
    if ([string]::IsNullOrWhiteSpace($Target)) { $Target = '1' }
    switch ($Target.Trim()) {
        '1' {
            $PortPrompt = if ($script:Language -eq 'en') { 'Local serial port (for example COM3; leave blank to auto-detect)' } else { '本地串口（例如 COM3；留空则自动检测）' }
            $Port = Read-Host $PortPrompt
            if ([string]::IsNullOrWhiteSpace($Port)) {
                & $FlashScript -BuildDir $BuildDir
            } else {
                & $FlashScript -Port $Port.Trim() -BuildDir $BuildDir
            }
        }
        '2' {
            $Port = Read-Host $(if ($script:Language -eq 'en') { 'Remote RFC2217 URL' } else { '远程 RFC2217 地址' })
            if ($Port -notmatch '(?i)^rfc2217://') { Fail 'remote flash requires an rfc2217:// URL' }
            & $FlashScript -Port $Port.Trim() -BaudRate 115200 -BuildDir $BuildDir
        }
        default { Write-LocalizedError 'Flash canceled: invalid target' }
    }
}

function Invoke-CloudBuild([System.Collections.IDictionary]$Config) {
    $env:FIRMWARE_LANG = $script:Language
    & (Get-PythonExecutable) (Join-Path $Root 'scripts/dispatch-cloud-build.py') --config (Join-Path (Join-Path $BuildRoot $Config.PROFILE) 'firmware.env')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

function Invoke-Doctor {
    $Missing = $false
    foreach ($Name in @('git', 'python')) {
        if ($Name -eq 'python') {
            $Python = Get-PythonExecutable
            Write-LocalizedOutput "ok: python=$Python"
            continue
        }
        $CommandInfo = Get-Command $Name -ErrorAction SilentlyContinue
        if ($null -eq $CommandInfo) { Write-LocalizedError "missing: $Name"; $Missing = $true } else { Write-LocalizedOutput "ok: $Name=$($CommandInfo.Source)" }
    }
    $IdfExport = [string](Get-IdfExportScript | Select-Object -Last 1)
    if ($null -eq $IdfExport) {
        Write-LocalizedError 'missing: ESP-IDF v6.0.2 export.ps1'
        $Missing = $true
    } elseif (-not (Test-IdfExportScript $IdfExport)) {
        Write-LocalizedError 'invalid ESP-IDF export.ps1 path'
        $Missing = $true
    } else {
        . ([string]$IdfExport)
        foreach ($Name in @('cmake', 'ninja', 'idf.py')) {
            $CommandInfo = Get-Command $Name -ErrorAction SilentlyContinue
            if ($null -eq $CommandInfo) { Write-LocalizedError "missing: $Name after ESP-IDF environment setup"; $Missing = $true } else { Write-LocalizedOutput "ok: $Name=$($CommandInfo.Source)" }
        }
        if ($null -ne (Get-Command idf.py -ErrorAction SilentlyContinue)) {
            try {
                Assert-RequiredIdfVersion
                Write-LocalizedOutput "ok: $script:RequiredIdfVersion"
            } catch {
                Write-LocalizedError $_.Exception.Message
                $Missing = $true
            }
        }
    }
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

function Get-CatalogDisplayIdsSupportedByMcu([string]$McuId) {
    $Mcu = Get-Record 'mcu' $McuId
    $Result = [System.Collections.Generic.List[string]]::new()
    foreach ($Id in (Get-CatalogIds 'display')) {
        $Display = Get-Record 'display' $Id
        if (Test-CsvContains $Mcu.DISPLAY_BUSES $Display.BUS) { $Result.Add($Id) }
    }
    return @($Result | Sort-Object -Unique)
}

function Get-CatalogOptionDescription([string]$Kind, [string]$Id) {
    $Labels = @{
        'mcu/esp32' = @('ESP32，最多 39 路 GPIO，支持 SPI 显示', 'ESP32, up to GPIO39, SPI display support')
        'mcu/esp32s3' = @('ESP32-S3，最多 48 路 GPIO，支持 SPI / I80 显示', 'ESP32-S3, up to GPIO48, SPI / I80 display support')
        'board/esp32-wroom-32e-legacy' = @('ESP32-WROOM-32E，4MB Flash，可本地构建（推荐）', 'ESP32-WROOM-32E, 4MB Flash, build-ready (recommended)')
        'board/esp32s3-n16r8-st7796u-gt1151' = @('慧勤智远 ESP32-S3 N16R8，ST7796U / GT1151，16MB Flash / 8MB PSRAM，可本地构建', 'Huiqin Zhiyuan ESP32-S3 N16R8, ST7796U / GT1151, 16MB Flash / 8MB PSRAM, build-ready')
        'board/custom' = @('自定义开发板：选择 MCU、显示屏并填写 GPIO', 'Custom board: choose MCU, display, and GPIO pins')
        'display/st7789-spi' = @('ST7789 SPI 显示屏', 'ST7789 SPI display')
        'display/ili9488-i80' = @('ILI9488 8080 并行显示屏', 'ILI9488 I80 parallel display')
        'display/st7796u-i80' = @('ST7796U 16 位 8080 并行显示屏（320 × 480）', 'ST7796U 16-bit I80 parallel display (320 × 480)')
        'display/custom' = @('自定义显示屏：填写名称并选择总线', 'Custom display: name it and choose its bus')
        'input/xpt2046-spi' = @('XPT2046 SPI 触摸屏', 'XPT2046 SPI touch input')
        'input/ft6336u-i2c' = @('FT6336U I2C 触摸屏', 'FT6336U I2C touch input')
        'input/gt1151-i2c' = @('GT1151 I2C 电容触摸屏', 'GT1151 I2C capacitive touch input')
        'input/custom' = @('自定义输入设备：填写名称并选择总线', 'Custom input: name it and choose its bus')
        'input/none' = @('不使用输入设备', 'No input device')
        'module/bms' = @('BMS 蓝牙连接', 'BMS Bluetooth connection')
        'module/gps' = @('GPS 定位与测速', 'GPS positioning and speed')
        'module/controller' = @('控制器蓝牙', 'Controller Bluetooth')
        'module/audio' = @('音频提示', 'Audio feedback')
        'module/network' = @('Wi-Fi、设置热点与本地网页', 'Wi-Fi, setup AP, and local web UI')
        'module/ota' = @('本地 Web OTA 更新（自动需要 network）', 'Local web OTA update (automatically requires network)')
        'module/cast' = @('实验性手机投屏（当前使用 legacy runtime）', 'Experimental phone casting (uses the legacy runtime)')
        'dashboard/s1000rr' = @('宝马 S1000RR 速度仪表', 'BMW S1000RR speed dashboard')
        'dashboard/controller' = @('控制器监控仪表', 'Controller monitoring dashboard')
        'dashboard/fireblade' = @('本田火刃仪表', 'Honda Fireblade dashboard')
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

function Select-ValueOption([string]$Title, [string]$Default, [string[]]$Options) {
    while ($true) {
        Write-Host ''
        Write-Host (Convert-LocalizedText $Title)
        for ($Index = 0; $Index -lt $Options.Count; $Index++) {
            $Mark = if ($Options[$Index] -eq $Default) { ' *' } else { '' }
            Write-Host ("  {0}) {1}{2}" -f ($Index + 1), $Options[$Index], $Mark)
        }
        $Prompt = if ($script:Language -eq 'en') { "Enter a number or value [$Default]" } else { "输入编号或值 [$Default]" }
        $Answer = Read-Host $Prompt
        if ([string]::IsNullOrWhiteSpace($Answer)) { $Answer = $Default }
        $Number = 0
        if ([int]::TryParse($Answer, [ref]$Number) -and $Number -ge 1 -and $Number -le $Options.Count) { return $Options[$Number - 1] }
        if ($Options -contains $Answer) { return $Answer }
        [Console]::Error.WriteLine($(if ($script:Language -eq 'en') { 'Invalid selection; please try again.' } else { '无效选择，请重新输入。' }))
    }
}

function Read-CustomId([System.Collections.IDictionary]$Config, [string]$Key, [string]$ZhPrompt, [string]$EnPrompt) {
    while ($true) {
        $Answer = Read-Host $(if ($script:Language -eq 'en') { $EnPrompt } else { $ZhPrompt })
        if (Test-Id $Answer) { $Config[$Key] = $Answer; return }
        [Console]::Error.WriteLine($(if ($script:Language -eq 'en') { 'Use 1-64 ASCII letters, numbers, hyphens, or underscores.' } else { '请使用 1–64 位 ASCII 字母、数字、连字符或下划线。' }))
    }
}

function Read-CustomNumber([System.Collections.IDictionary]$Config, [string]$Key, [string]$Default, [string]$ZhPrompt, [string]$EnPrompt) {
    while ($true) {
        $Prompt = if ($script:Language -eq 'en') { "$EnPrompt [$Default]" } else { "$ZhPrompt [$Default]" }
        $Answer = Read-Host $Prompt
        if ([string]::IsNullOrWhiteSpace($Answer)) { $Answer = $Default }
        if (Test-UnsignedInteger $Answer) { $Config[$Key] = $Answer; return }
        [Console]::Error.WriteLine($(if ($script:Language -eq 'en') { 'Enter a non-negative integer.' } else { '请输入非负整数。' }))
    }
}

function Test-InteractiveTerminal {
    try {
        return (-not [Console]::IsInputRedirected) -and (-not [Console]::IsOutputRedirected)
    } catch {
        return $false
    }
}

function Select-CatalogOptionsWithKeyboard([string]$Kind, [string]$Default, [string[]]$Options) {
    if ($Options.Count -eq 0) { Fail "no $Kind catalog options" }
    $Title = if ($Kind -eq 'module') { 'Modules' } elseif ($Kind -eq 'dashboard') { 'Dashboard UIs' } else { Fail "unsupported catalog menu: $Kind" }
    $Selected = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
    foreach ($Option in (Split-Csv $Default)) {
        if ($Options -contains $Option) { [void]$Selected.Add($Option) }
    }
    $Cursor = 0
    while ($true) {
        Clear-Host
        Show-InteractiveTitle
        Write-Host ''
        Write-Host (Convert-LocalizedText $Title)
        for ($Index = 0; $Index -lt $Options.Count; $Index++) {
            $Option = $Options[$Index]
            $Pointer = if ($Index -eq $Cursor) { '>' } else { ' ' }
            $Mark = if ($Selected.Contains($Option)) { '[x]' } else { '[ ]' }
            Write-Host ("  {0} {1} {2}) {3} — {4}" -f $Pointer, $Mark, ($Index + 1), $Option, (Get-CatalogOptionDescription $Kind $Option))
        }
        Write-Host $(if ($script:Language -eq 'en') { 'Use Up/Down to move, Space to toggle, Enter to continue.' } else { '使用 ↑/↓ 移动，空格切换，回车下一步。' })

        $Key = [Console]::ReadKey($true).Key
        if ($Key -eq [ConsoleKey]::UpArrow) {
            $Cursor = if ($Cursor -eq 0) { $Options.Count - 1 } else { $Cursor - 1 }
            continue
        }
        if ($Key -eq [ConsoleKey]::DownArrow) {
            $Cursor = ($Cursor + 1) % $Options.Count
            continue
        }
        if ($Key -eq [ConsoleKey]::Spacebar) {
            $Option = $Options[$Cursor]
            if ($Selected.Contains($Option)) { [void]$Selected.Remove($Option) } else { [void]$Selected.Add($Option) }
            continue
        }
        if ($Key -eq [ConsoleKey]::Enter) {
            $Values = [System.Collections.Generic.List[string]]::new()
            foreach ($Option in $Selected) { [void]$Values.Add($Option) }
            return ConvertTo-SortedCsv $Values.ToArray()
        }
    }
}

function Select-ModuleOptionsWithKeyboard([string]$Default, [string[]]$Options) {
    return (Select-CatalogOptionsWithKeyboard 'module' $Default $Options)
}

function Select-ModuleOptions([string]$Default) {
    $Options = @(Get-CatalogIds 'module')
    if (Test-InteractiveTerminal) { return (Select-ModuleOptionsWithKeyboard $Default $Options) }
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

function Select-DashboardOptions([System.Collections.IDictionary]$Config) {
    $Options = @(Get-AvailableDashboardIds $Config)
    if ($Options.Count -eq 0) {
        $Config.DASHBOARDS = ''
        return ''
    }
    Resolve-DashboardSelection $Config
    $Default = $Config.DASHBOARDS
    if (Test-InteractiveTerminal) {
        $Config.DASHBOARDS_AUTO = 'NO'
        return (Select-CatalogOptionsWithKeyboard 'dashboard' $Default $Options)
    }
    while ($true) {
        Write-Host ''
        Write-Host (Convert-LocalizedText 'Dashboard UIs')
        for ($Index = 0; $Index -lt $Options.Count; $Index++) {
            $Option = $Options[$Index]
            $Mark = if (",$Default," -like "*,$Option,*") { ' *' } else { '' }
            Write-Host ("  {0}) {1} — {2}{3}" -f ($Index + 1), $Option, (Get-CatalogOptionDescription 'dashboard' $Option), $Mark)
        }
        $Prompt = if ($script:Language -eq 'en') { "Enter comma-separated numbers or IDs [$Default]" } else { "输入以逗号分隔的编号或 ID [$Default]" }
        $Answer = Read-Host $Prompt
        if ([string]::IsNullOrWhiteSpace($Answer)) { return $Default }
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
        if ($Valid -and $Selected.Count -gt 0) {
            $Config.DASHBOARDS_AUTO = 'NO'
            return ConvertTo-SortedCsv $Selected.ToArray()
        }
        [Console]::Error.WriteLine($(if ($script:Language -eq 'en') { 'Invalid dashboard selection; please try again.' } else { '无效仪表选择，请重新输入。' }))
    }
}

function Set-CustomBoardGpio([System.Collections.IDictionary]$Config) {
    $Config.INPUT_GPIO = ''
    $Config.OUTPUT_GPIO = ''
    foreach ($Requirement in (Get-CustomBoardGpioRequirements $Config)) {
        $Parts = $Requirement -split ':', 2
        $Kind = $Parts[0]
        $Role = $Parts[1]
        while ($true) {
            $Prompt = if ($script:Language -eq 'en') { "$Role GPIO (0-$script:MCUGpioMax)" } else { "$Role 的 GPIO（0–$script:MCUGpioMax）" }
            $Answer = Read-Host $Prompt
            if (Test-Pin $Answer) {
                Add-GpioDeclaration $Config $(if ($Kind -eq 'input') { 'INPUT_GPIO' } else { 'OUTPUT_GPIO' }) $Role $Answer
                break
            }
            [Console]::Error.WriteLine($(if ($script:Language -eq 'en') { 'Enter a GPIO number from 0 to 99.' } else { '请输入 0–99 的 GPIO 编号。' }))
        }
    }
    foreach ($List in @($Config.INPUT_GPIO, $Config.OUTPUT_GPIO)) {
        foreach ($Pair in Split-Csv $List) {
            $Pin = ($Pair -split ':', 2)[1]
            if (Test-CsvContains $script:MCUDangerous $Pin) {
                $Prompt = if ($script:Language -eq 'en') { "GPIO $Pin is a boot-sensitive pin. Confirm its use? [y/N]" } else { "GPIO $Pin 是启动敏感引脚，确认使用吗？[y/N]" }
                if ((Read-Host $Prompt) -notmatch '^[Yy]$') { Fail "dangerous GPIO $Pin was not confirmed" }
                $Config.CONFIRM_DANGEROUS_GPIO = 'YES'
            }
        }
    }
}

function Set-CustomBoardConfig([System.Collections.IDictionary]$Config) {
    $Config.BOARD = 'custom'
    Read-CustomId $Config 'BOARD_NAME' '自定义开发板名称（ASCII）' 'Custom board name (ASCII)'
    $Config.MCU = Select-CatalogOption 'mcu' 'MCU' $Config.MCU @(Get-CatalogIds 'mcu')
    $Mcu = Get-Record 'mcu' $Config.MCU
    $script:MCUDisplayBuses = $Mcu.DISPLAY_BUSES
    $script:MCUGpioMax = [int]$Mcu.GPIO_MAX
    $script:MCUDangerous = $Mcu.DANGEROUS_GPIO
    Read-CustomNumber $Config 'FLASH_MB' '4' 'Flash 容量（MB）' 'Flash size (MB)'
    Read-CustomNumber $Config 'PSRAM_MB' '0' 'PSRAM 容量（MB）' 'PSRAM size (MB)'
    $Config.PARTITIONS = 'partitions.csv'

    $DisplayOptions = @(Get-CatalogDisplayIdsSupportedByMcu $Config.MCU) + 'custom'
    $Config.DISPLAY = Select-CatalogOption 'display' 'Display' 'custom' $DisplayOptions
    if ($Config.DISPLAY -eq 'custom') {
        Read-CustomId $Config 'DISPLAY_NAME' '自定义显示屏名称（ASCII）' 'Custom display name (ASCII)'
        $DisplayBuses = @(Split-Csv $script:MCUDisplayBuses)
        $Config.DISPLAY_BUS = Select-ValueOption 'Display bus' $DisplayBuses[0] $DisplayBuses
        if ($Config.DISPLAY_BUS -eq 'I80') {
            $script:BoardDisplayDataWidth = Select-ValueOption 'I80 data width' '8' @('8', '16')
        } else {
            $script:BoardDisplayDataWidth = '0'
        }
    } else {
        $Display = Get-Record 'display' $Config.DISPLAY
        $Config.DISPLAY_BUS = $Display.BUS
        $script:BoardDisplayDataWidth = $Display.DATA_WIDTH
    }

    $InputOptions = @(Get-CatalogIds 'input') + @('custom', 'none')
    $Config.INPUT = Select-CatalogOption 'input' 'Input' 'custom' $InputOptions
    if ($Config.INPUT -eq 'custom') {
        Read-CustomId $Config 'INPUT_NAME' '自定义输入设备名称（ASCII）' 'Custom input name (ASCII)'
        $Config.INPUT_BUS = Select-ValueOption 'Input bus' 'SPI' @('SPI', 'I2C', 'NONE')
    } elseif ($Config.INPUT -eq 'none') {
        $Config.INPUT_BUS = 'NONE'
    } else {
        $Config.INPUT_BUS = (Get-Record 'input' $Config.INPUT).BUS
    }
    $script:BoardDisplayBus = $Config.DISPLAY_BUS
    $script:BoardInputBus = $Config.INPUT_BUS
    $Config.MODULES = Select-ModuleOptions $Config.MODULES
    $Config.DASHBOARDS = Select-DashboardOptions $Config
    Set-CustomBoardGpio $Config
}

function Set-InteractiveProfileName([System.Collections.IDictionary]$Config, [switch]$Prompt) {
    $Source = $Config.BOARD
    if ($Source -eq 'custom') { $Source = $Config.BOARD_NAME }
    if ([string]::IsNullOrEmpty($Source)) { $Source = $Config.MCU }
    $Config.PROFILE = $Source
    if (-not $Prompt -or -not (Test-InteractiveTerminal)) { return }
    while ($true) {
        $Answer = Read-Host $(if ($script:Language -eq 'en') { "Configuration name [$Source]" } else { "配置名称 [$Source]" })
        if ([string]::IsNullOrWhiteSpace($Answer)) { return }
        if (Test-Id $Answer.Trim()) { $Config.PROFILE = $Answer.Trim(); return }
        [Console]::Error.WriteLine($(if ($script:Language -eq 'en') { 'Use 1-64 ASCII letters, numbers, _ or -.' } else { '请输入 1–64 个 ASCII 字母、数字、_ 或 -。' }))
    }
}

function Set-MissingBoardGpio([System.Collections.IDictionary]$Config) {
    $Mcu = Get-Record 'mcu' $Config.MCU
    $script:MCUCapabilities = $Mcu.CAPABILITIES
    $script:MCUDisplayBuses = $Mcu.DISPLAY_BUSES
    $script:MCUGpioMax = [int]$Mcu.GPIO_MAX
    $script:MCUInputOnly = $Mcu.INPUT_ONLY
    $script:MCUDangerous = $Mcu.DANGEROUS_GPIO
    $Board = Get-Record 'board' $Config.BOARD
    $script:BoardDisplayBus = $Board.DISPLAY_BUS
    $script:BoardDisplayDataWidth = $Board.DISPLAY_DATA_WIDTH
    $script:BoardInputBus = $Board.INPUT_BUS
    $script:BoardAudioBackend = $Board.AUDIO_BACKEND
    $script:BoardAudioDacChannel = $Board.AUDIO_DAC_CHANNEL
    $script:BoardAudioEnableActiveLevel = $Board.AUDIO_ENABLE_ACTIVE_LEVEL
    $BoardInputGpio = $Board.INPUT_GPIO
    $BoardOutputGpio = $Board.OUTPUT_GPIO
    Set-RequiredGpioRoles $Config
    $script:GpioValues = @{}
    $script:GpioKinds = @{}
    $script:BoardGpio = @{}
    Load-GpioList $script:GpioValues $script:GpioKinds $script:BoardGpio 'input' $BoardInputGpio
    Load-GpioList $script:GpioValues $script:GpioKinds $script:BoardGpio 'output' $BoardOutputGpio
    foreach ($Role in @($script:RequiredGpioKinds.Keys | Sort-Object)) {
        if ($script:GpioValues.ContainsKey($Role)) { continue }
        while ($true) {
            $Prompt = if ($script:Language -eq 'en') { "$Role GPIO (0-$script:MCUGpioMax)" } else { "$Role 的 GPIO（0–$script:MCUGpioMax）" }
            $Answer = Read-Host $Prompt
            if (-not (Test-Pin $Answer)) {
                [Console]::Error.WriteLine($(if ($script:Language -eq 'en') { 'Enter a GPIO number from 0 to 99.' } else { '请输入 0–99 的 GPIO 编号。' }))
                continue
            }
            $Config["GPIO_$Role"] = $Answer
            if (Test-CsvContains $script:MCUDangerous $Answer) {
                $Confirm = Read-Host $(if ($script:Language -eq 'en') { "GPIO $Answer is a boot-sensitive pin. Confirm its use? [y/N]" } else { "GPIO $Answer 是启动敏感引脚，确认使用吗？[y/N]" })
                if ($Confirm -notmatch '^[Yy]$') { Fail "dangerous GPIO $Answer was not confirmed" }
                $Config.CONFIRM_DANGEROUS_GPIO = 'YES'
            }
            break
        }
    }
}

function Get-SavedProfileIds {
    if (-not (Test-Path -LiteralPath $BuildRoot -PathType Container)) { return @() }
    $Profiles = [System.Collections.Generic.List[string]]::new()
    foreach ($Directory in Get-ChildItem -LiteralPath $BuildRoot -Directory -Force) {
        if (-not (Test-Id $Directory.Name) -or ($Directory.Attributes -band [IO.FileAttributes]::ReparsePoint)) { continue }
        $FirmwareEnv = Join-Path $Directory.FullName 'firmware.env'
        if (-not (Test-Path -LiteralPath $FirmwareEnv -PathType Leaf) -or ((Get-Item -LiteralPath $FirmwareEnv).Attributes -band [IO.FileAttributes]::ReparsePoint)) { continue }
        try {
            $Saved = New-DefaultConfig
            Import-UserConfig $Saved $FirmwareEnv
            Validate-Config $Saved
            [void]$Profiles.Add($Directory.Name)
        } catch {
            continue
        }
    }
    return @($Profiles | Sort-Object -Unique)
}

function Select-BoardOrSavedProfile([string]$Default) {
    $Options = [System.Collections.Generic.List[string]]::new()
    foreach ($Board in @(Get-CatalogIds 'board') + @('custom')) { [void]$Options.Add($Board) }
    foreach ($Profile in Get-SavedProfileIds) { [void]$Options.Add("saved:$Profile") }
    while ($true) {
        Write-Host ''
        Write-Host (Convert-LocalizedText 'Board')
        for ($Index = 0; $Index -lt $Options.Count; $Index++) {
            $Option = $Options[$Index]
            if ($Option.StartsWith('saved:')) {
                $Label = if ($script:Language -eq 'en') { 'Saved configuration' } else { '已保存配置' }
                Write-Host ("  {0}) [{1}] {2}" -f ($Index + 1), $Label, $Option.Substring(6))
            } else {
                $Mark = if ($Option -eq $Default) { ' *' } else { '' }
                Write-Host ("  {0}) {1} — {2}{3}" -f ($Index + 1), $Option, (Get-CatalogOptionDescription 'board' $Option), $Mark)
            }
        }
        $Answer = Read-Host $(if ($script:Language -eq 'en') { "Enter a number or ID [$Default]" } else { "输入编号或 ID [$Default]" })
        if ([string]::IsNullOrWhiteSpace($Answer)) { $Answer = $Default }
        $Number = 0
        if ([int]::TryParse($Answer, [ref]$Number) -and $Number -ge 1 -and $Number -le $Options.Count) { return $Options[$Number - 1] }
        foreach ($Option in $Options) {
            if ($Option -eq $Answer -or ($Option.StartsWith('saved:') -and $Option.Substring(6) -eq $Answer)) { return $Option }
        }
        [Console]::Error.WriteLine($(if ($script:Language -eq 'en') { 'Invalid selection; please try again.' } else { '无效选择，请重新输入。' }))
    }
}

function Show-InteractiveSummary([System.Collections.IDictionary]$Config) {
    $Modules = if ([string]::IsNullOrEmpty($Config.MODULES)) { if ($script:Language -eq 'en') { '(none)' } else { '（无）' } } else { $Config.MODULES }
    if ($script:Language -eq 'en') {
        Write-Host "`nBuild plan"
        Write-Host "  Board: $($Config.BOARD)"
        Write-Host "  MCU: $($Config.MCU)"
        Write-Host "  Display: $($Config.DISPLAY)"
        Write-Host "  Input: $($Config.INPUT)"
        Write-Host "  Modules: $Modules"
        Write-Host "  Dashboard UIs: $($Config.DASHBOARDS)"
        Write-Host "  Output: firmware-builds/$($Config.PROFILE)/"
    } else {
        Write-Host "`n构建方案"
        Write-Host "  开发板：$($Config.BOARD)"
        Write-Host "  MCU：$($Config.MCU)"
        Write-Host "  显示屏：$($Config.DISPLAY)"
        Write-Host "  输入设备：$($Config.INPUT)"
        Write-Host "  功能模块：$Modules"
        Write-Host "  仪表界面：$($Config.DASHBOARDS)"
        Write-Host "  输出目录：firmware-builds/$($Config.PROFILE)/"
    }
}

function Confirm-InteractivePlan {
    $Prompt = if ($script:Language -eq 'en') { 'Create this configuration? [Y/n]' } else { '确认生成此配置？[Y/n]' }
    $Answer = Read-Host $Prompt
    return [string]::IsNullOrEmpty($Answer) -or $Answer -match '^[Yy]$'
}

function Select-PostConfigAction {
    while ($true) {
        Write-Host ''
        if ($script:Language -eq 'en') {
            Write-Host 'Next step'
            Write-Host '  1) Build locally'
            Write-Host '  2) Prepare an online build request'
            Write-Host '  0) Keep the generated configuration and exit'
            $Answer = Read-Host 'Choose [0]'
        } else {
            Write-Host '下一步'
            Write-Host '  1) 本地编译'
            Write-Host '  2) 准备在线构建请求'
            Write-Host '  0) 保留生成的配置并退出'
            $Answer = Read-Host '请选择 [0]'
        }
        if ([string]::IsNullOrWhiteSpace($Answer)) { return 'config' }
        switch ($Answer.Trim()) {
            '0' { return 'config' }
            'config' { return 'config' }
            '1' { return 'local' }
            'local' { return 'local' }
            'build-local' { return 'local' }
            '2' { return 'cloud' }
            'cloud' { return 'cloud' }
            'online' { return 'cloud' }
            'build-cloud' { return 'cloud' }
            default { [Console]::Error.WriteLine($(if ($script:Language -eq 'en') { 'Invalid next step; please try again.' } else { '无效的下一步选择，请重新输入。' })) }
        }
    }
}

function Invoke-Interactive {
    $Config = New-DefaultConfig
    Select-InteractiveLanguage
    Show-InteractiveTitle
    $Config.BOARD = Select-BoardOrSavedProfile $Config.BOARD
    if ($Config.BOARD.StartsWith('saved:')) {
        $SavedProfile = $Config.BOARD.Substring(6)
        $Config = New-DefaultConfig
        Import-UserConfig $Config (Join-Path (Join-Path $BuildRoot $SavedProfile) 'firmware.env')
        Validate-Config $Config
        Show-InteractiveSummary $Config
        Write-Profile $Config
        Invoke-LocalBuild $Config
        exit $script:BuildExitCode
    }
    if ($Config.BOARD -eq 'custom') {
        Set-CustomBoardConfig $Config
    } else {
        $Board = Get-Record 'board' $Config.BOARD
        $Config.MCU = $Board.MCU
        $Config.DISPLAY_BUS = $Board.DISPLAY_BUS
        $Config.INPUT_BUS = $Board.INPUT_BUS
        $DisplayOptions = @(Get-CatalogIdsMatching 'display' 'BUS' $Board.DISPLAY_BUS) + 'custom'
        if ($DisplayOptions -notcontains $Config.DISPLAY) { $Config.DISPLAY = $DisplayOptions[0] }
        $Config.DISPLAY = Select-CatalogOption 'display' 'Display' $Config.DISPLAY $DisplayOptions
        if ($Config.DISPLAY -eq 'custom') { Read-CustomId $Config 'DISPLAY_NAME' '自定义显示屏名称（ASCII）' 'Custom display name (ASCII)' }
        $InputOptions = @(Get-CatalogIdsMatching 'input' 'BUS' $Board.INPUT_BUS) + 'custom'
        if ($InputOptions -notcontains $Config.INPUT) { $Config.INPUT = $InputOptions[0] }
        $Config.INPUT = Select-CatalogOption 'input' 'Input' $Config.INPUT $InputOptions
        if ($Config.INPUT -eq 'custom') { Read-CustomId $Config 'INPUT_NAME' '自定义输入设备名称（ASCII）' 'Custom input name (ASCII)' }
        $Config.MODULES = Select-ModuleOptions $Config.MODULES
        $Config.DASHBOARDS = Select-DashboardOptions $Config
        Set-MissingBoardGpio $Config
    }
    Set-InteractiveProfileName $Config
    Validate-Config $Config
    Show-InteractiveSummary $Config
    if (-not (Confirm-InteractivePlan)) {
        Write-Host $(if ($script:Language -eq 'en') { 'Configuration canceled.' } else { '已取消生成配置。' })
        return
    }
    switch (Select-PostConfigAction) {
        'config' { Write-Profile $Config; return }
        'local' {
            $PendingBuildRoot = Join-Path $Root ("output/.config-" + [guid]::NewGuid().ToString('N'))
            $OriginalBuildRoot = $script:BuildRoot
            try {
                [void](New-Item -ItemType Directory -Path $PendingBuildRoot -Force)
                $script:BuildRoot = $PendingBuildRoot
                Write-Profile $Config
                Invoke-LocalBuild $Config
            } finally {
                $script:BuildRoot = $OriginalBuildRoot
                Remove-Item -LiteralPath $PendingBuildRoot -Recurse -Force -ErrorAction SilentlyContinue
            }
            if ($script:BuildExitCode -ne 0) { exit $script:BuildExitCode }
            $Save = Read-Host $(if ($script:Language -eq 'en') { 'Save this configuration after building? [y/N]' } else { '编译完成后保存此配置吗？[y/N]' })
            if ($Save -match '^[Yy]$') { Set-InteractiveProfileName $Config -Prompt; Write-Profile $Config }
            exit 0
        }
        'cloud' { Write-Profile $Config; Invoke-CloudBuild $Config; exit 0 }
    }
}

try {
    $Arguments = @(Remove-LanguageOptions $Arguments)
    if ([string]::IsNullOrEmpty($Command)) { Invoke-Interactive; exit 0 }
    switch ($Command) {
        'install-idf' { Install-EspIdf $Arguments; exit 0 }
        'doctor' { if ($Arguments.Count -ne 0) { Fail 'doctor does not accept options' }; Invoke-Doctor; exit 0 }
        'configure' { $Config = New-DefaultConfig; Parse-Options $Config $Arguments; Validate-Config $Config; Write-Profile $Config; exit 0 }
        'validate' { $Config = New-DefaultConfig; Parse-Options $Config $Arguments; Validate-Config $Config; Write-LocalizedOutput "valid: profile=$($Config.PROFILE) modules=$($Config.MODULES) dashboards=$($Config.DASHBOARDS)"; exit 0 }
        'build-local' {
            $Config = New-DefaultConfig; Parse-Options $Config $Arguments; Validate-Config $Config; Write-Profile $Config
            Invoke-LocalBuild $Config
            exit $script:BuildExitCode
        }
        'build-cloud' { $Config = New-DefaultConfig; Parse-Options $Config $Arguments; Validate-Config $Config; Write-Profile $Config; Invoke-CloudBuild $Config; exit 0 }
        'help' { Show-Usage; exit 0 }
        '-h' { Show-Usage; exit 0 }
        '--help' { Show-Usage; exit 0 }
        default { Fail "unknown command: $Command" }
    }
} catch {
    $ErrorDetails = [System.Collections.Generic.List[string]]::new()
    [void]$ErrorDetails.Add($_.Exception.Message)
    if (-not [string]::IsNullOrWhiteSpace($_.InvocationInfo.PositionMessage)) {
        [void]$ErrorDetails.Add($_.InvocationInfo.PositionMessage.Trim())
    }
    if (-not [string]::IsNullOrWhiteSpace($_.ScriptStackTrace)) {
        [void]$ErrorDetails.Add($_.ScriptStackTrace.Trim())
    }
    [Console]::Error.WriteLine(($ErrorDetails -join [Environment]::NewLine))
    exit 2
}
