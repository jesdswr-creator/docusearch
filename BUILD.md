# DocuSearch — Build & Setup Guide

**Offline Intelligent Document Search & OCR System for Windows 11**

C++20 · Qt 6 Widgets · SQLite + FTS5 · Tesseract OCR · Poppler PDF

---

## 0. Quick Start — One-Shot Windows 11 Installer

If you just want the .msi / .msix and don't care about the details:

```bat
:: One-time setup
git clone https://github.com/microsoft/vcpkg.git C:\dev\vcpkg
C:\dev\vcpkg\bootstrap-vcpkg.bat
setx VCPKG_ROOT "C:\dev\vcpkg"
pip install aqtinstall
aqt install-qt windows desktop 6.7.0 win64_msvc2022_64 -m qtcore qtgui qtwidgets qtconcurrent qtsql qtsvg
setx QtPath "C:\Qt\6.7.0\msvc2022_64"
dotnet tool install -g wix                :: WiX v4 (for MSI)

:: Build + bundle + package + sign
cd C:\path\to\docusearch
.\scripts\build-release.ps1 -MakeMsi -MakeMsix -Zip -Sign -CertPfx C:\certs\MyCode.pfx -CertPassword secret
```

Outputs land in `dist\`:
- `DocuSearch-Setup-1.0.0.0.msi`   — classic Windows installer
- `DocuSearch-1.0.0.0-x64.msix`    — modern MSIX package
- `DocuSearch-1.0.0.0-portable.zip`— portable, no-install build

Skip ahead to §4 to just run the .exe, or read on for the full details.

---

## 1. Prerequisites

### 1.1 Toolchain (Windows)
- **Visual Studio 2022** with the *Desktop development with C++* workload
  (provides MSVC, Windows SDK).
- **CMake ≥ 3.21** (bundled with VS 2022, or install from cmake.org).
- **vcpkg** for C++ dependencies:
  ```bat
  git clone https://github.com/microsoft/vcpkg.git C:\dev\vcpkg
  C:\dev\vcpkg\bootstrap-vcpkg.bat
  setx VCPKG_ROOT "C:\dev\vcpkg"
  ```

### 1.2 Qt 6
Install **Qt 6.7+** via the Qt Online Installer or `aqtinstall`:
```bat
pip install aqtinstall
aqt install-qt windows desktop 6.7.0 win64_msvc2022_64 -m qtcore qtgui qtwidgets qtconcurrent qtsql qtnetwork qtsvg
```
Set `CMAKE_PREFIX_PATH` to your Qt installation, e.g.
`C:\Qt\6.7.0\msvc2022_64`.

### 1.3 C++ libraries (via vcpkg)
```bat
vcpkg install qtbase:x64-windows qtsvg:x64-windows ^
           sqlite3[fts5]:x64-windows ^
           tesseract:x64-windows leptonica:x64-windows ^
           poppler:x64-windows zlib:x64-windows
```

### 1.4 Packaging tools (optional — only for MSI / MSIX)
- **WiX v4** — install as a dotnet tool: `dotnet tool install -g wix`
- **Windows SDK** — provides `makeappx.exe` and `signtool.exe`
  (already installed with Visual Studio's "Desktop development" workload).

---

## 2. Configure & Build

```bat
cd C:\path\to\docusearch
cmake -B build -S . ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake ^
  -DCMAKE_PREFIX_PATH=C:\Qt\6.7.0\msvc2022_64 ^
  -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Release --parallel
