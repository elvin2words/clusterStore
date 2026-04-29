$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

$steps = @(
    @{
        Name = "TypeScript checks"
        Command = { & cmd /c npm run check }
    },
    @{
        Name = "Node test suite"
        Command = { & cmd /c npm test }
    },
    @{
        Name = "TypeScript build"
        Command = { & cmd /c npm run build }
    },
    @{
        Name = "Firmware binding tests"
        Command = { & cmd /c npm run test:firmware-binding }
    },
    @{
        Name = "Overlay adapter tests"
        Command = { & cmd /c npm run test:overlay-adapter }
    },
    @{
        Name = "Simulation smoke test"
        Command = { & cmd /c npm run sim:smoke }
    },
    @{
        Name = "Daemon stack smoke test"
        Command = { & cmd /c npm run smoke:stack }
    },
    @{
        Name = "Live readiness validation"
        Command = { & cmd /c npm run check:live-readiness }
    },
    @{
        Name = "Firmware environment check"
        Command = { & cmd /c npm run firmware:check }
    },
    @{
        Name = "ARM firmware build"
        Command = { & cmd /c npm run firmware:build:arm }
    }
)

$results = @()

Push-Location $repoRoot
try {
    foreach ($step in $steps) {
        Write-Host ""
        Write-Host ("=== {0} ===" -f $step.Name) -ForegroundColor Cyan

        $started = Get-Date
        try {
            & $step.Command
            $duration = ((Get-Date) - $started).TotalSeconds
            $results += [pscustomobject]@{
                Name = $step.Name
                Status = "PASS"
                Seconds = [Math]::Round($duration, 2)
            }
        } catch {
            $duration = ((Get-Date) - $started).TotalSeconds
            $results += [pscustomobject]@{
                Name = $step.Name
                Status = "FAIL"
                Seconds = [Math]::Round($duration, 2)
            }

            Write-Host ""
            Write-Host ("Audit failed during: {0}" -f $step.Name) -ForegroundColor Red
            throw
        }
    }
} finally {
    Pop-Location
}

Write-Host ""
Write-Host "=== Audit Summary ===" -ForegroundColor Green
$results | Format-Table -AutoSize
