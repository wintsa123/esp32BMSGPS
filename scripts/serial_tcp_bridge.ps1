[CmdletBinding()]
param(
    [string]$PortName = "COM3",
    [int]$ListenPort = 4000,
    [string]$AllowedRemote = "192.168.2.108",
    [string]$IdfPythonEnv = "$env:USERPROFILE\.espressif\python_env\idf5.5_py3.10_env",
    [switch]$VerboseRfc2217
)

$ErrorActionPreference = "Stop"

function Resolve-Rfc2217Server {
    param([Parameter(Mandatory = $true)][string]$PythonEnv)

    $envServer = Join-Path $PythonEnv "Scripts\esp_rfc2217_server.exe"
    if (Test-Path $envServer) {
        return (Resolve-Path $envServer).Path
    }

    $pathServer = Get-Command esp_rfc2217_server.exe -ErrorAction SilentlyContinue
    if ($pathServer) {
        return $pathServer.Source
    }

    throw "esp_rfc2217_server.exe was not found. Load ESP-IDF tools or pass -IdfPythonEnv."
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

$server = Resolve-Rfc2217Server -PythonEnv $IdfPythonEnv
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
