[CmdletBinding()]
param(
    [string]$Port,
    [int]$BaudRate = 0,
    [switch]$Monitor,
    [string]$BuildDir
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$idfExportScript = Join-Path $env:USERPROFILE "esp\esp-idf-v6.0.2\export.ps1"
$requiredIdfVersion = "ESP-IDF v6.0.2"
$originalLocation = Get-Location

function Set-DefaultProxyEnv {
    if (-not (Test-Path Env:https_proxy)) {
        $env:https_proxy = "http://127.0.0.1:7897"
    }
    if (-not (Test-Path Env:http_proxy)) {
        $env:http_proxy = "http://127.0.0.1:7897"
    }
    if (-not (Test-Path Env:all_proxy)) {
        $env:all_proxy = "socks5://127.0.0.1:7897"
    }
}

function Test-CommandExists {
    param([Parameter(Mandatory = $true)][string]$Name)

    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command '$Name' was not found in PATH."
    }
}

function Test-RequiredIdfVersion {
    $version = ((& idf.py --version 2>&1) | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $version -ne $requiredIdfVersion) {
        throw "Unsupported ESP-IDF version: expected $requiredIdfVersion, got $version."
    }
}

function Resolve-FlashPort {
    param([string]$RequestedPort)

    if ($RequestedPort) {
        if ($RequestedPort -match "^[A-Za-z][A-Za-z0-9+.-]*://") {
            return $RequestedPort
        }
        return $RequestedPort.ToUpperInvariant()
    }

    $serialPorts = @(Get-CimInstance Win32_SerialPort | ForEach-Object {
        $deviceId = [string]$_.DeviceID
        $name = [string]$_.Name
        if ($deviceId -match "^COM\d+$" -and $name -match "USB|CH340|CP210|FTDI|WCH|Serial|UART") {
            [PSCustomObject]@{
                DeviceID = $deviceId.ToUpperInvariant()
                Name = $name
            }
        }
    })

    $pnpPorts = @(Get-CimInstance Win32_PnPEntity | ForEach-Object {
        $name = [string]$_.Name
        $comMatch = [regex]::Match($name, "(COM\d+)")
        if ($comMatch.Success -and $name -match "USB|CH340|CP210|FTDI|WCH|Serial|UART") {
            [PSCustomObject]@{
                DeviceID = $comMatch.Groups[1].Value.ToUpperInvariant()
                Name = $name
            }
        }
    })

    $ports = @($serialPorts + $pnpPorts | Group-Object DeviceID | ForEach-Object {
        $_.Group[0]
    })

    if ($ports.Count -eq 0) {
        throw "No USB serial device was found. Connect the ESP32 board or pass -Port COMx."
    }

    $preferred = @($ports | Where-Object { $_.Name -match "CH340|CP210|USB-SERIAL|USB Serial" })
    if ($preferred.Count -eq 1) {
        return $preferred[0].DeviceID
    }

    if ($ports.Count -eq 1) {
        return $ports[0].DeviceID
    }

    $portList = ($ports | ForEach-Object { "$($_.DeviceID) [$($_.Name)]" }) -join ", "
    throw "Multiple serial devices were found: $portList. Pass -Port COMx explicitly."
}

function Resolve-BuildDirectory {
    param([string]$RequestedBuildDir)

    if ([string]::IsNullOrWhiteSpace($RequestedBuildDir)) {
        return $null
    }

    $candidate = if ([System.IO.Path]::IsPathRooted($RequestedBuildDir)) {
        $RequestedBuildDir
    } else {
        Join-Path $repoRoot $RequestedBuildDir
    }
    if (-not (Test-Path -LiteralPath $candidate -PathType Container)) {
        throw "Build directory was not found: $candidate"
    }
    return (Resolve-Path -LiteralPath $candidate).Path
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$Step,
        [Parameter(Mandatory = $true)][scriptblock]$Action
    )

    Write-Host "==> $Step" -ForegroundColor Cyan
    & $Action
    if ($LASTEXITCODE -ne 0) {
        throw "$Step failed with exit code $LASTEXITCODE."
    }
}

function Initialize-IdfEnvironment {
    $candidates = @()
    if (Test-Path Env:IDF_PATH) {
        $candidates += (Join-Path $env:IDF_PATH "export.ps1")
    }
    $candidates += $idfExportScript

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            . $candidate
            return
        }
    }
}

try {
    Push-Location $repoRoot
    Set-DefaultProxyEnv
    Initialize-IdfEnvironment
    Test-CommandExists idf.py
    Test-RequiredIdfVersion

    $resolvedPort = Resolve-FlashPort -RequestedPort $Port
    $resolvedBuildDir = Resolve-BuildDirectory -RequestedBuildDir $BuildDir
    $effectiveBaudRate = $BaudRate
    if ($effectiveBaudRate -le 0 -and $resolvedPort -match "(?i)^socket://") {
        $effectiveBaudRate = 115200
        Write-Host "==> Raw socket serial selected; using -b $effectiveBaudRate to match the bridge baud rate" -ForegroundColor DarkGray
    }

    $idfArgs = @()
    if ($null -ne $resolvedBuildDir) {
        $idfArgs += @("-B", $resolvedBuildDir)
    }
    $idfArgs += @("-p", $resolvedPort)
    if ($effectiveBaudRate -gt 0) {
        $idfArgs += @("-b", "$effectiveBaudRate")
    }
    $idfArgs += "flash"
    if ($Monitor) {
        $idfArgs += "monitor"
    }

    Invoke-Checked -Step "Flashing ESP-IDF app to $resolvedPort" -Action {
        & idf.py @idfArgs
    }
}
finally {
    Set-Location $originalLocation
}
