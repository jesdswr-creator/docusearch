# ============================================================
# build-release.ps1
# ============================================================
# End-to-end Windows 11 release pipeline for DocuSearch.
#
# Stages:
#   1. Configure (CMake + vcpkg + Qt)
#   2. Build (Release, parallel)
#   3. Bundle Qt DLLs (windeployqt)
#   4. [Optional] Build unit tests + ctest
#   5. [Optional -MakeMsi]  Produce DocuSearch-Setup.msi via WiX v4
#   6. [Optional -MakeMsix] Produce DocuSearch-x64.msix via MakeAppx
#   7. [Optional -Sign]     Sign both packages with signtool
#   8. [Optional -Zip]      Also produce a portable .zip
#
# Usage:
#   .\scripts\build-release.ps1                              # build only
#   .\scripts\build-release.ps1 -MakeMsi                     # + MSI
#   .\scripts\build-release.ps1 -MakeMsi -MakeMsix -Sign     # full release
#   .\scripts\build-release.ps1 -Clean -RunTests -MakeMsi    # clean + tests + MSI
#
# Required environment:
#   $env:VCPKG_ROOT   - path to vcpkg checkout
#   $env:QtPath       - e.g. C:\Qt\6.7.0\msvc2022_64  (or pass -QtPath)
#   WiX v4 (`wix` dotnet tool)         - only for -MakeMsi
#   Windows SDK (MakeAppx + signtool)  - only for -MakeMsix / -Sign
# ============================================================

param(
    [switch]$Clean,
    [switch]$RunTests,
    [switch]$MakeMsi,
    [switch]$MakeMsix,
    [switch]$Sign,
    [switch]$Zip,
    [string]$QtPath        = $env:QtPath,
    [string]$VcpkgRoot     = $env:VCPKG_ROOT,
    [string]$BuildDir      = "build",
    [string]$Config        = "Release",
    # v1.7.11: default derives from project(VERSION) in CMakeLists.txt —
    # the single source of truth CI uses (the old hardcoded "1.0.0.0"
    # default was frozen and drifted from every release since).
    [string]$Version       = "",
    [string]$CertPfx        = "",          # path to .pfx for signing
    [string]$CertPassword   = "",          # .pfx password
    [string]$TimestampUrl   = "http://timestamp.digicert.com",
    [int]   $Parallel       = 0
)

$ErrorActionPreference = "Stop"

# ---- Resolve version from CMakeLists.txt when not passed -------------------
if (-not $Version) {
    $cmakeText = Get-Content (Join-Path $PSScriptRoot "..\CMakeLists.txt") -Raw
    if ($cmakeText -match 'project\(DocuSearch\s+VERSION\s+(\d+)\.(\d+)\.(\d+)') {
        $Version = "$($Matches[1]).$($Matches[2]).$($Matches[3])"
        Write-Host "Version from CMakeLists.txt: $Version" -ForegroundColor DarkGray
    } else {
        Write-Error "Could not parse project VERSION from CMakeLists.txt (pass -Version)."
        exit 1
    }
}
# MSI ProductVersion needs the 4-part form.
$VersionMsi = if ($Version -match '^\d+\.\d+\.\d+$') { "$Version.0" } else { $Version }

# ---- Validate prerequisites ------------------------------------------------
if (-not $VcpkgRoot) { Write-Error "VCPKG_ROOT is not set."; exit 1 }
if (-not $QtPath -or -not (Test-Path $QtPath)) {
    Write-Error "QtPath not found: $QtPath (pass -QtPath or set `$env:QtPath)."
    exit 1
}

$projectRoot  = Split-Path $PSScriptRoot -Parent
$buildOutput  = Join-Path $projectRoot "$BuildDir\bin\$Config"
$exe          = Join-Path $buildOutput "DocuSearch.exe"

