[CmdletBinding()]
param(
    [string]$PortName = "COM6",
    [int]$ListenPort = 4000,
    [string]$AllowedRemote = "192.168.2.108",
    [string]$IdfPythonEnv,
    [switch]$VerboseRfc2217
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "esp-idf-version.ps1")

function Initialize-IdfEnvironment {
    $idfRoots = @()
    if (-not [string]::IsNullOrWhiteSpace($env:IDF_PATH)) {
        $idfRoots += $env:IDF_PATH
    }
    foreach ($scope in @("User", "Machine")) {
        $persistedIdfPath = [Environment]::GetEnvironmentVariable("IDF_PATH", $scope)
        if (-not [string]::IsNullOrWhiteSpace($persistedIdfPath)) {
            $idfRoots += $persistedIdfPath
        }
    }
    if (-not [string]::IsNullOrWhiteSpace($env:SystemDrive)) {
        $idfRoots += (Join-Path $env:SystemDrive "esp\esp-idf-v6.0.2")
    }
    if (-not [string]::IsNullOrWhiteSpace($env:USERPROFILE)) {
        $idfRoots += (Join-Path $env:USERPROFILE "esp\esp-idf-v6.0.2")
    }

    foreach ($idfRoot in ($idfRoots | Select-Object -Unique)) {
        if (-not (Test-EspIdfV602Root $idfRoot)) { continue }
        $candidate = Join-Path $idfRoot "export.ps1"
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            . ([string]$candidate)
            return
        }
    }

    throw "ESP-IDF v6.0.2 export.ps1 was not found. Set IDF_PATH to an ESP-IDF v6.0.2 installation or pass -IdfPythonEnv."
}

function Find-Rfc2217Server {
    param([string]$PythonEnv)

    if ($PythonEnv) {
        $envServer = Join-Path $PythonEnv "Scripts\esp_rfc2217_server.exe"
        if (Test-Path $envServer) {
            return (Resolve-Path $envServer).Path
        }
    }

    $pathServer = Get-Command esp_rfc2217_server.exe -ErrorAction SilentlyContinue

    $pythonEnvRoots = @()
    if (-not [string]::IsNullOrWhiteSpace($env:IDF_TOOLS_PATH)) {
        $pythonEnvRoots += (Join-Path $env:IDF_TOOLS_PATH "python_env")
    }
    if (-not [string]::IsNullOrWhiteSpace($env:SystemDrive)) {
        $pythonEnvRoots += (Join-Path $env:SystemDrive "esp\esp-idf-tools\python_env")
    }
    if (-not [string]::IsNullOrWhiteSpace($env:USERPROFILE)) {
        $pythonEnvRoots += (Join-Path $env:USERPROFILE ".espressif\python_env")
    }

    $discoveredServers = @(
        foreach ($pythonEnvRoot in ($pythonEnvRoots | Select-Object -Unique)) {
            Get-ChildItem -Path $pythonEnvRoot -Filter "esp_rfc2217_server.exe" -Recurse -File -ErrorAction SilentlyContinue
        }
    )
    $v6Server = $discoveredServers | Where-Object { $_.FullName -match '(?i)\\idf6\.0_' } | Select-Object -First 1
    if ($v6Server) {
        return $v6Server.FullName
    }
    if ($pathServer) {
        return $pathServer.Source
    }
    if ($discoveredServers.Count -gt 0) {
        return $discoveredServers[0].FullName
    }

    return $null
}

function Resolve-Rfc2217Server {
    param([string]$PythonEnv)

    $server = Find-Rfc2217Server -PythonEnv $PythonEnv
    if ($server) { return $server }
    throw "esp_rfc2217_server.exe was not found after loading ESP-IDF. Set IDF_PATH, install ESP-IDF 6.0.2 under $env:SystemDrive\esp, or pass -IdfPythonEnv."
}

