# ============================================================
# build-msix.ps1 - Shared MSIX packager for DocuSearch
# ============================================================
# Produces an (unsigned) .msix suitable for:
#   * Microsoft Store submission via Partner Center (the Store
#     re-signs the package with a Microsoft certificate during
#     publishing - no CA code-signing certificate is needed), or
#   * local sideloading after signing with signtool.
#
# Single source of truth for the MSIX flow - used by BOTH
# .github/workflows/build.yml and scripts/build-release.ps1
# (the old duplicated inline logic drifted once before; do not
# re-inline this).
#
# Stages:
#   1. Generate the 7 tile/splash PNG assets from the master icon
#   2. Stage the package layout from the built release folder
#      (excluding test binaries and redist bootstrappers)
#   3. Stamp the staged AppxManifest Identity Version (and
#      optionally Identity Name/Publisher for Partner Center)
#   4. Verify the layout (exe, Qt DLLs, model, manifest, assets)
#   5. makeappx pack
#
# Usage:
#   .\scripts\build-msix.ps1                                 # derive version from CMakeLists.txt
#   .\scripts\build-msix.ps1 -Version 1.7.19                 # explicit version
#   .\scripts\build-msix.ps1 -IdentityName "45678YourApp" ` # Partner Center identity
#                            -IdentityPublisher "CN=..."
#
# Requires: the release folder to already exist (windeployqt + DLLs
# bundled), and the Windows SDK (makeappx.exe).
# ============================================================

[CmdletBinding()]
param(
    [string]$ProjectRoot       = "",
    [string]$BuildOutputDir    = "",     # e.g. build\bin\Release
    [string]$StageDir          = "",     # default <ProjectRoot>\build\msix
    [string]$Version           = "",     # default: project(VERSION) from CMakeLists.txt
    [string]$OutFile           = "",     # default dist\DocuSearch-<Version>-x64.msix
    # Partner Center product identity (Product management -> Product identity).
    # Leave empty for sideload builds; the manifest keeps its placeholder.
    [string]$IdentityName      = "",
    [string]$IdentityPublisher = ""
)

$ErrorActionPreference = "Stop"

function Write-Step { param($msg) Write-Host "[msix] $msg" -ForegroundColor Cyan }
function Write-OK   { param($msg) Write-Host "[msix]   OK: $msg" -ForegroundColor Green }
function Write-Warn2{ param($msg) Write-Host "[msix]   WARN: $msg" -ForegroundColor Yellow }

# ---- Locate the project ----------------------------------------------------
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $ProjectRoot) { $ProjectRoot = Split-Path -Parent $ScriptDir }

# ---- Resolve version (single source of truth: CMakeLists.txt) --------------
if (-not $Version) {
    $cmakeText = Get-Content (Join-Path $ProjectRoot "CMakeLists.txt") -Raw
    if ($cmakeText -notmatch 'project\(DocuSearch\s+VERSION\s+(\d+)\.(\d+)\.(\d+)') {
        Write-Error "Could not parse project VERSION from CMakeLists.txt (pass -Version)."
        exit 1
    }
    $Version = "$($Matches[1]).$($Matches[2]).$($Matches[3])"
}
$Version4 = if ($Version -match '^\d+\.\d+\.\d+$') { "$Version.0" } else { $Version }

# ---- Resolve paths ---------------------------------------------------------
if (-not $BuildOutputDir) { $BuildOutputDir = Join-Path $ProjectRoot "build\bin\Release" }
if (-not $StageDir)       { $StageDir       = Join-Path $ProjectRoot "build\msix" }
if (-not $OutFile)        { $OutFile        = Join-Path $ProjectRoot "dist\DocuSearch-$Version-x64.msix" }
$manifestSrc = Join-Path $ProjectRoot "installer\AppxManifest.xml"
$masterPng   = Join-Path $ProjectRoot "resources\icons\DocuSearch-256.png"

if (-not (Test-Path (Join-Path $BuildOutputDir "DocuSearch.exe"))) {
    Write-Error "DocuSearch.exe not found in $BuildOutputDir - build + windeployqt first."
    exit 1
}
if (-not (Test-Path $manifestSrc)) {
    Write-Error "AppxManifest.xml not found at $manifestSrc."
    exit 1
}

Write-Step "Packaging MSIX v$Version"
Write-Host "      Output dir : $BuildOutputDir"
Write-Host "      Stage dir  : $StageDir"
Write-Host "      Out file   : $OutFile"

