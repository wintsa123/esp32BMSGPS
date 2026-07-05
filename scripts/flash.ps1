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
$buildStateDir = Join-Path $repoRoot "target\$target\$Profile"

function Test-CommandExists {
    param([Parameter(Mandatory = $true)][string]$Name)

    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command '$Name' was not found in PATH."
    }
}

function Initialize-Python3Shim {
    param([Parameter(Mandatory = $true)][string]$RepoRoot)

    $shimDir = Join-Path $RepoRoot "target\tool-shims"
    New-Item -ItemType Directory -Force -Path $shimDir | Out-Null

    $shimPath = Join-Path $shimDir "python3.exe"
    $launcher = Get-Command py.exe -ErrorAction SilentlyContinue
    if ($launcher) {
        Copy-Item -LiteralPath $launcher.Source -Destination $shimPath -Force
    }
    else {
        $python = Get-Command python.exe -ErrorAction SilentlyContinue
        if (-not $python) {
            throw "Required command 'python3' was not found, and neither 'py.exe' nor 'python.exe' is available for a shim."
        }
        Copy-Item -LiteralPath $python.Source -Destination $shimPath -Force
    }

    & $shimPath --version | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Prepared python3 shim failed to run at '$shimPath'."
    }

    $shimDir
}

function Resolve-FlashPort {
    param([string]$RequestedPort)

    if ($RequestedPort) {
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

function Remove-BuildArtifact {
    param(
        [Parameter(Mandatory = $true)][System.IO.FileSystemInfo]$Item,
        [Parameter(Mandatory = $true)][string]$StateDir
    )

    $trimChars = @([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)
    $statePath = [System.IO.Path]::GetFullPath($StateDir).TrimEnd($trimChars)
    $itemPath = [System.IO.Path]::GetFullPath($Item.FullName)
    $allowedPrefix = $statePath + [System.IO.Path]::DirectorySeparatorChar

    if (-not $itemPath.StartsWith($allowedPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove build artifact outside '$statePath': '$itemPath'."
    }

    Remove-Item -LiteralPath $itemPath -Recurse -Force
}

function Clear-LocalCrateArtifacts {
    param(
        [Parameter(Mandatory = $true)][string]$StateDir,
        [Parameter(Mandatory = $true)][string]$BinaryName
    )

    if (-not (Test-Path $StateDir)) {
        return 0
    }

    $crateFilePrefix = $BinaryName.Replace("-", "_")
    $items = @()

    foreach ($name in @(
        $BinaryName,
        "$BinaryName.d",
        "lib$crateFilePrefix.rlib",
        "lib$crateFilePrefix.d"
    )) {
        $path = Join-Path $StateDir $name
        if (Test-Path $path) {
            $items += @(Get-Item -LiteralPath $path -Force)
        }
    }

    $depsDir = Join-Path $StateDir "deps"
    if (Test-Path $depsDir) {
        $items += @(Get-ChildItem -LiteralPath $depsDir -Force -File | Where-Object {
            $_.Name -like "$crateFilePrefix-*" -or $_.Name -like "lib$crateFilePrefix-*"
        })
    }

    $fingerprintDir = Join-Path $StateDir ".fingerprint"
    if (Test-Path $fingerprintDir) {
        $items += @(Get-ChildItem -LiteralPath $fingerprintDir -Force -Directory | Where-Object {
            $_.Name -like "$BinaryName-*"
        })
    }

    $incrementalDir = Join-Path $StateDir "incremental"
    if (Test-Path $incrementalDir) {
        $items += @(Get-ChildItem -LiteralPath $incrementalDir -Force -Directory | Where-Object {
            $_.Name -like "$crateFilePrefix-*"
        })
    }

    $removed = 0
    foreach ($item in @($items | Sort-Object FullName -Unique)) {
        Remove-BuildArtifact -Item $item -StateDir $StateDir
        $removed += 1
    }

    $removed
}

Test-CommandExists cargo
Test-CommandExists espflash

$resolvedPort = Resolve-FlashPort -RequestedPort $Port
$hadManifestUrl = Test-Path Env:OTA_MANIFEST_URL
$originalManifestUrl = $env:OTA_MANIFEST_URL
$originalPath = $env:PATH
$originalLocation = Get-Location

try {
    Push-Location $repoRoot
    $pythonShimDir = Initialize-Python3Shim -RepoRoot $repoRoot
    $env:PATH = "$pythonShimDir;$repoRoot\scripts;$env:PATH"

    if ($PSBoundParameters.ContainsKey("ManifestUrl")) {
        $env:OTA_MANIFEST_URL = $ManifestUrl
    }

    if (-not $NoBuild) {
        $removedArtifactCount = Clear-LocalCrateArtifacts -StateDir $buildStateDir -BinaryName $binaryName
        Write-Host "==> Forcing local crate rebuild; removed $removedArtifactCount artifact(s)" -ForegroundColor Cyan

        $buildArgs = @("+esp", "build", "--target", $target)
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

    $firmwareInfo = Get-Item -LiteralPath $firmwarePath
    Write-Host ("==> Firmware timestamp: {0:u}" -f $firmwareInfo.LastWriteTimeUtc) -ForegroundColor DarkGray

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
    Set-Location $originalLocation

    if ($hadManifestUrl) {
        $env:OTA_MANIFEST_URL = $originalManifestUrl
    }
    else {
        Remove-Item Env:OTA_MANIFEST_URL -ErrorAction SilentlyContinue
    }
    $env:PATH = $originalPath
}
