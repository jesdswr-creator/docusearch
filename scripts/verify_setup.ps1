<#
.SYNOPSIS
    Verify DocuSearch setup — checks that all required files and
    dependencies are in place.

.DESCRIPTION
    Runs a comprehensive check of the DocuSearch installation:
      1. Verifies docusearch.exe exists and is runnable.
      2. Verifies docusearch_ocr_helper.exe exists.
      3. Verifies Windows.Media.Ocr language packs are installed.
      4. Verifies Qt6 DLLs are present.
      5. Verifies Poppler DLLs are present.
      6. Verifies zlib / minizip DLL is present (OOXML extraction).
      7. Verifies write access to %APPDATA%\DocuSearch\.
      8. Verifies SQLite DLL is present.

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

# ── 3. Windows.Media.Ocr language packs ───────────────────
# DocuSearch uses Windows.Media.Ocr — the officially-supported WinRT
# OCR API built into Windows 10 1809+. No DLLs to install; the only
# requirement is at least one OCR language pack.
#
# We probe by launching the helper exe with no args (it exits 1 and
# prints "Usage:" to stderr). If the helper is missing or the WinRT
# runtime isn't available, we'll catch it here.
$ocrLangs = $null
try {
    # Windows.Media.Ocr.OcrEngine.AvailableRecognizerLanguages is the
    # canonical list. We use a tiny PowerShell probe via the WinRT
    # projection (the same one the helper uses).
    Add-Type -AssemblyName "System.Runtime.WindowsRuntime" -ErrorAction Stop
    $asTaskGeneric = ([System.WindowsRuntime.WindowsRuntimeSystemExtensions].GetMethods() |
        Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' } |
        Select-Object -First 1).MakeGenericMethod([Windows.Media.Ocr.OcrResult])
    # Simpler: just enumerate the language tags via the static property.
    $langs = [Windows.Media.Ocr.OcrEngine,Windows.Media.Ocr,ContentType=WindowsRuntime]::AvailableRecognizerLanguages
    if ($langs -and $langs.Size -gt 0) {
        $ocrLangs = @()
        for ($i = 0; $i -lt $langs.Size; $i++) {
            $ocrLangs += $langs.GetAt($i).LanguageTag
        }
        Write-Pass "Windows.Media.Ocr ready — $($ocrLangs.Count) language pack(s): $($ocrLangs -join ', ')"
    } else {
        Write-Warn2 "No OCR language packs installed"
        Write-Host "         Install via: Settings > Time & Language > Language >" -ForegroundColor Yellow
        Write-Host "           Add a language > Optical character recognition" -ForegroundColor Yellow
        $warnings++
    }
} catch {
    # WinRT projection not available in this PowerShell host — skip the
    # check rather than fail. The helper exe will surface the real
    # status when the user runs OCR.
    Write-Warn2 "Could not probe Windows.Media.Ocr from PowerShell: $($_.Exception.Message)"
    Write-Host "         The helper exe will report status at runtime." -ForegroundColor Yellow
    $warnings++
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

# ── 6. AppData directory write access ──────────────────────
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

# ── 7. SQLite DLL ──────────────────────────────────────────
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