```

The executable is produced at `build\bin\Release\DocuSearch.exe`.

---

## 3. Runtime Dependencies

### 3.1 Tesseract tessdata
Download `eng.traineddata` (and any other languages) from
<https://github.com/tesseract-ocr/tessdata> and place in one of:
- `%TESSDATA_PREFIX%` (environment variable)
- A folder you specify in **Settings → OCR**
- The default location `C:\Program Files\Tesseract-OCR\tessdata`

### 3.2 Qt runtime DLLs
Run `windeployqt` on the built executable to bundle the Qt DLLs:
```bat
C:\Qt\6.7.0\msvc2022_64\bin\windeployqt.exe --release build\bin\Release\DocuSearch.exe
```

The `build-release.ps1` script runs `windeployqt` automatically.

### 3.3 Optional: SQLite amalgamation
If `find_package(SQLite3)` fails, drop the latest `sqlite3.c` + `sqlite3.h`
into `third_party/sqlite3/` and the CMake script will build it as a static
library with FTS5 enabled.

---

## 4. Running

### 4.1 Bare .exe (developer mode)

```bat
build\bin\Release\DocuSearch.exe
```

On first launch:
1. Open **Tools → Settings → Indexing**
2. Add the drives/folders you want to index (e.g. `D:\`)
3. Add excluded folders (e.g. `D:\Movies`)
4. Click OK — indexing starts automatically
5. Search bar is live after Phase 1 completes (a few seconds for filename search)

### 4.2 Installed MSI (end-user mode)

After running `DocuSearch-Setup-1.0.0.0.msi`:
- DocuSearch is installed to `C:\Program Files\DocuSearch\`
- A Start Menu shortcut is created
- An optional Desktop shortcut is created
- File associations for `.pdf`, `.docx`, `.xlsx`, `.pptx` are added to
  the "Open with" menu (right-click any such file)
- An entry appears in **Settings → Apps → Installed apps** with
  Uninstall support

Launch via the Start Menu shortcut, or:
```bat
"C:\Program Files\DocuSearch\bin\DocuSearch.exe"
```

### 4.3 MSIX package (modern Windows 11 install)

```powershell
Add-AppxPackage .\DocuSearch-1.0.0.0-x64.msix
```

After install:
- DocuSearch appears in the Start Menu with proper tile + splash screen
- The app shows up in **Settings → Apps → Installed apps** as a
  packaged app (with proper icon, publisher, and uninstall)
- File associations are registered through the package manifest
- On first launch, Windows will prompt you to grant **File system**
  permission (needed so the indexer can walk `D:\`, `E:\`, etc.) —
  accept it. You can change this later in
  **Settings → Privacy & security → File system**

To uninstall:
```powershell
Get-AppxPackage DocuSearch.DocuSearch | Remove-AppxPackage
```

---

## 5. Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                         UI (Qt Widgets)                      │
│  MainWindow · SearchBar · ResultsPane · PreviewPane ·        │
│  MetadataPane · TagsNotesPane · IndexingProgress ·           │
│  SettingsDialog · Theme                                      │
└─────────────────────────────────────────────────────────────┘
                              │
        ┌─────────────────────┼─────────────────────┐
        ▼                     ▼                     ▼
   SearchEngine          Indexer              FileWatcher
   (FTS5 query)     (Phase 1 + 2)    (ReadDirectoryChangesW)
        │                     │                     │
        ▼                     ▼                     ▼
   ┌─────────────────────────────────────────────────────┐
   │                 FileRepository                       │
   │   (Files, DocumentText, Tags, Notes, SavedSearches)  │
   └─────────────────────────────────────────────────────┘
                              │
                              ▼
                  ┌──────────────────────┐
                  │   Database (SQLite)  │
                  │   + FTS5 virtual tbl │
                  └──────────────────────┘

   Document extractors        OcrEngine + OcrWorkerPool
   (PDF / DOCX / XLSX /       (Tesseract, per-thread,
    PPTX / Text / RTF)         CPU-throttled)

   Windows 11 native layer
   ├─ app.rc            — embeds manifest + icon + version info
   ├─ DocuSearch.exe.manifest — PerMonitorV2, Common Controls v6, UTF-8
   ├─ src/win/JumpList  — native COM ICustomDestinationList
   └─ main.cpp          — Mica backdrop via DwmSetWindowAttribute
```

---

## 6. Windows 11 Integration

DocuSearch is a proper Windows 11 citizen:

