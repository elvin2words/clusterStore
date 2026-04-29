[CmdletBinding()]
param(
    [string]$WorkspaceRoot = "",
    [switch]$SkipSmoke,
    [switch]$SkipHostConfigure
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
    $scriptPath = $PSCommandPath
    if ([string]::IsNullOrWhiteSpace($scriptPath)) {
        $scriptPath = $MyInvocation.MyCommand.Path
    }

    if ([string]::IsNullOrWhiteSpace($scriptPath)) {
        throw "Unable to determine the script path for workspace discovery"
    }

    $WorkspaceRoot = Split-Path -Parent (Split-Path -Parent $scriptPath)
}

function Resolve-FirstTool {
    param(
        [string[]]$CommandNames,
        [string[]]$CandidatePaths
    )

    foreach ($commandName in $CommandNames) {
        try {
            $command = Get-Command -Name $commandName -ErrorAction Stop
            if ($null -ne $command -and -not [string]::IsNullOrWhiteSpace($command.Source)) {
                return $command.Source
            }
        } catch {
        }
    }

    foreach ($candidate in $CandidatePaths) {
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }

        $resolved = Get-ChildItem -Path $candidate -File -ErrorAction SilentlyContinue |
            Sort-Object -Property FullName -Descending |
            Select-Object -First 1
        if ($null -ne $resolved) {
            return $resolved.FullName
        }
    }

    return $null
}

function Write-ToolStatus {
    param(
        [string]$Name,
        [string]$Path
    )

    if ([string]::IsNullOrWhiteSpace($Path)) {
        Write-Host ("[missing] {0}" -f $Name)
        return
    }

    Write-Host ("[found]   {0}: {1}" -f $Name, $Path)
}

function Invoke-SmokeCompile {
    param(
        [string]$CompilerPath,
        [string]$WorkspaceRoot
    )

    $buildDir = Join-Path $WorkspaceRoot "build\syntax"
    $root = $WorkspaceRoot
    $commonIncludes = @(
        "-I$root\lib\cluster_platform",
        "-I$root\lib\boot_control",
        "-I$root\lib\journal",
        "-I$root\bsp\stm32g474",
        "-I$root\app",
        "-I$root\tests",
        "-I$root\tests\fixtures"
    )

    $sources = @(
        @{ Source = "lib\cluster_platform\cs_cluster_platform.c"; Object = "cs_cluster_platform.o"; Includes = @("-I$root\lib\cluster_platform") }
        @{ Source = "lib\boot_control\cs_crc32.c"; Object = "cs_crc32.o"; Includes = @("-I$root\lib\boot_control") }
        @{ Source = "lib\boot_control\cs_boot_control.c"; Object = "cs_boot_control.o"; Includes = @("-I$root\lib\cluster_platform", "-I$root\lib\boot_control") }
        @{ Source = "lib\journal\cs_journal.c"; Object = "cs_journal.o"; Includes = @("-I$root\lib\cluster_platform", "-I$root\lib\boot_control", "-I$root\lib\journal") }
        @{ Source = "bsp\stm32g474\cs_adc_g474.c"; Object = "cs_adc_g474.o"; Includes = @("-I$root\bsp\stm32g474", "-I$root\lib\cluster_platform") }
        @{ Source = "bsp\stm32g474\cs_can_g474.c"; Object = "cs_can_g474.o"; Includes = @("-I$root\bsp\stm32g474", "-I$root\lib\cluster_platform") }
        @{ Source = "bsp\stm32g474\cs_flash_g474.c"; Object = "cs_flash_g474.o"; Includes = @("-I$root\bsp\stm32g474", "-I$root\lib\cluster_platform") }
        @{ Source = "bsp\stm32g474\cs_ina228.c"; Object = "cs_ina228.o"; Includes = @("-I$root\bsp\stm32g474", "-I$root\lib\cluster_platform") }
        @{ Source = "bsp\stm32g474\cs_iwdg_g474.c"; Object = "cs_iwdg_g474.o"; Includes = @("-I$root\bsp\stm32g474", "-I$root\lib\cluster_platform") }
        @{ Source = "bsp\stm32g474\cs_bsp_g474.c"; Object = "cs_bsp_g474.o"; Includes = @("-I$root\bsp\stm32g474", "-I$root\lib\cluster_platform") }
        @{ Source = "app\cs_can_bench_node.c"; Object = "cs_can_bench_node.o"; Includes = @("-I$root\app", "-I$root\bsp\stm32g474", "-I$root\lib\cluster_platform") }
        @{ Source = "tests\fixtures\flash_sim.c"; Object = "flash_sim.o"; Includes = @("-I$root\tests", "-I$root\tests\fixtures", "-I$root\lib\cluster_platform") }
        @{ Source = "tests\test_boot_control.c"; Object = "test_boot_control.o"; Includes = $commonIncludes }
        @{ Source = "tests\test_journal.c"; Object = "test_journal.o"; Includes = $commonIncludes }
        @{ Source = "tests\test_cluster_platform.c"; Object = "test_cluster_platform.o"; Includes = $commonIncludes }
        @{ Source = "tests\test_g474_bsp.c"; Object = "test_g474_bsp.o"; Includes = $commonIncludes }
    )

    New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

    foreach ($entry in $sources) {
        $sourcePath = Join-Path $root $entry.Source
        $objectPath = Join-Path $buildDir $entry.Object
        $arguments = @(
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-c",
            $sourcePath,
            "-o",
            $objectPath
        ) + $entry.Includes

        & $CompilerPath @arguments
        if ($LASTEXITCODE -ne 0) {
            throw ("Smoke compile failed for {0}" -f $entry.Source)
        }
    }

    Write-Host ("[pass]    ARM syntax smoke compiled {0} translation units into {1}" -f $sources.Count, $buildDir)
}

