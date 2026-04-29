[CmdletBinding()]
param(
    [string]$WorkspaceRoot = "",
    [string]$BuildDirectory = "build\g474-arm-hal-mingw",
    [switch]$Clean,
    [switch]$ReuseBuildDirectory
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

$resolvedWorkspaceRoot = [System.IO.Path]::GetFullPath($WorkspaceRoot)
$buildPath = Join-Path $WorkspaceRoot $BuildDirectory
$resolvedBuildPath = [System.IO.Path]::GetFullPath($buildPath)
$toolchainFile = Join-Path $WorkspaceRoot "cmake\arm-none-eabi.cmake"

if (-not (Test-Path -LiteralPath $toolchainFile)) {
    throw "ARM toolchain file not found at $toolchainFile"
}

if (
    -not $resolvedBuildPath.StartsWith(
        $resolvedWorkspaceRoot,
        [System.StringComparison]::OrdinalIgnoreCase
    )
) {
    throw "Refusing to use build directory outside the firmware workspace: $resolvedBuildPath"
}

if (($Clean -or -not $ReuseBuildDirectory) -and (Test-Path -LiteralPath $buildPath)) {
    Remove-Item -LiteralPath $buildPath -Recurse -Force
}

$cmakePath = (Get-Command cmake -ErrorAction Stop).Source
$mingwMakePath = (Get-Command mingw32-make -ErrorAction Stop).Source

Write-Host ("[info]    ARM HAL build directory: {0}" -f $resolvedBuildPath)
if (-not $ReuseBuildDirectory) {
    Write-Host "[info]    Performing a clean configure to avoid stale CMake toolchain cache."
}

& $cmakePath `
    -G "MinGW Makefiles" `
    -S $WorkspaceRoot `
    -B $buildPath `
    -DCMAKE_BUILD_TYPE=MinSizeRel `
    -DCS_BUILD_TESTS=OFF `
    -DCS_G474_USE_HAL=ON `
    -DCS_BUILD_CAN_BENCH_APP=ON `
    -DCS_BUILD_NATIVE_NODE_APP=ON `
    -DCS_BUILD_BOOTLOADER=ON `
    "-DCMAKE_TOOLCHAIN_FILE=$($toolchainFile.Replace('\', '/'))" `
    "-DCMAKE_MAKE_PROGRAM=$($mingwMakePath.Replace('\', '/'))"

if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed for ARM HAL build"
}

& $cmakePath --build $buildPath --parallel 4
if ($LASTEXITCODE -ne 0) {
    throw "ARM HAL build failed"
}

Write-Host ("[pass]    ARM HAL build completed at {0}" -f $buildPath)
