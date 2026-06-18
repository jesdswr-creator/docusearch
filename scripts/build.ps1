# ============================================================
# build.ps1 — One-shot configure + build for Windows
# ============================================================
# Usage:
#   .\scripts\build.ps1
#   .\scripts\build.ps1 -Clean
# ============================================================

param(
    [switch]$Clean,
    [string]$QtPath       = "C:\Qt\6.7.0\msvc2022_64",
    [string]$VcpkgRoot    = $env:VCPKG_ROOT,
    [string]$BuildDir     = "build",
    [string]$Config       = "Release",
    [int]   $Parallel     = 0
)

$ErrorActionPreference = "Stop"

if (-not $VcpkgRoot) {
    Write-Error "VCPKG_ROOT is not set. Install vcpkg and set VCPKG_ROOT env var."
    exit 1
}
if (-not (Test-Path $QtPath)) {
    Write-Error "Qt not found at: $QtPath"
    exit 1
}

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "Cleaning $BuildDir..."
    Remove-Item -Recurse -Force $BuildDir
}

$toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"

Write-Host "Configuring..."
cmake -B $BuildDir -S . `
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" `
    -DCMAKE_PREFIX_PATH="$QtPath" `
    -DVCPKG_TARGET_TRIPLET=x64-windows

if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$jobs = if ($Parallel -gt 0) { $Parallel } else { $env:NUMBER_OF_PROCESSORS }
Write-Host "Building ($Config, $jobs parallel jobs)..."
cmake --build $BuildDir --config $Config --parallel $jobs

if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Run windeployqt to bundle Qt DLLs next to the exe
$exe = Join-Path $BuildDir "bin\$Config\DocuSearch.exe"
if (Test-Path $exe) {
    $windeployqt = Join-Path $QtPath "bin\windeployqt.exe"
    if (Test-Path $windeployqt) {
        Write-Host "Running windeployqt..."
        & $windeployqt --$Config.ToLower() $exe
    }
    Write-Host ""
    Write-Host "Build complete: $exe" -ForegroundColor Green
} else {
    Write-Warning "Build appeared to succeed but $exe was not found."
}