Write-Host ""
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host " DocuSearch - Windows 11 Release Builder"      -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host " Project root : $projectRoot"
Write-Host " Build dir    : $BuildDir"
Write-Host " Config       : $Config"
Write-Host " Qt           : $QtPath"
Write-Host " vcpkg root   : $VcpkgRoot"
Write-Host " Output       : $exe"
Write-Host " Tests        : $RunTests"
Write-Host " MSI          : $MakeMsi"
Write-Host " MSIX         : $MakeMsix"
Write-Host " Sign         : $Sign"
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host ""

# ---- Clean ----------------------------------------------------------------
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "[1/8] Cleaning $BuildDir..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $BuildDir
}

# ---- 1. Configure ---------------------------------------------------------
Write-Host "[1/8] Configuring CMake..." -ForegroundColor Yellow
$toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
$testFlag = if ($RunTests) { "-DDOCUSEARCH_BUILD_TESTS=ON" } else { "" }

cmake -B $BuildDir -S $projectRoot `
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" `
    -DCMAKE_PREFIX_PATH="$QtPath" `
    -DVCPKG_TARGET_TRIPLET=x64-windows `
    $testFlag

if ($LASTEXITCODE -ne 0) { Write-Error "CMake configure failed"; exit 1 }

# ---- 2. Build -------------------------------------------------------------
Write-Host ""
Write-Host "[2/8] Building ($Config)..." -ForegroundColor Yellow
$jobs = if ($Parallel -gt 0) { $Parallel } else { $env:NUMBER_OF_PROCESSORS }
cmake --build $BuildDir --config $Config --parallel $jobs
if ($LASTEXITCODE -ne 0) { Write-Error "Build failed"; exit 1 }

if (-not (Test-Path $exe)) {
    Write-Error "Build reported success but $exe was not produced."
    exit 1
}

