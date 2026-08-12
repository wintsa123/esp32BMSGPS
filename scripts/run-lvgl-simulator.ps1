[CmdletBinding()]
param(
    [string]$SDL2Root = $env:SDL2_ROOT,
    [string]$BuildDir,
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release',
    [switch]$HostBle,
    [switch]$NoHostBle,
    [ValidateRange(1, 65535)]
    [int]$BlePort = 8765,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$SimulatorArgs
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot

if ($HostBle -and $NoHostBle) {
    throw '-HostBle and -NoHostBle cannot be used together.'
}
$enableHostBle = -not $NoHostBle

function Test-Sdl2Root {
    param([string]$Path)

    if (-not $Path -or -not (Test-Path -LiteralPath $Path -PathType Container)) {
        return $false
    }
    $configCandidates = @(
        (Join-Path $Path 'cmake\SDL2Config.cmake'),
        (Join-Path $Path 'cmake\sdl2-config.cmake'),
        (Join-Path $Path 'lib\cmake\SDL2\SDL2Config.cmake'),
        (Join-Path $Path 'lib\cmake\SDL2\sdl2-config.cmake'),
        (Join-Path $Path 'x86_64-w64-mingw32\lib\cmake\SDL2\SDL2Config.cmake'),
        (Join-Path $Path 'x86_64-w64-mingw32\lib\cmake\SDL2\sdl2-config.cmake')
    )
    return [bool]($configCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1)
}

function Find-Sdl2Root {
    param([string]$RequestedRoot)

    $candidates = [System.Collections.Generic.List[string]]::new()
    foreach ($candidate in @(
        $RequestedRoot,
        $env:SDL2_ROOT,
        (Join-Path $repoRoot '.deps\SDL2'),
        (Join-Path $repoRoot 'third_party\SDL2'),
        'C:\SDK\SDL2',
        'C:\SDL2'
    )) {
        if ($candidate) {
            $candidates.Add($candidate)
        }
    }

    Get-ChildItem -LiteralPath $repoRoot -Directory -Filter 'build-simulator-*' -ErrorAction SilentlyContinue |
        ForEach-Object {
            $cache = Join-Path $_.FullName 'CMakeCache.txt'
            if (Test-Path -LiteralPath $cache) {
                $rootLine = Get-Content -LiteralPath $cache -ErrorAction SilentlyContinue |
                    Where-Object { $_ -match '^SDL2_ROOT:[^=]*=(.+)$' } |
                    Select-Object -First 1
                if ($rootLine -and $rootLine -match '^SDL2_ROOT:[^=]*=(.+)$') {
                    $candidates.Add($Matches[1])
                }
                $dirLine = Get-Content -LiteralPath $cache -ErrorAction SilentlyContinue |
                    Where-Object { $_ -match '^SDL2_DIR:[^=]*=(.+)$' } |
                    Select-Object -First 1
                if ($dirLine -and $dirLine -match '^SDL2_DIR:[^=]*=(.+)$' -and
                    $Matches[1] -notmatch 'NOTFOUND$') {
                    $candidates.Add((Split-Path -Parent $Matches[1]))
                }
            }
        }

    $tempDependencies = Join-Path $env:LOCALAPPDATA 'Temp\esp-simulator-mingw-agent-deps'
    Get-ChildItem -LiteralPath $tempDependencies -Directory -Filter 'SDL2-*' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        ForEach-Object { $candidates.Add($_.FullName) }

    foreach ($candidate in $candidates) {
        if (Test-Sdl2Root $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    return $null
}

$useDefaultBuildDir = -not $BuildDir
if ($useDefaultBuildDir) {
    $BuildDir = Join-Path $repoRoot 'build-simulator-windows-mingw'
}

$SDL2Root = Find-Sdl2Root $SDL2Root
if (-not $SDL2Root) {
    throw "SDL2 development files were not found. Extract SDL2 to C:\SDK\SDL2 or pass -SDL2Root."
}

Write-Host "Using SDL2: $SDL2Root"

$configureArgs = @(
    '-S', (Join-Path $repoRoot 'simulator'),
    '-B', $BuildDir,
    "-DSDL2_ROOT=$SDL2Root"
)
if ($useDefaultBuildDir) {
    foreach ($tool in @('ninja', 'gcc', 'g++')) {
        if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
            throw "$tool was not found. Install MinGW-w64 and Ninja, then add them to PATH."
        }
    }
    $configureArgs += @(
        '-G', 'Ninja',
        '-DCMAKE_C_COMPILER=gcc',
        '-DCMAKE_CXX_COMPILER=g++'
    )
}

$runtimeCandidates = @(
    (Join-Path $SDL2Root 'x86_64-w64-mingw32\bin'),
    (Join-Path $SDL2Root 'bin'),
    (Join-Path $SDL2Root 'lib\x64')
)
$runtimeBin = $runtimeCandidates |
    Where-Object { Test-Path -LiteralPath (Join-Path $_ 'SDL2.dll') } |
    Select-Object -First 1
if (-not $runtimeBin) {
    throw "SDL2.dll was not found under SDL2_ROOT: $SDL2Root"
}
$env:Path = "$runtimeBin;$env:Path"

& cmake @configureArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& cmake --build $BuildDir --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$candidates = @(
    (Join-Path $BuildDir "$Configuration\esp_bms_lvgl_simulator.exe"),
    (Join-Path $BuildDir 'esp_bms_lvgl_simulator.exe')
)
$simulator = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $simulator) {
    throw "The build succeeded, but esp_bms_lvgl_simulator.exe was not found in $BuildDir."
}

$bleBridge = $null
$previousBleEndpoint = $env:ESP_BMS_SIMULATOR_BLE_ENDPOINT
try {
    if ($enableHostBle) {
        & py -3 -c 'import bleak' 2>$null
        if ($LASTEXITCODE -ne 0) {
            throw 'Python package bleak is missing. Run: py -3 -m pip install bleak'
        }

        $bridgeScript = Join-Path $repoRoot 'simulator\ble_host_bridge.py'
        $bleBridge = Start-Process `
            -FilePath 'py' `
            -ArgumentList @('-3', $bridgeScript, '--port', $BlePort) `
            -PassThru `
            -WindowStyle Hidden

        $ready = $false
        $deadline = [DateTime]::UtcNow.AddSeconds(8)
        while ([DateTime]::UtcNow -lt $deadline -and -not $bleBridge.HasExited) {
            $probe = [System.Net.Sockets.TcpClient]::new()
            try {
                $probe.Connect('127.0.0.1', $BlePort)
                $ready = $true
                break
            } catch {
                Start-Sleep -Milliseconds 100
            } finally {
                $probe.Dispose()
            }
        }
        if (-not $ready) {
            throw "The BLE bridge did not start on 127.0.0.1:$BlePort."
        }
        $env:ESP_BMS_SIMULATOR_BLE_ENDPOINT = "127.0.0.1:$BlePort"
    }

    $startArguments = @{
        FilePath = $simulator
        Wait = $true
        PassThru = $true
        NoNewWindow = $true
    }
    if ($SimulatorArgs) {
        $startArguments.ArgumentList = $SimulatorArgs
    }
    $process = Start-Process @startArguments
    exit $process.ExitCode
} finally {
    if ($bleBridge -and -not $bleBridge.HasExited) {
        Stop-Process -Id $bleBridge.Id
    }
    $env:ESP_BMS_SIMULATOR_BLE_ENDPOINT = $previousBleEndpoint
}
