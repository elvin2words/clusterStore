param(
    [switch]$CheckOnly
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Resolve-MosquittoExe {
    $candidates = @()

    if ($env:CLUSTERSTORE_MOSQUITTO_EXE) {
        $candidates += $env:CLUSTERSTORE_MOSQUITTO_EXE
    }

    try {
        $command = Get-Command mosquitto.exe -ErrorAction Stop
        if ($command.Path) {
            $candidates += $command.Path
        }
    } catch {
    }

    $candidates += @(
        "C:\Program Files\mosquitto\mosquitto.exe",
        "C:\Program Files (x86)\mosquitto\mosquitto.exe"
    )

    foreach ($candidate in $candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path -LiteralPath $candidate)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    return $null
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$configPath = (Resolve-Path (Join-Path $repoRoot "services\utilitycore-bridge\config\mosquitto\local-dev.conf")).Path
$mosquittoExe = Resolve-MosquittoExe

if (-not $mosquittoExe) {
    throw "Mosquitto was not found. Install mosquitto.exe or set CLUSTERSTORE_MOSQUITTO_EXE to its full path."
}

if ($CheckOnly) {
    [pscustomobject]@{
        mosquittoExe = $mosquittoExe
        configPath = $configPath
        bridgeConfig = (Resolve-Path (Join-Path $repoRoot "services\utilitycore-bridge\config\example.daemon.json")).Path
        note = "example.daemon.json already points the bridge at 127.0.0.1:1883 for local development."
    } | ConvertTo-Json -Depth 3
    exit 0
}

Write-Host ("Starting local Mosquitto with config '{0}'." -f $configPath) -ForegroundColor Cyan
Write-Host "Use the existing bridge config at services/utilitycore-bridge/config/example.daemon.json." -ForegroundColor Cyan

& $mosquittoExe -c $configPath -v