function Test-FirewallRuleScope {
    param(
        [Parameter(Mandatory = $true)][int]$Port,
        [Parameter(Mandatory = $true)][string]$RemoteIp
    )

    $matchingRules = @(
        foreach ($rule in (Get-NetFirewallRule -DisplayName "ESP_COM*_TCP_Bridge_*" -ErrorAction SilentlyContinue)) {
            if ($rule.Direction -ne "Inbound" -or $rule.Action -ne "Allow" -or $rule.Enabled -ne "True") {
                continue
            }
            $portFilter = $rule | Get-NetFirewallPortFilter
            $addressFilter = $rule | Get-NetFirewallAddressFilter
            $hasPort = @($portFilter.LocalPort | ForEach-Object { [string]$_ }) -contains [string]$Port
            $hasRemote = @($addressFilter.RemoteAddress | ForEach-Object { [string]$_ }) -contains $RemoteIp
            if ($hasPort -and $hasRemote) {
                $rule.DisplayName
            }
        }
    )

    if ($matchingRules.Count -eq 0) {
        Write-Warning "No enabled ESP_COM*_TCP_Bridge_* firewall rule matches port $Port and remote $RemoteIp. The RFC2217 server itself has no authentication."
        return
    }

    Write-Verbose "Firewall rule matched: $($matchingRules -join ', ')"
}

function Test-UsbSerialJtagPort {
    param([Parameter(Mandatory = $true)][string]$SerialPort)

    $portPattern = "\(" + [regex]::Escape($SerialPort) + "\)"
    $device = Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
        Where-Object {
            $_.Name -match $portPattern -and
            $_.PNPDeviceID -match '(?i)^USB\\VID_303A&PID_1001'
        } |
        Select-Object -First 1
    return [bool]$device
}

$server = $null
if ($IdfPythonEnv) {
    $explicitServer = Join-Path $IdfPythonEnv "Scripts\esp_rfc2217_server.exe"
    if (Test-Path -LiteralPath $explicitServer -PathType Leaf) {
        $server = (Resolve-Path -LiteralPath $explicitServer).Path
    }
}
if (-not $server) {
    $server = Find-Rfc2217Server -PythonEnv $IdfPythonEnv
}
if (-not $server) {
    Initialize-IdfEnvironment
    $server = Resolve-Rfc2217Server -PythonEnv $IdfPythonEnv
}
Test-FirewallRuleScope -Port $ListenPort -RemoteIp $AllowedRemote

$serverArgs = @("-p", [string]$ListenPort)
if ($VerboseRfc2217) {
    $serverArgs += "-v"
}
$serverArgs += $PortName

$isUsbSerialJtag = Test-UsbSerialJtagPort -SerialPort $PortName
$launchTarget = $server
$launchArgs = $serverArgs
$bridgeMode = "ESP UART"
$clientUrl = "rfc2217://192.168.2.10:${ListenPort}?ign_set_control"
if ($isUsbSerialJtag) {
    $serverPython = Join-Path (Split-Path -Parent $server) "python.exe"
    $usbBridge = Join-Path $PSScriptRoot "esp_rfc2217_usb_bridge.py"
    if (-not (Test-Path -LiteralPath $serverPython -PathType Leaf)) {
        throw "ESP-IDF Python next to $server was not found."
    }
    if (-not (Test-Path -LiteralPath $usbBridge -PathType Leaf)) {
        throw "USB Serial/JTAG bridge adapter was not found: $usbBridge"
    }
    $launchTarget = $serverPython
    $launchArgs = @($usbBridge) + $serverArgs
    $bridgeMode = "ESP32-S3 USB Serial/JTAG"
    $clientUrl = "rfc2217://192.168.2.10:${ListenPort}?ign_set_control&timeout=10"
}

Write-Host "Starting ESP RFC2217 serial bridge:"
Write-Host "  serial:  $PortName"
Write-Host "  mode:    $bridgeMode"
Write-Host "  listen:  0.0.0.0:$ListenPort"
Write-Host "  allowed: $AllowedRemote/32 via Windows Firewall"
Write-Host "  client:  $clientUrl (project endpoint)"
# ESP-IDF 后续可能显示 vgate0 的 172.* 地址；实际监听仍覆盖所有 IPv4 接口。
Write-Host "  note:    Ignore ESP-IDF's auto-detected 172.* URL; use the project endpoint above."
Write-Host ""

& $launchTarget @launchArgs
