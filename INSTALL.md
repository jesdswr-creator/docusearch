# DocuSearch — Install Guide

**Offline Intelligent Document Search & OCR System for Windows 11**

C++20 · Qt 6 Widgets · SQLite + FTS5 · Tesseract OCR · Poppler PDF

---

## Three Ways to Install

### Option A — Build it yourself (recommended)

1. Unzip `DocuSearch-1.0.0-source.zip` to a folder like `C:\dev\docusearch`.
2. Follow the **Quick Start** section in `docusearch/BUILD.md` to install prerequisites (Visual Studio 2022, vcpkg, Qt 6, WiX).
3. Open the **x64 Native Tools Command Prompt for VS 2022**, then:
   ```bat
   cd C:\dev\docusearch\docusearch
   install.bat
   ```
4. When `install.bat` finishes, it opens the `dist\` folder containing:
   - `DocuSearch-Setup-1.0.0.0.msi` — classic Windows installer
   - `DocuSearch-1.0.0.0-x64.msix` — modern MSIX package
   - `DocuSearch-1.0.0.0-portable.zip` — portable, no-install build

Double-click the **.msi** to install — DocuSearch appears in the Start Menu and Add/Remove Programs.

Or install the MSIX via PowerShell (Admin):
```powershell
Add-AppxPackage .\DocuSearch-1.0.0.0-x64.msix
```

### Option B — Portable (no install)

If you only have the portable ZIP, just unzip it anywhere and run `DocuSearch.exe`. No installation required — the SQLite database is stored in `%APPDATA%\DocuSearch\`.

### Option C — From source (manual)

```bat
cd C:\dev\docusearch\docusearch
cmake -B build -S . ^
    -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake ^
    -DCMAKE_PREFIX_PATH=C:\Qt\6.7.0\msvc2022_64 ^
    -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Release --parallel
build\bin\Release\DocuSearch.exe
```

---

## What's in this ZIP

```
docusearch/
├── install.bat                 ← One-click Windows build + package script
├── BUILD.md                    ← Full build & setup guide (read this!)
├── README.md                   ← Project overview
├── CMakeLists.txt              ← CMake build (Qt6, SQLite, Tesseract, Poppler)
├── vcpkg.json                  ← C++ dependency manifest
├── installer/
│   ├── DocuSearch.wxs          ← WiX v4 MSI definition
│   └── AppxManifest.xml        ← MSIX package manifest
├── resources/
│   ├── DocuSearch.exe.manifest ← Win11 manifest (PerMonitorV2, ComCtl v6, UTF-8)
│   ├── app.rc                  ← Embeds manifest + icon + version info
│   ├── app.qrc                 ← Qt resource bundle (splash + themes)
│   ├── icons/
│   │   ├── DocuSearch.ico      ← Multi-resolution app icon (16..256px)
│   │   └── DocuSearch-256.png
│   ├── images/splash.png       ← Startup splash screen
│   └── themes/                 ← QSS theme overrides
├── scripts/
│   ├── build.ps1               ← Developer build script
│   └── build-release.ps1       ← Full release pipeline (MSI + MSIX + ZIP + sign)
├── src/
│   ├── main.cpp                ← Win11 entry point (splash + Mica + jump list)
│   ├── core/                   ← Logger, Config, StringUtils, FileUtils, Types
│   ├── database/               ← SQLite + FTS5 wrapper, schema, repository
│   ├── documents/              ← PDF / DOCX / XLSX / PPTX / Text extractors
│   ├── ocr/                    ← Tesseract engine + CPU-throttled worker pool
│   ├── indexer/                ← Two-phase indexer + priority scheduler
│   ├── search/                 ← Query parser + FTS5 search engine
│   ├── monitoring/             ← ReadDirectoryChangesW file watcher
│   ├── preview/                ← Thumbnail generator
│   ├── settings/               ← Settings manager
│   ├── backup/                 ← Backup / restore
│   ├── win/                    ← Native Win11 (JumpList via COM)
│   └── ui/                     ← MainWindow + 8 panes + Win11 Fluent theme
└── tests/                      ← Qt Test unit suite (~75 test cases)
```

---

## System Requirements

| Component         | Requirement                                  |
|-------------------|----------------------------------------------|
| Operating system  | Windows 10 1809+ (Windows 11 recommended)    |
| Architecture      | x64                                           |
| RAM               | 8 GB minimum (for indexing large drives)     |
| Disk              | ~200 MB for the app + DB grows with content  |
| Build tools (dev) | Visual Studio 2022, Qt 6.7+, vcpkg, CMake 3.21+ |

**Tesseract OCR** requires `*.traineddata` files in one of:
- `%TESSDATA_PREFIX%` (env var)
- A folder you specify in **Settings → OCR**
- `C:\Program Files\Tesseract-OCR\tessdata`

Download `eng.traineddata` from <https://github.com/tesseract-ocr/tessdata>.

---

## First Launch

1. Open **Tools → Settings → Indexing**
2. Add the drives/folders you want to index (e.g. `D:\`)
3. Add excluded folders (e.g. `D:\Movies`, `D:\Games`)
4. Click OK — indexing starts automatically in the background
5. The search bar is live as soon as Phase 1 completes (a few seconds for filename search)

---

## Windows 11 Features

- ✅ PerMonitorV2 DPI awareness (crisp on 4K / HiDPI)
- ✅ Common Controls v6 (modern visual styles)
- ✅ UTF-8 active code page
- ✅ Mica backdrop (title bar blends with desktop accent tint, Win11 22000+)
- ✅ Dark / light title bar follows theme
- ✅ Multi-resolution app icon (.ico with 7 sizes)
- ✅ Splash screen on startup
- ✅ Taskbar jump list (Open Settings, Pause/Resume indexing, Rebuild)
- ✅ File associations for `.pdf`, `.docx`, `.xlsx`, `.pptx`
- ✅ Start Menu + Desktop shortcuts (MSI install)
- ✅ Add/Remove Programs entry with proper uninstall
- ✅ Microsoft Store-ready MSIX package
- ✅ Fluent Design QSS theme (8 px rounded, Win11 accent blue)

---

## Troubleshooting

See `docusearch/BUILD.md` §10 for common issues:
- "Cannot find Qt6"
- "Tesseract init failed"
- "MSIX install fails with 0x80073CFF" (sideloading disabled)
- "Mica backdrop doesn't show" (need Win11 22000+)
- "File system access denied (MSIX)" — grant in Settings → Privacy

---

## License

DocuSearch source: MIT-style. Bundled libraries retain their original
licenses (Qt: LGPL/Commercial, SQLite: Public Domain, Tesseract: Apache 2.0,
Poppler: GPL).

---

**Need help?** Read `docusearch/BUILD.md` for the full build walkthrough, or run `install.bat` for the automated path.
