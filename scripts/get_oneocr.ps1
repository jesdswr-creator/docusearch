<#
.SYNOPSIS
    Install oneocr.dll, oneocr.onemodel, and onnxruntime.dll for DocuSearch OCR.

.DESCRIPTION
    Copies the three required OCR files from the locally-installed Windows 11
    Snipping Tool (Microsoft.ScreenSketch) into the DocuSearch app folder.

    These files are NOT redistributed by DocuSearch. They are Microsoft-owned
    binaries that ship with the Windows 11 Snipping Tool. This script lets
    you legally use copies already present on your own machine.

    Supports Windows 10 and Windows 11. On Windows 10, you must first install
    the Snipping Tool app from the Microsoft Store:
        https://apps.microsoft.com/detail/9mz95kl8mr0l

.PARAMETER TargetDir
    Destination directory. Defaults to the directory containing the built
    docusearch.exe (auto-detected: looks for build\bin\Release\ relative to
    the repo root, then alongside the script itself).

.PARAMETER SourceDir
    Optional explicit source directory containing the Snipping Tool files.
    If omitted, the script searches Get-AppxPackage for Microsoft.ScreenSketch.

.EXAMPLE
    .\scripts\get_oneocr.ps1
    Installs into the default build output directory.

.EXAMPLE
    .\scripts\get_oneocr.ps1 -TargetDir "C:\DocuSearch\bin"
    Installs into the specified directory.

.EXAMPLE
    .\scripts\get_oneocr.ps1 -SourceDir "C:\Program Files\WindowsApps\Microsoft.ScreenSketch_11.x.x_x64__8wekyb3d8bbwe\SnippingTool"
    Uses the specified source directory (you may need to copy the folder out
    first if WindowsApps ACLs prevent direct access).
#>

[CmdletBinding()]
param(
    [string]$TargetDir = "",
    [string]$SourceDir = ""
)

$ErrorActionPreference = "Stop"

# ── Required files ─────────────────────────────────────────
$RequiredFiles = @("oneocr.dll", "oneocr.onemodel", "onnxruntime.dll")

function Write-Step  { param($msg) Write-Host "[oneocr] $msg" -ForegroundColor Cyan }
function Write-OK    { param($msg) Write-Host "[oneocr] $msg" -ForegroundColor Green }
function Write-Warn2 { param($msg) Write-Host "[oneocr] $msg" -ForegroundColor Yellow }
function Write-Err   { param($msg) Write-Host "[oneocr] $msg" -ForegroundColor Red }

# ── Locate the script's repo root ─────────────────────────
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot  = Split-Path -Parent $ScriptDir

# ── Determine target directory ────────────────────────────
if (-not $TargetDir) {
    $candidates = @(
        (Join-Path $RepoRoot "build\bin\Release"),
        (Join-Path $RepoRoot "build\bin\Debug"),
        (Join-Path $RepoRoot "build\Release"),
        (Join-Path $RepoRoot "build\Debug"),
        $RepoRoot
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) {
            $TargetDir = $c
            break
        }
    }
    if (-not $TargetDir) { $TargetDir = $RepoRoot }
}

if (-not (Test-Path $TargetDir)) {
    New-Item -ItemType Directory -Path $TargetDir -Force | Out-Null
}

Write-Step "Target directory: $TargetDir"

# ── Locate Snipping Tool install location ─────────────────
if (-not $SourceDir) {
    Write-Step "Looking for installed Snipping Tool (Microsoft.ScreenSketch)..."
    $pkg = Get-AppxPackage -Name "Microsoft.ScreenSketch" -ErrorAction SilentlyContinue
    if (-not $pkg) {
        Write-Err "Snipping Tool (Microsoft.ScreenSketch) is not installed."
        Write-Host ""
        Write-Host "Install it from the Microsoft Store:" -ForegroundColor Yellow
        Write-Host "  https://apps.microsoft.com/detail/9mz95kl8mr0l" -ForegroundColor Yellow
        Write-Host ""
        Write-Host "After installing, re-run this script." -ForegroundColor Yellow
        exit 1
    }

    Write-OK "Found Snipping Tool: $($pkg.Name) version $($pkg.Version)"
    $installDir = $pkg.InstallLocation

    # The oneocr files live in the SnippingTool subdirectory.
    $SourceDir = Join-Path $installDir "SnippingTool"
    if (-not (Test-Path $SourceDir)) {
        # Some builds put the files directly in the install root.
        $SourceDir = $installDir
    }
}

Write-Step "Source directory: $SourceDir"

if (-not (Test-Path $SourceDir)) {
    Write-Err "Source directory does not exist: $SourceDir"
    exit 1
}

# ── Verify all required files are present in source ───────
$missing = @()
foreach ($f in $RequiredFiles) {
    $src = Join-Path $SourceDir $f
    if (-not (Test-Path $src)) {
        $missing += $f
    }
}

if ($missing.Count -gt 0) {
    Write-Err "Missing required files in source directory:"
    foreach ($f in $missing) { Write-Host "  - $f" }
    Write-Host ""
    Write-Host "The Snipping Tool version installed on this machine does not" -ForegroundColor Yellow
    Write-Host "appear to ship oneocr. Update Snipping Tool from Microsoft Store" -ForegroundColor Yellow
    Write-Host "and try again." -ForegroundColor Yellow
    exit 1
}

# ── Copy files ────────────────────────────────────────────
$copied = 0
foreach ($f in $RequiredFiles) {
    $src = Join-Path $SourceDir $f
    $dst = Join-Path $TargetDir $f
    try {
        Copy-Item -Path $src -Destination $dst -Force -ErrorAction Stop
        $size = (Get-Item $dst).Length
        Write-OK "Copied $f ($('{0:N1}' -f ($size/1MB)) MB)"
        $copied++
    } catch {
        # WindowsApps has restrictive ACLs — copy may fail with Access Denied.
        Write-Err "Failed to copy ${f}: $($_.Exception.Message)"
        Write-Host ""
        Write-Warn2 "WindowsApps files have restrictive ACLs. Workaround:"
        Write-Host "  1. Open PowerShell as Administrator" -ForegroundColor Yellow
        Write-Host "  2. Run:  takeown /f ""$SourceDir"" /r /d Y" -ForegroundColor Yellow
        Write-Host "  3. Run:  icacls ""$SourceDir"" /grant ""${env:USERNAME}:(OI)(CI)F"" /T" -ForegroundColor Yellow
        Write-Host "  4. Re-run this script" -ForegroundColor Yellow
        Write-Host ""
        Write-Host "  OR manually copy these 3 files from:" -ForegroundColor Yellow
        Write-Host "    $SourceDir" -ForegroundColor Yellow
        Write-Host "  to:" -ForegroundColor Yellow
        Write-Host "    $TargetDir" -ForegroundColor Yellow
        exit 1
    }
}

if ($copied -eq $RequiredFiles.Count) {
    Write-Host ""
    Write-OK "All $copied OCR files installed successfully."
    Write-OK "DocuSearch OCR is ready to use."
    Write-Host ""
    Write-Host "Files installed at: $TargetDir" -ForegroundColor White
    exit 0
} else {
    Write-Err "Only $copied of $($RequiredFiles.Count) files were copied."
    exit 1
}
