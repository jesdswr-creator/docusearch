<#
.SYNOPSIS
    Verify DocuSearch setup — checks that all required files and
    dependencies are in place.

.DESCRIPTION
    Runs a comprehensive check of the DocuSearch installation:
      1. Verifies docusearch.exe exists and is runnable.
      2. Verifies docusearch_ocr_helper.exe exists.
      3. Verifies oneocr.dll, oneocr.onemodel, onnxruntime.dll are present.
      4. Verifies Qt6 DLLs are present.
      5. Verifies Poppler DLLs are present.
      6. Verifies the Snipping Tool is installed (for re-installing oneocr if needed).
      7. Verifies write access to %APPDATA%\DocuSearch\.

    Exits 0 if all checks pass, 1 if any check fails.

.PARAMETER TargetDir
    Directory to verify. Defaults to the directory containing this script.

.EXAMPLE
    .\scripts\verify_setup.ps1
#>

[CmdletBinding()]
param(
    [string]$TargetDir = ""
)

$ErrorActionPreference = "Stop"

function Write-Pass { param($msg) Write-Host "[PASS] $msg" -ForegroundColor Green }
function Write-Fail  { param($msg) Write-Host "[FAIL] $msg" -ForegroundColor Red }
function Write-Warn2 { param($msg) Write-Host "[WARN] $msg" -ForegroundColor Yellow }
function Write-Info  { param($msg) Write-Host "[INFO] $msg" -ForegroundColor Cyan }

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $TargetDir) {
    $TargetDir = Split-Path -Parent $ScriptDir
}

Write-Info "Verifying DocuSearch setup in: $TargetDir"
Write-Host ""

$failures = 0
$warnings = 0

# ── 1. Main executable ─────────────────────────────────────
$docuExe = Join-Path $TargetDir "DocuSearch.exe"
if (Test-Path $docuExe) {
    $size = (Get-Item $docuExe).Length
    Write-Pass "DocuSearch.exe found ($('{0:N1}' -f ($size/1MB)) MB)"
} else {
    Write-Fail "DocuSearch.exe NOT found"
    $failures++
}

# ── 2. OCR helper executable ───────────────────────────────
$helperExe = Join-Path $TargetDir "docusearch_ocr_helper.exe"
if (Test-Path $helperExe) {
    Write-Pass "docusearch_ocr_helper.exe found"
} else {
    Write-Fail "docusearch_ocr_helper.exe NOT found (OCR will be unavailable)"
    $failures++
}

# ── 3. oneocr files ────────────────────────────────────────
$oneocrFiles = @("oneocr.dll", "oneocr.onemodel", "onnxruntime.dll")
$oneocrMissing = @()
foreach ($f in $oneocrFiles) {
    $p = Join-Path $TargetDir $f
    if (Test-Path $p) {
        $size = (Get-Item $p).Length
        Write-Pass "$f found ($('{0:N1}' -f ($size/1MB)) MB)"
    } else {
        Write-Warn2 "$f NOT found"
        $oneocrMissing += $f
        $warnings++
    }
}

if ($oneocrMissing.Count -gt 0) {
    Write-Host ""
    Write-Warn2 "OCR is not configured. Install with:"
    Write-Host "  .\scripts\get_oneocr.ps1" -ForegroundColor Yellow
}

# ── 4. Qt6 DLLs ────────────────────────────────────────────
$qtDlls = Get-ChildItem -Path $TargetDir -Filter "Qt6*.dll" -ErrorAction SilentlyContinue
if ($qtDlls -and $qtDlls.Count -ge 5) {
    Write-Pass "$($qtDlls.Count) Qt6 DLLs found"
} else {
    $count = if ($qtDlls) { $qtDlls.Count } else { 0 }
    Write-Fail "Only $count Qt6 DLLs found (expected at least 5)"
    Write-Host "         Run windeployqt to install Qt dependencies."
    $failures++
}

# ── 5. Poppler DLLs ────────────────────────────────────────
$popplerDll = Get-ChildItem -Path $TargetDir -Filter "poppler*.dll" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($popplerDll) {
    Write-Pass "Poppler DLL found: $($popplerDll.Name)"
} else {
    Write-Warn2 "No poppler*.dll found (PDF text extraction will be unavailable)"
    $warnings++
}

# Minizip / zlib (for OOXML extraction)
$zlibDll = Get-ChildItem -Path $TargetDir -Filter "zlib*.dll" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($zlibDll) {
    Write-Pass "zlib DLL found: $($zlibDll.Name)"
} else {
    Write-Warn2 "No zlib*.dll found (DOCX/XLSX/PPTX extraction may be unavailable)"
    $warnings++
}

# ── 6. Snipping Tool installation ──────────────────────────
$snipPkg = Get-AppxPackage -Name "Microsoft.ScreenSketch" -ErrorAction SilentlyContinue
if ($snipPkg) {
    Write-Pass "Snipping Tool installed (version $($snipPkg.Version))"
    Write-Info "  Install path: $($snipPkg.InstallLocation)"

    # Check the Snipping Tool folder for oneocr files (so user can install if needed)
    $snipDir = Join-Path $snipPkg.InstallLocation "SnippingTool"
    if (-not (Test-Path $snipDir)) { $snipDir = $snipPkg.InstallLocation }

    $oneocrInSnip = Join-Path $snipDir "oneocr.dll"
    if (Test-Path $oneocrInSnip) {
        Write-Pass "oneocr.dll is available in Snipping Tool (can install via get_oneocr.ps1)"
    } else {
        Write-Warn2 "oneocr.dll not found in Snipping Tool folder — update Snipping Tool from Microsoft Store"
        $warnings++
    }
} else {
    Write-Warn2 "Snipping Tool (Microsoft.ScreenSketch) is not installed"
    Write-Host "         Install from: https://apps.microsoft.com/detail/9mz95kl8mr0l" -ForegroundColor Yellow
    Write-Host "         (Required to install oneocr files via get_oneocr.ps1)"
    $warnings++
}

# ── 7. AppData directory write access ──────────────────────
$appDataDir = Join-Path $env:APPDATA "DocuSearch"
try {
    if (-not (Test-Path $appDataDir)) {
        New-Item -ItemType Directory -Path $appDataDir -Force | Out-Null
    }
    $testFile = Join-Path $appDataDir "verify_write_test.tmp"
    "test" | Out-File -FilePath $testFile -Encoding utf8
    Remove-Item $testFile -Force
    Write-Pass "Write access to $appDataDir"
} catch {
    Write-Fail "Cannot write to $appDataDir : $($_.Exception.Message)"
    $failures++
}

# ── 8. SQLite DLL ──────────────────────────────────────────
$sqliteDll = Get-ChildItem -Path $TargetDir -Filter "sqlite*.dll" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($sqliteDll) {
    Write-Pass "SQLite DLL found: $($sqliteDll.Name)"
}

# ── Summary ────────────────────────────────────────────────
Write-Host ""
Write-Host "================================================" -ForegroundColor White
if ($failures -eq 0 -and $warnings -eq 0) {
    Write-Pass "All checks passed. DocuSearch is ready to use."
    exit 0
} elseif ($failures -eq 0) {
    Write-Warn2 "All critical checks passed. $warnings warning(s)."
    Write-Host ""
    Write-Host "DocuSearch will run, but some features may be limited." -ForegroundColor Yellow
    exit 0
} else {
    Write-Fail "$failures critical check(s) failed. $warnings warning(s)."
    Write-Host ""
    Write-Host "DocuSearch may not run correctly. Please fix the failures above." -ForegroundColor Red
    exit 1
}
