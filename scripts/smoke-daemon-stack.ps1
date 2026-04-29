[CmdletBinding()]
param(
    [string]$WorkspaceRoot = "",
    [switch]$LeaveRunning
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
    $WorkspaceRoot = Split-Path -Parent $PSScriptRoot
}

function Invoke-JsonRequest {
    param(
        [string]$Uri,
        [string]$Method = "GET",
        [object]$Body = $null
    )

    if ($null -eq $Body) {
        return Invoke-RestMethod -Uri $Uri -Method $Method
    }

    return Invoke-RestMethod -Uri $Uri -Method $Method -ContentType "application/json" -Body ($Body | ConvertTo-Json -Depth 10)
}

function Wait-Health {
    param(
        [string]$Uri,
        [string]$Label,
        [System.Diagnostics.Process]$Process = $null,
        [string]$StdoutPath = "",
        [string]$StderrPath = ""
    )

    $lastError = $null
    for ($attempt = 0; $attempt -lt 40; $attempt += 1) {
        if ($null -ne $Process -and $Process.HasExited) {
            $stderr = ""
            if (-not [string]::IsNullOrWhiteSpace($StderrPath) -and (Test-Path -LiteralPath $StderrPath)) {
                $stderr = Get-Content -Path $StderrPath -Raw
            }
            $stdout = ""
            if (-not [string]::IsNullOrWhiteSpace($StdoutPath) -and (Test-Path -LiteralPath $StdoutPath)) {
                $stdout = Get-Content -Path $StdoutPath -Raw
            }

            throw ("{0} exited before becoming healthy. ExitCode={1}`nSTDOUT:`n{2}`nSTDERR:`n{3}" -f $Label, $Process.ExitCode, $stdout.Trim(), $stderr.Trim())
        }

        try {
            return Invoke-JsonRequest -Uri $Uri
        } catch {
            $lastError = $_
            Start-Sleep -Milliseconds 250
        }
    }

    if ($null -ne $lastError) {
        throw $lastError
    }

    throw "Timed out waiting for $Label"
}

function Get-FreeTcpPort {
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $listener.Start()
    try {
        return $listener.LocalEndpoint.Port
    } finally {
        $listener.Stop()
    }
}

function Convert-ToNodeStatus {
    param(
        [int]$NodeAddress,
        [int]$SocPct,
        [int]$PackVoltageMv
    )

    return @{
        nodeAddress = $NodeAddress
        nodeId = ("node-{0:d2}" -f $NodeAddress)
        ratedCapacityKwh = 5
        socPct = $SocPct
        packVoltageMv = $PackVoltageMv
        packCurrentMa = 0
        temperatureDeciC = 250
        faultFlags = @()
        contactorClosed = $true
        readyForConnection = $true
        balancingActive = $false
        maintenanceLockout = $false
        serviceLockout = $false
        heartbeatAgeMs = 0
    }
}

$runRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("clusterstore-daemon-smoke-" + [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null

$brokerMessagesPath = Join-Path $runRoot "mqtt-messages.json"
$brokerStdoutPath = Join-Path $runRoot "broker-stdout.log"
$brokerStderrPath = Join-Path $runRoot "broker-stderr.log"
$emsStdoutPath = Join-Path $runRoot "ems-stdout.log"
$emsStderrPath = Join-Path $runRoot "ems-stderr.log"
$bridgeStdoutPath = Join-Path $runRoot "bridge-stdout.log"
$bridgeStderrPath = Join-Path $runRoot "bridge-stderr.log"

$emsPort = Get-FreeTcpPort
$bridgePort = Get-FreeTcpPort
$brokerPort = Get-FreeTcpPort

$emsConfig = Get-Content (Join-Path $WorkspaceRoot "services\cluster-ems\config\example.daemon.json") -Raw | ConvertFrom-Json
$bridgeConfig = Get-Content (Join-Path $WorkspaceRoot "services\utilitycore-bridge\config\example.daemon.json") -Raw | ConvertFrom-Json

$emsConfig.http.port = $emsPort
$emsConfig.canBus.statusesPath = (Join-Path $runRoot "ems-statuses.json")
$emsConfig.canBus.diagnosticsPath = (Join-Path $runRoot "ems-diagnostics.json")
$emsConfig.canBus.commandsPath = (Join-Path $runRoot "ems-commands.json")
$emsConfig.canBus.commandHistoryPath = (Join-Path $runRoot "ems-command-history.jsonl")
$emsConfig.canBus.isolatesPath = (Join-Path $runRoot "ems-isolates.jsonl")
$emsConfig.inverter.statePath = (Join-Path $runRoot "ems-inverter-state.json")
$emsConfig.inverter.setpointPath = (Join-Path $runRoot "ems-inverter-setpoint.json")
$emsConfig.inverter.setpointHistoryPath = (Join-Path $runRoot "ems-inverter-setpoint-history.jsonl")
$emsConfig.inverter.prechargePath = (Join-Path $runRoot "ems-precharge.jsonl")
$emsConfig.inverter.holdOpenPath = (Join-Path $runRoot "ems-hold-open.jsonl")
$emsConfig.hmi.snapshotPath = (Join-Path $runRoot "ems-hmi-snapshot.json")
$emsConfig.hmi.alertsPath = (Join-Path $runRoot "ems-hmi-alerts.jsonl")
$emsConfig.watchdog.heartbeatPath = (Join-Path $runRoot "ems-watchdog-heartbeat.json")
$emsConfig.watchdog.failSafePath = (Join-Path $runRoot "ems-watchdog-failsafe.jsonl")
$emsConfig.journal.path = (Join-Path $runRoot "ems-journal.jsonl")

$bridgeConfig.http.port = $bridgePort
$bridgeConfig.mqtt.host = "127.0.0.1"
$bridgeConfig.mqtt.port = $brokerPort
$bridgeConfig.emsApi.baseUrl = "http://127.0.0.1:$emsPort"
$bridgeConfig.lte.path = (Join-Path $runRoot "bridge-lte.json")
$bridgeConfig.buffer.path = (Join-Path $runRoot "bridge-buffer.json")
$bridgeConfig.scada.telemetryPath = (Join-Path $runRoot "bridge-scada-telemetry.json")
$bridgeConfig.scada.alertsPath = (Join-Path $runRoot "bridge-scada-alerts.jsonl")
$bridgeConfig.commandLedger.path = (Join-Path $runRoot "bridge-command-ledger.json")
$bridgeConfig.journal.path = (Join-Path $runRoot "bridge-journal.jsonl")

$emsConfigPath = Join-Path $runRoot "ems-config.json"
$bridgeConfigPath = Join-Path $runRoot "bridge-config.json"

@(
    Convert-ToNodeStatus -NodeAddress 1 -SocPct 48 -PackVoltageMv 51200
    Convert-ToNodeStatus -NodeAddress 2 -SocPct 50 -PackVoltageMv 51300
) | ConvertTo-Json -Depth 10 | Set-Content -Path $emsConfig.canBus.statusesPath -Encoding ascii

"[]" | Set-Content -Path $emsConfig.canBus.diagnosticsPath -Encoding ascii

@{
    acInputVoltageV = 230
    acInputFrequencyHz = 50
    acOutputVoltageV = 230
    acOutputFrequencyHz = 50
    acOutputLoadW = 0
    dcBusVoltageV = 51.2
    gridAvailable = $true
    solarGenerationW = 0
    availableChargeCurrentA = 10
    requestedDischargeCurrentA = 0
    exportAllowed = $false
    tariffBand = "normal"
} | ConvertTo-Json -Depth 10 | Set-Content -Path $emsConfig.inverter.statePath -Encoding ascii

@{
    online = $true
    signalRssiDbm = -78
    carrier = "local-test"
    lastHeartbeatAt = [DateTime]::UtcNow.ToString("o")
} | ConvertTo-Json -Depth 10 | Set-Content -Path $bridgeConfig.lte.path -Encoding ascii

$emsConfig | ConvertTo-Json -Depth 20 | Set-Content -Path $emsConfigPath -Encoding ascii
$bridgeConfig | ConvertTo-Json -Depth 20 | Set-Content -Path $bridgeConfigPath -Encoding ascii

$brokerScript = Join-Path $WorkspaceRoot "scripts\fake-mqtt-broker-cli.mjs"
$emsDaemon = Join-Path $WorkspaceRoot "services\cluster-ems\dist\services\cluster-ems\src\daemon.js"
$bridgeDaemon = Join-Path $WorkspaceRoot "services\utilitycore-bridge\dist\services\utilitycore-bridge\src\daemon.js"

$brokerProcess = $null
$emsProcess = $null
$bridgeProcess = $null

try {
    $brokerProcess = Start-Process -FilePath node `
        -ArgumentList @($brokerScript, "--host", "127.0.0.1", "--port", "$brokerPort", "--messages", $brokerMessagesPath) `
        -WorkingDirectory $WorkspaceRoot `
        -RedirectStandardOutput $brokerStdoutPath `
        -RedirectStandardError $brokerStderrPath `
        -PassThru

    $emsProcess = Start-Process -FilePath node `
        -ArgumentList @($emsDaemon, "--config", $emsConfigPath) `
        -WorkingDirectory $WorkspaceRoot `
        -RedirectStandardOutput $emsStdoutPath `
        -RedirectStandardError $emsStderrPath `
        -PassThru

    $bridgeProcess = Start-Process -FilePath node `
        -ArgumentList @($bridgeDaemon, "--config", $bridgeConfigPath) `
        -WorkingDirectory $WorkspaceRoot `
        -RedirectStandardOutput $bridgeStdoutPath `
        -RedirectStandardError $bridgeStderrPath `
        -PassThru

    $emsHealth = Wait-Health -Uri "http://127.0.0.1:$emsPort/health" -Label "EMS health" -Process $emsProcess -StdoutPath $emsStdoutPath -StderrPath $emsStderrPath
    $bridgeHealth = Wait-Health -Uri "http://127.0.0.1:$bridgePort/health" -Label "Bridge health" -Process $bridgeProcess -StdoutPath $bridgeStdoutPath -StderrPath $bridgeStderrPath

    Invoke-JsonRequest -Uri "http://127.0.0.1:$bridgePort/publish-cycle" -Method "POST" | Out-Null
    Start-Sleep -Milliseconds 300

    $messages = @()
    for ($attempt = 0; $attempt -lt 20; $attempt += 1) {
        if (Test-Path -LiteralPath $brokerMessagesPath) {
            $messages = Get-Content -Path $brokerMessagesPath -Raw | ConvertFrom-Json
            if ($messages.Count -gt 0) {
                break
            }
        }
        Start-Sleep -Milliseconds 150
    }

    if ($messages.Count -eq 0) {
        throw "Bridge did not publish any MQTT telemetry during smoke verification"
    }

    $snapshot = Invoke-JsonRequest -Uri "http://127.0.0.1:$emsPort/snapshot"

    [PSCustomObject]@{
        runRoot = $runRoot
        emsHealth = $emsHealth
        bridgeHealth = $bridgeHealth
        mqttMessages = $messages.Count
        clusterMode = $snapshot.clusterMode
        freshNodeCount = $snapshot.freshNodeCount
    } | ConvertTo-Json -Depth 20
} finally {
    if (-not $LeaveRunning) {
        foreach ($proc in @($bridgeProcess, $emsProcess, $brokerProcess)) {
            if ($null -ne $proc -and -not $proc.HasExited) {
                Stop-Process -Id $proc.Id -Force
            }
        }
    }
}
