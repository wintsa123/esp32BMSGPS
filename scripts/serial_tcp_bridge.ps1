[CmdletBinding()]
param(
    [string]$PortName = "COM3",
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
    $idfRoots += (Join-Path $env:USERPROFILE "esp\esp-idf-v6.0.2")

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
    if ($pathServer) {
        return $pathServer.Source
    }

    $pythonEnvRoot = Join-Path $env:USERPROFILE ".espressif\python_env"
    $discoveredServer = Get-ChildItem -Path $pythonEnvRoot -Filter "esp_rfc2217_server.exe" -Recurse -File -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($discoveredServer) {
        return $discoveredServer.FullName
    }

    return $null
}

function Resolve-Rfc2217Server {
    param([string]$PythonEnv)

    $server = Find-Rfc2217Server -PythonEnv $PythonEnv
    if ($server) { return $server }
    throw "esp_rfc2217_server.exe was not found after loading ESP-IDF. Set IDF_PATH, install ESP-IDF 6.0.2 under $env:USERPROFILE\esp, or pass -IdfPythonEnv."
}

function Test-FirewallRuleScope {
    param(
        [Parameter(Mandatory = $true)][int]$Port,
        [Parameter(Mandatory = $true)][string]$RemoteIp
    )

    $ruleText = & netsh advfirewall firewall show rule name="ESP_COM3_TCP_Bridge_4000" 2>$null
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Firewall rule ESP_COM3_TCP_Bridge_4000 was not found. The RFC2217 server itself has no authentication."
        return
    }

    $hasPort = $ruleText -match "LocalPort:\s+$Port"
    $hasRemote = $ruleText -match [regex]::Escape("$RemoteIp/32")
    if (-not ($hasPort -and $hasRemote)) {
        Write-Warning "Firewall rule exists but does not match port $Port and remote $RemoteIp/32. Check access scope before using this on an untrusted LAN."
    }
}

$server = Find-Rfc2217Server -PythonEnv $IdfPythonEnv
if (-not $server) {
    Initialize-IdfEnvironment
    $server = Resolve-Rfc2217Server -PythonEnv $IdfPythonEnv
}
Test-FirewallRuleScope -Port $ListenPort -RemoteIp $AllowedRemote

$args = @("-p", [string]$ListenPort)
if ($VerboseRfc2217) {
    $args += "-v"
}
$args += $PortName

Write-Host "Starting ESP RFC2217 serial bridge:"
Write-Host "  serial:  $PortName"
Write-Host "  listen:  0.0.0.0:$ListenPort"
Write-Host "  allowed: $AllowedRemote/32 via Windows Firewall"
Write-Host "  client:  rfc2217://192.168.2.10:${ListenPort}?ign_set_control"
Write-Host ""

& $server @args
