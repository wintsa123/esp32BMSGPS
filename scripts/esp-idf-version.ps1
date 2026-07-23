function Test-EspIdfV602Root([string]$IdfRoot) {
    if ([string]::IsNullOrWhiteSpace($IdfRoot)) { return $false }

    $VersionHeader = Join-Path $IdfRoot 'components\esp_common\include\esp_idf_version.h'
    if (Test-Path -LiteralPath $VersionHeader -PathType Leaf) {
        try {
            $VersionText = Get-Content -LiteralPath $VersionHeader -Raw -Encoding UTF8
            return (
                $VersionText -match '(?m)^\s*#define\s+ESP_IDF_VERSION_MAJOR\s+6\b' -and
                $VersionText -match '(?m)^\s*#define\s+ESP_IDF_VERSION_MINOR\s+0\b' -and
                $VersionText -match '(?m)^\s*#define\s+ESP_IDF_VERSION_PATCH\s+2\b'
            )
        } catch {
            return $false
        }
    }

    return (Split-Path -Leaf $IdfRoot) -eq 'esp-idf-v6.0.2'
}
