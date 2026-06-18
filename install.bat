@echo off
REM ============================================================
REM  DocuSearch — One-Click Windows 11 Install Script
REM ============================================================
REM
REM  WHAT THIS DOES:
REM    1. Checks that Visual Studio 2022, vcpkg, Qt 6, and WiX are installed
REM    2. Configures CMake (with vcpkg + Qt)
REM    3. Builds DocuSearch.exe in Release mode
REM    4. Bundles Qt DLLs via windeployqt
REM    5. Produces the MSI installer (DocuSearch-Setup-1.0.0.0.msi)
REM    6. Produces the MSIX package (DocuSearch-1.0.0.0-x64.msix)
REM    7. Opens the dist\ folder so you can double-click the installer
REM
REM  PREREQUISITES (one-time setup, see BUILD.md for details):
REM    - Visual Studio 2022 with "Desktop development with C++"
REM    - vcpkg cloned somewhere, with VCPKG_ROOT env var set
REM    - Qt 6.7+ installed (msvc2022_64), QtPath env var set
REM    - WiX v4:  dotnet tool install -g wix
REM    - Windows SDK (signtool, makeappx) — comes with VS 2022
REM
REM  USAGE:
REM    1. Unzip DocuSearch-1.0.0-source.zip to C:\dev\docusearch
REM    2. Open "x64 Native Tools Command Prompt for VS 2022"
REM    3. cd C:\dev\docusearch
REM    4. install.bat
REM
REM  After it finishes, look in dist\ for the .msi and .msix.
REM ============================================================

setlocal enabledelayedexpansion
set "ROOT=%~dp0"
cd /d "%ROOT%"

echo.
echo ==========================================================
echo  DocuSearch - Windows 11 Install Script
echo ==========================================================
echo  Project: %ROOT%
echo.

REM ---- 1. Locate prerequisites --------------------------------------------

where cmake >nul 2>&1
if errorlevel 1 (
    echo [ERROR] cmake not found on PATH.
    echo         Install CMake 3.21+ or open "x64 Native Tools Command Prompt for VS 2022".
    exit /b 1
)

if "%VCPKG_ROOT%"=="" (
    echo [ERROR] VCPKG_ROOT environment variable is not set.
    echo         Install vcpkg and run: setx VCPKG_ROOT "C:\dev\vcpkg"
    exit /b 1
)
if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
    echo [ERROR] %VCPKG_ROOT% does not look like a vcpkg checkout.
    exit /b 1
)

if "%QtPath%"=="" (
    if exist "C:\Qt\6.7.0\msvc2022_64" (
        set "QtPath=C:\Qt\6.7.0\msvc2022_64"
    ) else (
        echo [ERROR] QtPath env var not set and default C:\Qt\6.7.0\msvc2022_64 not found.
        echo         Install Qt 6.7+ and run: setx QtPath "C:\Qt\6.7.0\msvc2022_64"
        exit /b 1
    )
)
if not exist "%QtPath%\bin\qmake.exe" (
    echo [ERROR] Qt not found at: %QtPath%
    exit /b 1
)

where wix >nul 2>&1
if errorlevel 1 (
    echo [WARN] WiX v4 not found on PATH - MSI step will be skipped.
    echo        Install with: dotnet tool install -g wix
    set "HAS_WIX=0"
) else (
    set "HAS_WIX=1"
)

echo  VCPKG_ROOT : %VCPKG_ROOT%
echo  QtPath     : %QtPath%
echo  WiX v4     : %HAS_WIX%
echo ==========================================================
echo.

REM ---- 2. Configure -------------------------------------------------------

echo [1/5] Configuring CMake...
cmake -B build -S . ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
    -DCMAKE_PREFIX_PATH="%QtPath%" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows
if errorlevel 1 (
    echo [ERROR] CMake configure failed.
    exit /b 1
)

REM ---- 3. Build ------------------------------------------------------------

echo.
echo [2/5] Building DocuSearch (Release)...
cmake --build build --config Release --parallel
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

if not exist "build\bin\Release\DocuSearch.exe" (
    echo [ERROR] Build reported success but DocuSearch.exe was not produced.
    exit /b 1
)

REM ---- 4. windeployqt ------------------------------------------------------

echo.
echo [3/5] Bundling Qt runtime DLLs (windeployqt)...
"%QtPath%\bin\windeployqt.exe" --release --no-translations ^
    --no-system-d3d-compiler --no-opengl-sw --no-quick-import ^
    --no-webkit2 --compiler-runtime ^
    "build\bin\Release\DocuSearch.exe"
if errorlevel 1 (
    echo [ERROR] windeployqt failed.
    exit /b 1
)

REM Copy theme overrides next to the exe
if not exist "build\bin\Release\themes" mkdir "build\bin\Release\themes"
copy /Y "resources\themes\*.qss" "build\bin\Release\themes\" >nul

REM ---- 5. Package MSI + MSIX via PowerShell --------------------------------

echo.
echo [4/5] Packaging MSI + MSIX via build-release.ps1...

set "PS_ARGS=-MakeMsix -Zip"
if "%HAS_WIX%"=="1" set "PS_ARGS=-MakeMsi -MakeMsix -Zip"

powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\build-release.ps1" %PS_ARGS% -BuildDir build -Version 1.0.0.0
if errorlevel 1 (
    echo [ERROR] Packaging failed.
    echo         You can still run the bare .exe at: build\bin\Release\DocuSearch.exe
    exit /b 1
)

REM ---- 6. Open the dist folder --------------------------------------------

echo.
echo [5/5] Done. Opening dist\ folder...
if not exist dist mkdir dist
explorer dist

echo.
echo ==========================================================
echo  Installation packages ready in: %ROOT%dist\
echo ==========================================================
echo  - DocuSearch-Setup-1.0.0.0.msi           (double-click to install)
echo  - DocuSearch-1.0.0.0-x64.msix            (Add-AppxPackage)
echo  - DocuSearch-1.0.0.0-portable.zip        (no-install build)
echo.
echo  Or run the bare executable directly:
echo      build\bin\Release\DocuSearch.exe
echo ==========================================================
echo.
pause
endlocal