# ---- 3. windeployqt -------------------------------------------------------
Write-Host ""
Write-Host "[3/8] Bundling Qt runtime DLLs (windeployqt)..." -ForegroundColor Yellow
$windeployqt = Join-Path $QtPath "bin\windeployqt.exe"
if (-not (Test-Path $windeployqt)) {
    Write-Error "windeployqt.exe not found at: $windeployqt"
    exit 1
}
& $windeployqt --release --no-translations --no-system-d3d-compiler `
               --no-opengl-sw --no-quick-import `
               --compiler-runtime `
               $exe
if ($LASTEXITCODE -ne 0) { Write-Error "windeployqt failed"; exit 1 }

# Also copy the DocuSearch icon + themes next to the exe (windeployqt
# doesn't see Qt resources embedded via RCC, but having the loose
# files on disk makes Settings -> Edit theme overrides work).
$themesSrc = Join-Path $projectRoot "resources\themes"
$themesDst = Join-Path $buildOutput "themes"
if (-not (Test-Path $themesDst)) { New-Item -ItemType Directory -Path $themesDst | Out-Null }
Copy-Item -Path "$themesSrc\*" -Destination $themesDst -Recurse -Force

# ---- 4. Tests (optional) --------------------------------------------------
if ($RunTests) {
    Write-Host ""
    Write-Host "[4/8] Running unit tests..." -ForegroundColor Yellow
    Push-Location $BuildDir
    try {
        ctest --output-on-failure -C $Config
        if ($LASTEXITCODE -ne 0) {
            Write-Error "Unit tests failed. Aborting release."
            exit 1
        }
    } finally { Pop-Location }
} else {
    Write-Host ""
    Write-Host "[4/8] Skipping tests (pass -RunTests to enable)." -ForegroundColor DarkGray
}

# ---- 5. Bundle distribution docs + Portable ZIP (optional) ----------------
# v1.7.11: every distribution (ZIP, MSI harvest, MSIX stage all read
# $buildOutput) must carry the third-party notices (Qt LGPLv3 attribution
# obligation) and the privacy policy — so copy them unconditionally.
foreach ($doc in @("THIRD-PARTY-NOTICES.md", "PRIVACY.md", "HELP.md", "FAQ.md")) {
    $srcDoc = Join-Path $projectRoot $doc
    if (Test-Path $srcDoc) { Copy-Item $srcDoc $buildOutput -Force }
}
if ($Zip) {
    Write-Host ""
    Write-Host "[5/8] Creating portable ZIP..." -ForegroundColor Yellow
    $zipPath = Join-Path $projectRoot "dist\DocuSearch-$Version-portable.zip"
    $distDir = Split-Path $zipPath -Parent
    if (-not (Test-Path $distDir)) { New-Item -ItemType Directory -Path $distDir | Out-Null }
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    Compress-Archive -Path "$buildOutput\*" -DestinationPath $zipPath -CompressionLevel Optimal
    Write-Host "      Wrote $zipPath" -ForegroundColor Green
} else {
    Write-Host ""
    Write-Host "[5/8] Skipping portable ZIP (pass -Zip to enable)." -ForegroundColor DarkGray
}

# ---- 6. WiX MSI (optional) ------------------------------------------------
if ($MakeMsi) {
    Write-Host ""
    Write-Host "[6/8] Building MSI installer (WiX)..." -ForegroundColor Yellow

    $wixSrcDir = Join-Path $projectRoot "installer"
    $wixWork   = Join-Path $projectRoot "$BuildDir\wix"
    if (-not (Test-Path $wixWork)) { New-Item -ItemType Directory -Path $wixWork | Out-Null }

    # 6a. Harvest the windeployqt output into a WiX v4 fragment.
    #     v1.7.11: use scripts/generate-harvest.ps1 - the SAME harvester CI
    #     uses. The old heat.exe call here had drifted: it emitted component
    #     group 'AppFilesHarvest' into a .wxi that was never passed to the
    #     build, while DocuSearch.wxs references ComponentGroupRef
    #     'HarvestedFiles' from DocuSearch.Harvest.wxs - so every local
    #     -MakeMsi run died on an unresolved reference.
    $harvestScript = Join-Path $projectRoot "scripts\generate-harvest.ps1"
    $harvestWxs    = Join-Path $wixSrcDir "DocuSearch.Harvest.wxs"
    & pwsh -File $harvestScript -BuildDir $buildOutput -OutputFile $harvestWxs
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $harvestWxs)) {
        Write-Error "generate-harvest.ps1 failed - no $harvestWxs produced"
        exit 1
    }

    # 6b. Compile + link the installer.
    $wxsFile = Join-Path $wixSrcDir "DocuSearch.wxs"
    $msiOut  = Join-Path $projectRoot "dist\DocuSearch-Setup-$Version.msi"
    $distDir = Split-Path $msiOut -Parent
    if (-not (Test-Path $distDir)) { New-Item -ItemType Directory -Path $distDir | Out-Null }
    if (Test-Path $msiOut) { Remove-Item $msiOut -Force }

    # DocuSearch.wxs is WiX v4 schema (<Package> root, wxs/v4 namespace):
    # the v3 candle+light toolchain CANNOT compile it, so the old
    # "fall back to WiX v3" path here could never have worked. Require v4.
    $wixTool = (Get-Command wix -ErrorAction SilentlyContinue).Source
    if (-not $wixTool) {
        Write-Error "WiX v4 'wix' tool not found. Install with: dotnet tool install -g wix"
        exit 1
    }
    Write-Host "      Using WiX v4 (wix dotnet tool)..." -ForegroundColor DarkGray
    # Pass BOTH the main wxs and the harvested fragment - same invocation
    # shape as the CI workflow's 'Build MSI installer (WiX v4)' step.
    & $wixTool build $wxsFile $harvestWxs `
          -o $msiOut `
          -d "BuildOutputDir=$buildOutput" `
          -d "ProductVersion=$VersionMsi" `
          -ext WixToolset.UI.wixext `
          -platform x64
    if ($LASTEXITCODE -ne 0) { Write-Error "WiX build failed"; exit 1 }
    Write-Host "      Wrote $msiOut" -ForegroundColor Green
} else {
    Write-Host ""
    Write-Host "[6/8] Skipping MSI (pass -MakeMsi to enable)." -ForegroundColor DarkGray
}