function Convert-ToCMakePath {
    param(
        [string]$Path
    )

    return $Path.Replace('\', '/')
}

function Invoke-HostConfigure {
    param(
        [string]$CMakePath,
        [string]$CompilerPath,
        [string]$NinjaPath,
        [string]$MinGWMakePath,
        [string]$WorkspaceRoot
    )

    $ctestPath = Join-Path (Split-Path -Parent $CMakePath) "ctest.exe"
    $windresPath = Join-Path (Split-Path -Parent $CompilerPath) "windres.exe"
    $originalPath = $env:PATH

    try {
        $env:PATH = ("{0};{1}" -f (Split-Path -Parent $CompilerPath), $originalPath)

        if ($CompilerPath -like "*gcc.exe" -and -not [string]::IsNullOrWhiteSpace($MinGWMakePath)) {
            $buildDir = Join-Path $WorkspaceRoot "build\host-mingw"
        } elseif (-not [string]::IsNullOrWhiteSpace($NinjaPath)) {
            $buildDir = Join-Path $WorkspaceRoot "build\host-ninja"
        } else {
            throw "Host configure requires either mingw32-make or ninja"
        }

        $arguments = @(
            "-S", $WorkspaceRoot,
            "-B", $buildDir,
            "-DCS_G474_USE_HAL=OFF",
            ("-DCMAKE_C_COMPILER={0}" -f (Convert-ToCMakePath $CompilerPath)),
            ("-DCMAKE_ASM_COMPILER={0}" -f (Convert-ToCMakePath $CompilerPath))
        )

        if (Test-Path -LiteralPath $windresPath) {
            $arguments += ("-DCMAKE_RC_COMPILER={0}" -f (Convert-ToCMakePath $windresPath))
        }

        if ($CompilerPath -like "*gcc.exe" -and -not [string]::IsNullOrWhiteSpace($MinGWMakePath)) {
            $arguments += @(
                "-G", "MinGW Makefiles",
                ("-DCMAKE_MAKE_PROGRAM={0}" -f (Convert-ToCMakePath $MinGWMakePath))
            )
        } elseif (-not [string]::IsNullOrWhiteSpace($NinjaPath)) {
            $arguments += @(
                "-G", "Ninja",
                ("-DCMAKE_MAKE_PROGRAM={0}" -f (Convert-ToCMakePath $NinjaPath))
            )
        } else {
            throw "Host configure requires either mingw32-make or ninja"
        }

        & $CMakePath @arguments
        if ($LASTEXITCODE -ne 0) {
            throw "Host configure failed"
        }

        & $CMakePath --build $buildDir --parallel 4
        if ($LASTEXITCODE -ne 0) {
            throw "Host build failed"
        }

        & $ctestPath --test-dir $buildDir --output-on-failure
        if ($LASTEXITCODE -ne 0) {
            throw "Host tests failed"
        }
    } finally {
        $env:PATH = $originalPath
    }

    Write-Host ("[pass]    Host build configured and built at {0}" -f $buildDir)
}

$armCompiler = Resolve-FirstTool -CommandNames @("arm-none-eabi-gcc") -CandidatePaths @(
    "C:\tools\arm-gnu-toolchain-*\bin\arm-none-eabi-gcc.exe",
    "C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\*\bin\arm-none-eabi-gcc.exe",
    "C:\Program Files\Arm GNU Toolchain arm-none-eabi\*\bin\arm-none-eabi-gcc.exe"
)
$cmake = Resolve-FirstTool -CommandNames @("cmake") -CandidatePaths @(
    "C:\tools\mingw64\bin\cmake.exe",
    "C:\Program Files\CMake\bin\cmake.exe"
)
$hostCompiler = Resolve-FirstTool -CommandNames @("gcc", "clang", "cl") -CandidatePaths @(
    "C:\tools\mingw64\bin\gcc.exe",
    "C:\tools\winlibs-*\mingw64\bin\gcc.exe",
    "C:\msys64\mingw64\bin\gcc.exe",
    "C:\mingw64\bin\gcc.exe",
    "C:\WinLibs\mingw64\bin\gcc.exe",
    "C:\Program Files\LLVM\bin\clang.exe",
    "C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe"
)
$scoopNinjaPath = $null
if (-not [string]::IsNullOrWhiteSpace($env:USERPROFILE)) {
    $scoopNinjaPath = Join-Path $env:USERPROFILE "scoop\apps\ninja\current\ninja.exe"
}

$ninja = Resolve-FirstTool -CommandNames @("ninja") -CandidatePaths @(
    "C:\tools\mingw64\bin\ninja.exe",
    "C:\tools\ninja-win\ninja.exe",
    "C:\Program Files\Ninja\ninja.exe",
    "C:\tools\ninja\ninja.exe",
    $scoopNinjaPath
)
$mingwMake = Resolve-FirstTool -CommandNames @("mingw32-make") -CandidatePaths @(
    "C:\tools\mingw64\bin\mingw32-make.exe",
    "C:\tools\winlibs-*\mingw64\bin\mingw32-make.exe",
    "C:\mingw64\bin\mingw32-make.exe",
    "C:\msys64\mingw64\bin\mingw32-make.exe"
)
$cubeHalHeader = Join-Path $WorkspaceRoot "bsp\stm32g474\cube_generated\Drivers\STM32G4xx_HAL_Driver\Inc\stm32g4xx_hal.h"
$cubeDriversReady = Test-Path -LiteralPath $cubeHalHeader

Write-Host "ClusterStore firmware environment check"
Write-Host ("workspace: {0}" -f $WorkspaceRoot)
Write-Host ""
Write-ToolStatus -Name "arm-none-eabi-gcc" -Path $armCompiler
Write-ToolStatus -Name "cmake" -Path $cmake
Write-ToolStatus -Name "host compiler" -Path $hostCompiler
Write-ToolStatus -Name "ninja" -Path $ninja
Write-ToolStatus -Name "mingw32-make" -Path $mingwMake
if ($cubeDriversReady) {
    Write-Host ("[found]   cube drivers: {0}" -f $cubeHalHeader)
} else {
    Write-Host ("[missing] cube drivers: {0}" -f $cubeHalHeader)
}
Write-Host ""

if (-not $SkipSmoke) {
    if ([string]::IsNullOrWhiteSpace($armCompiler)) {
        Write-Host "[skip]    ARM syntax smoke skipped because arm-none-eabi-gcc was not found"
    } else {
        Invoke-SmokeCompile -CompilerPath $armCompiler -WorkspaceRoot $WorkspaceRoot
    }
}

if (-not $SkipHostConfigure) {
    if ([string]::IsNullOrWhiteSpace($cmake) -or
        [string]::IsNullOrWhiteSpace($hostCompiler) -or
        ([string]::IsNullOrWhiteSpace($ninja) -and [string]::IsNullOrWhiteSpace($mingwMake))) {
        Write-Host "[skip]    Host CMake build skipped because cmake, a host compiler, and either ninja or mingw32-make are required"
    } else {
        Invoke-HostConfigure -CMakePath $cmake `
                             -CompilerPath $hostCompiler `
                             -NinjaPath $ninja `
                             -MinGWMakePath $mingwMake `
                             -WorkspaceRoot $WorkspaceRoot
    }
}