| Feature                       | Implementation                                              |
|-------------------------------|-------------------------------------------------------------|
| Per-Monitor V2 DPI awareness  | `DocuSearch.exe.manifest` + `setHighDpiScaleFactorRoundingPolicy` |
| Common Controls v6            | Manifest dependency on `Microsoft.Windows.Common-Controls` |
| UTF-8 active code page        | `<activeCodePage>UTF-8</activeCodePage>` in manifest       |
| Mica backdrop (title bar)     | `DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE, …)`      |
| Dark / light title bar        | `DWMWA_USE_IMMERSIVE_DARK_MODE` toggled per theme          |
| Multi-resolution app icon     | `resources/icons/DocuSearch.ico` (16..256 px)              |
| Splash screen                 | `resources/images/splash.png` embedded in RCC              |
| Taskbar jump list             | `src/win/JumpList.{h,cpp}` — native `ICustomDestinationList` |
| File associations             | Both WiX (`.pdf`, `.docx`, `.xlsx`, `.pptx`) AND MSIX manifest |
| Start Menu + Desktop shortcuts| WiX installer (`ApplicationShortcut` + Desktop shortcut)   |
| Add/Remove Programs entry     | WiX `RegistryUninstallEntry` component                     |
| Modern Fluent QSS theme       | `Theme.cpp` — 8 px rounded, Win11 accent (#005FB8 / #4CC2FF) |
| App User Model ID             | Set on shortcuts so taskbar grouping works correctly       |

---

## 7. Search Query Syntax

| Syntax | Meaning |
|--------|---------|
| `NOC` | Filename or content contains "NOC" |
| `"Executive Lounge"` | Exact phrase |
| `NOC AND examination` | Boolean AND |
| `NOC -draft` | Excludes "draft" |
| `type:pdf` | Filter by extension |
| `folder:Railway` | Path contains "Railway" |
| `date:2026` | Modified in 2026 |
| `tag:Urgent` | Has tag "Urgent" |
| `favorite:1` | Only favorites |
| `ocr:1` | Only OCR-processed files |
| `prefix*` | Prefix wildcard |

---

## 8. Performance Notes

- **Phase 1** scans files at ~5–15k files/sec on HDD, faster on SSD.
- **Phase 2** content extraction runs at ~50–200 docs/sec for small text files,
  ~5–20 docs/sec for PDFs, ~1–5 pages/sec for OCR.
- **Search** is <100 ms for typical queries via FTS5 + bm25 ranking.
- The OCR worker pool throttles automatically when system CPU exceeds the
  threshold (default 70%). Workers sleep proportionally to bring actual CPU
  down to the target (default 30%).
- All long-running operations run in QThreads; the UI thread only renders.
- WAL mode keeps DB writes from blocking reads/searches.

---

## 9. Unit Tests

DocuSearch ships with a Qt Test-based unit-test suite covering the
platform-independent core (string utils, file utils, query parser,
priority scheduler) and the full data-access layer (FileRepository
against an in-memory SQLite database with FTS5 enabled).

### 9.1 Building tests

Tests are opt-in. Enable them at configure time:

```bat
cmake -B build -S . ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake ^
  -DCMAKE_PREFIX_PATH=C:\Qt\6.7.0\msvc2022_64 ^
  -DDOCUSEARCH_BUILD_TESTS=ON
cmake --build build --config Release --parallel
```

### 9.2 Running tests

```bat
ctest --test-dir build --output-on-failure
```

Or run an individual test executable directly:

```bat
build\bin\Release\tst_StringUtils.exe
build\bin\Release\tst_QueryParser.exe
build\bin\Release\tst_FileRepository.exe
build\bin\Release\tst_FileUtils.exe
build\bin\Release\tst_PriorityScheduler.exe
```

### 9.3 What is covered

| Test binary              | Coverage                                                          |
|--------------------------|-------------------------------------------------------------------|
| `tst_StringUtils`        | normalize, fts5Quote, snippet, slugify, levenshtein, jaccard      |
| `tst_FileUtils`          | extensionOf, isUnderAny, sha256OfFile, walkDirectory              |
| `tst_QueryParser`        | phrases, boolean ops, field filters (`type:`, `folder:`, `tag:`…)  |
| `tst_PriorityScheduler`  | P1/P2/P3/P4 bands, edge cases at day boundaries                   |
| `tst_FileRepository`     | upsert, content/FTS, tags, notes, saved searches, dupes, vacuum   |

The full suite runs in well under a second on a normal office PC.

---

## 10. Troubleshooting

**"Cannot find Qt6"** — ensure `CMAKE_PREFIX_PATH` points to the Qt
`msvc2022_64` directory.

**"Tesseract init failed"** — set `TESSDATA_PREFIX` or specify the tessdata
folder in Settings. You need at least `eng.traineddata`.

**"PDF text empty"** — the PDF may be scanned. Enable OCR; the system will
flag `needsOcr` and queue the file.

**Slow first launch** — SQLite is creating the FTS5 index for the first time.
Subsequent launches are <2 seconds.

**High memory** — lower `cache_size` in `Database.cpp` or reduce worker
thread count in Settings.

**"MSIX install fails with error 0x80073CFF"** — sideloading is disabled.
Enable it in **Settings → Privacy & security → For developers → Developer Mode**,
or sign the MSIX with a certificate trusted on the target machine.

**"Mica backdrop doesn't show"** — Mica requires Windows 11 build 22000+.
On Windows 10 the `DwmSetWindowAttribute` call returns an error which is
silently ignored; the app still works but uses a solid background.

**"File system access denied (MSIX)"** — open
**Settings → Privacy & security → File system** and toggle DocuSearch on.
This is required because the indexer needs to walk `D:\`, `E:\`, etc.

---

## 11. License & Credits

DocuSearch is distributed as source code. Bundled libraries retain their
original licenses (Qt: LGPL/Commercial, SQLite: Public Domain,
Tesseract: Apache 2.0, Poppler: GPL).