# ---- 1. Generate the tile/splash assets from the master icon ---------------
$assetsDir = Join-Path $StageDir "assets"
New-Item -ItemType Directory -Path $assetsDir -Force | Out-Null
if (Test-Path $masterPng) {
    $assetSizes = @{
        "StoreLogo.png"         = @{ W = 50;  H = 50 }
        "Square44x44Logo.png"   = @{ W = 44;  H = 44 }
        "Square71x71Logo.png"   = @{ W = 71;  H = 71 }
        "Square150x150Logo.png" = @{ W = 150; H = 150 }
        "Square310x310Logo.png" = @{ W = 310; H = 310 }
        "Wide310x150Logo.png"   = @{ W = 310; H = 150 }
        "SplashScreen.png"      = @{ W = 620; H = 300 }
    }
    Add-Type -AssemblyName System.Drawing
    foreach ($entry in $assetSizes.GetEnumerator()) {
        $w = $entry.Value.W; $h = $entry.Value.H
        $src = [System.Drawing.Image]::FromFile($masterPng)
        $bmp = New-Object System.Drawing.Bitmap $w, $h
        $g   = [System.Drawing.Graphics]::FromImage($bmp)
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
        $g.PixelOffsetMode   = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        # Letterbox (do NOT stretch): center a height-sized square on the
        # transparent canvas so wide tiles and the splash keep proportions.
        $side = [Math]::Min($w, $h)
        $x = [int](($w - $side) / 2)
        $y = [int](($h - $side) / 2)
        $g.DrawImage($src, $x, $y, $side, $side)
        $bmp.Save((Join-Path $assetsDir $entry.Key), [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose(); $src.Dispose(); $g.Dispose()
    }
    Write-OK "Generated $($assetSizes.Count) manifest assets from master icon"
} else {
    Write-Error "Master icon not found: $masterPng - cannot generate manifest assets."
    exit 1
}

# ---- 2. Stage the package layout -------------------------------------------
# Clean the stage first: Copy-Item -Force overwrites but never deletes,
# so a dirty stage leaks files removed from newer builds into the package.
if (Test-Path $StageDir) { Remove-Item $StageDir -Recurse -Force }
New-Item -ItemType Directory -Path $StageDir -Force | Out-Null

Get-ChildItem -Path $BuildOutputDir | Where-Object {
    $_.Name -notlike "tst_*" -and      # CTest binaries
    $_.Name -notlike "vc_redist*" -and # VC++ redist bootstrappers (unused: /MT)
    $_.Name -notlike "vcredist*"
} | Copy-Item -Destination $StageDir -Recurse -Force
Copy-Item $manifestSrc (Join-Path $StageDir "AppxManifest.xml") -Force
Write-OK "Staged package layout"

# ---- 3. Stamp identity + version -------------------------------------------
$manifestPath = Join-Path $StageDir "AppxManifest.xml"
$manifest = Get-Content $manifestPath -Raw
# Identity Version <- app version (regex negative-lookbehind keeps
# MinVersion/MaxVersionTested in TargetDeviceFamily untouched).
$manifest = $manifest -replace '(?<![A-Za-z])Version="\d+\.\d+\.\d+\.\d+"', "Version=`"$Version4`""
if ($IdentityName -or $IdentityPublisher) {
    # Partner Center identity: replace ONLY inside the <Identity .../> tag.
    $idTag = [regex]::Match($manifest, '<Identity\b[^>]*/>').Value
    if (-not $idTag) { Write-Error "Could not locate the <Identity/> element."; exit 1 }
    $newTag = $idTag
    if ($IdentityName)      { $newTag = $newTag -replace 'Name="[^"]*"',      "Name=`"$IdentityName`"" }
    if ($IdentityPublisher) { $newTag = $newTag -replace 'Publisher="[^"]*"', "Publisher=`"$IdentityPublisher`"" }
    $manifest = $manifest.Replace($idTag, $newTag)
    Write-OK "Stamped Partner Center identity"
}
Set-Content -Path $manifestPath -Value $manifest -Encoding utf8
Write-OK "Stamped Identity Version=$Version4"

# ---- 4. Verify the layout (fail fast, before Partner Center does) ----------
$required = @(
    "DocuSearch.exe",
    "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "Qt6Sql.dll",
    "models\bge-small-en-v1.5\model.onnx",   # semantic search must work out of the box
    "AppxManifest.xml",
    "assets\StoreLogo.png", "assets\Square44x44Logo.png", "assets\Square150x150Logo.png",
    "assets\SplashScreen.png"
)
foreach ($rel in $required) {
    if (-not (Test-Path (Join-Path $StageDir $rel))) {
        Write-Error "Package layout is missing required file: $rel"
        exit 1
    }
}
Write-OK "Required files present (exe, Qt DLLs, BGE model, manifest, assets)"

$warnIfMissing = @(
    "docusearch_ocr_helper.exe",  # OCR (Windows.Media.Ocr helper process)
    "pdfium.dll",                 # PDF extraction + preview
    "onnxruntime.dll",            # BGE inference
    "sqldrivers\qsqlite.dll"      # SQLite driver plugin
)
foreach ($rel in $warnIfMissing) {
    if (-not (Test-Path (Join-Path $StageDir $rel))) {
        Write-Warn2 "$rel missing - feature will be degraded"
    }
}

# ---- 5. makeappx pack -------------------------------------------------------
$makeAppx = (Get-Command makeappx -ErrorAction SilentlyContinue).Source
if (-not $makeAppx) {
    $sdk = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin" -Directory |
           Sort-Object Name -Descending |
           Select-Object -First 1
    if ($sdk) { $makeAppx = Join-Path $sdk.FullName "x64\makeappx.exe" }
}
if (-not $makeAppx -or -not (Test-Path $makeAppx)) {
    Write-Error "makeappx.exe not found. Install the Windows SDK."
    exit 1
}

$distDir = Split-Path $OutFile -Parent
if (-not (Test-Path $distDir)) { New-Item -ItemType Directory -Path $distDir | Out-Null }
if (Test-Path $OutFile) { Remove-Item $OutFile -Force }

# /nv: skip validation so Win32-specific manifest bits (rescap, desktop5)
# don't trip the generic UWP validator. Partner Center runs its own full
# validation at upload.
& $makeAppx pack /o /d $StageDir /p $OutFile /nv
if ($LASTEXITCODE -ne 0) { Write-Error "MakeAppx pack failed"; exit 1 }

$size = (Get-Item $OutFile).Length
Write-OK "Wrote $OutFile ($('{0:N1}' -f ($size/1MB)) MB)"
Write-Host "[msix] Store submission: upload this .msix in Partner Center (unsigned is correct -"
Write-Host "[msix] the Store re-signs it during publishing)."
