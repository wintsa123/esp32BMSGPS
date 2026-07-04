[CmdletBinding()]
param(
    [string]$Port,
    [ValidateSet("debug", "release")]
    [string]$Profile = "release",
    [string]$ManifestUrl,
    [switch]$Monitor,
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$target = "xtensa-esp32-none-elf"
$binaryName = "esp32-bms-gps"
$partitionTable = Join-Path $repoRoot "partitions.csv"
$firmwarePath = Join-Path $repoRoot "target\$target\$Profile\$binaryName"
$linkerFlags = "-C link-arg=-nostartfiles -C link-arg=-Tlinkall.x"

function Test-CommandExists {
    param([Parameter(Mandatory = $true)][string]$Name)

    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command '$Name' was not found in PATH."
    }
}

function Resolve-FlashPort {
    param([string]$RequestedPort)

    if ($RequestedPort) {
        return $RequestedPort.ToUpperInvariant()
    }

    $ports = @(Get-CimInstance Win32_SerialPort | Where-Object {
        $_.DeviceID -match "^COM\d+$" -and $_.Name -match "USB|CH340|CP210|Serial|UART"
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

Test-CommandExists cargo
Test-CommandExists espflash

$resolvedPort = Resolve-FlashPort -RequestedPort $Port
$originalRustflags = $env:RUSTFLAGS
$hadRustflags = Test-Path Env:RUSTFLAGS
$hadManifestUrl = Test-Path Env:OTA_MANIFEST_URL
$originalManifestUrl = $env:OTA_MANIFEST_URL

try {
    $env:RUSTFLAGS = $linkerFlags
    if ($PSBoundParameters.ContainsKey("ManifestUrl")) {
        $env:OTA_MANIFEST_URL = $ManifestUrl
    }

    if (-not $NoBuild) {
        $buildArgs = @("+esp", "build", "-Zbuild-std=core", "--target", $target)
        if ($Profile -eq "release") {
            $buildArgs += "--release"
        }

        Invoke-Checked -Step "Building firmware for $target ($Profile)" -Action {
            & cargo @buildArgs
        }
    }

    if (-not (Test-Path $firmwarePath)) {
        throw "Firmware image not found at '$firmwarePath'. Build first or remove -NoBuild."
    }

    $flashArgs = @("flash", "-p", $resolvedPort, "--partition-table", $partitionTable)
    if ($Monitor) {
        $flashArgs += "--monitor"
    }
    $flashArgs += $firmwarePath

    Invoke-Checked -Step "Flashing $firmwarePath to $resolvedPort" -Action {
        & espflash @flashArgs
    }
}
finally {
    if ($hadRustflags) {
        $env:RUSTFLAGS = $originalRustflags
    }
    else {
        Remove-Item Env:RUSTFLAGS -ErrorAction SilentlyContinue
    }

    if ($hadManifestUrl) {
        $env:OTA_MANIFEST_URL = $originalManifestUrl
    }
    else {
        Remove-Item Env:OTA_MANIFEST_URL -ErrorAction SilentlyContinue
    }
}