# ---- 7. MSIX (optional) ---------------------------------------------------
if ($MakeMsix) {
    Write-Host ""
    Write-Host "[7/8] Building MSIX package..." -ForegroundColor Yellow
    # v1.7.20: delegate to scripts/build-msix.ps1 - the SAME script CI uses.
    # (The old inline copy of assets/staging/stamping/packing drifted from
    # CI once; there is now exactly one implementation of the MSIX flow.)
    # Identity stays the manifest placeholder here; pass
    # -IdentityName/-IdentityPublisher (or edit installer/AppxManifest.xml)
    # with the Partner Center product identity before a Store submission.
    $msixOut = Join-Path $projectRoot "dist\DocuSearch-$Version-x64.msix"
    & pwsh -File (Join-Path $PSScriptRoot "build-msix.ps1") `
        -ProjectRoot $projectRoot `
        -BuildOutputDir $buildOutput `
        -StageDir (Join-Path $projectRoot "$BuildDir\msix") `
        -Version $Version `
        -OutFile $msixOut
    if ($LASTEXITCODE -ne 0) { Write-Error "MSIX build failed"; exit 1 }
    Write-Host "      Wrote $msixOut" -ForegroundColor Green
} else {
    Write-Host ""
    Write-Host "[7/8] Skipping MSIX (pass -MakeMsix to enable)." -ForegroundColor DarkGray
}

# ---- 8. Sign (optional) ---------------------------------------------------
if ($Sign) {
    Write-Host ""
    Write-Host "[8/8] Signing packages..." -ForegroundColor Yellow
    if (-not $CertPfx -or -not (Test-Path $CertPfx)) {
        Write-Error "Sign requested but -CertPfx not provided or not found."
        exit 1
    }
    $signtool = (Get-Command signtool -ErrorAction SilentlyContinue).Source
    if (-not $signtool) {
        $sdk = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin" -Directory |
               Sort-Object Name -Descending | Select-Object -First 1
        if ($sdk) { $signtool = Join-Path $sdk.FullName "x64\signtool.exe" }
    }
    if (-not $signtool -or -not (Test-Path $signtool)) {
        Write-Error "signtool.exe not found. Install the Windows SDK."
        exit 1
    }
    $signArgs = @("sign", "/fd", "SHA256", "/tr", $TimestampUrl, "/td", "SHA256",
                  "/f", $CertPfx)
    if ($CertPassword) { $signArgs += @("/p", $CertPassword) }

    $msi  = Join-Path $projectRoot "dist\DocuSearch-Setup-$Version.msi"
    $msix = Join-Path $projectRoot "dist\DocuSearch-$Version-x64.msix"
    foreach ($pkg in @($msi, $msix)) {
        if (Test-Path $pkg) {
            & $signtool @signArgs $pkg
            if ($LASTEXITCODE -ne 0) { Write-Error "Signing failed for $pkg"; exit 1 }
            Write-Host "      Signed $pkg" -ForegroundColor Green
        }
    }
} else {
    Write-Host ""
    Write-Host "[8/8] Skipping signing (pass -Sign with -CertPfx / -CertPassword)." -ForegroundColor DarkGray
}

# ---- Done -----------------------------------------------------------------
Write-Host ""
Write-Host "==========================================" -ForegroundColor Green
Write-Host " Release build complete!" -ForegroundColor Green
Write-Host "==========================================" -ForegroundColor Green
Write-Host ""
Write-Host " Executable : $exe"
if ($MakeMsi)  { Write-Host " MSI       : $projectRoot\dist\DocuSearch-Setup-$Version.msi" }
if ($MakeMsix) { Write-Host " MSIX      : $projectRoot\dist\DocuSearch-$Version-x64.msix" }
if ($Zip)      { Write-Host " ZIP       : $projectRoot\dist\DocuSearch-$Version-portable.zip" }
Write-Host ""
Write-Host "Install the MSI by double-clicking it." -ForegroundColor Cyan
Write-Host "Install the MSIX via:" -ForegroundColor Cyan
Write-Host "  PowerShell> Add-AppxPackage $projectRoot\dist\DocuSearch-$Version-x64.msix" -ForegroundColor Cyan
Write-Host ""
