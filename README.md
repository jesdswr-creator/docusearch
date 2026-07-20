# DocuSearch

**Offline Intelligent Document Search & OCR System for Windows 10/11**

A professional, completely offline C++20 / Qt 6 desktop application that indexes
your drives, extracts text from documents (PDF, DOCX, XLSX, PPTX), OCRs scanned
PDFs and images (oneocr), and provides instant full-text search — all without
any cloud dependency. Your data never leaves your machine.

## Key Features

- **Full-text search** powered by SQLite FTS5 with BM25 ranking
- **Advanced query syntax**: phrases, boolean (AND/OR/NOT), field filters
  (`type:pdf`, `folder:Railway`, `date:>2024-01-01`)
- **PDF text extraction** via Poppler (born-digital PDFs) + **OCR** via
  oneocr (the Windows 11 Snipping Tool's OCR engine — scanned PDFs and images)
- **Auto-scan every 1 hour** — detects new and modified files automatically
- **Tags, notes, favorites, saved searches** — organize your way
- **Duplicate detection** by SHA-256 hash
- **Backup / restore** — database + tags + notes + saved searches
- **Dark & light themes** with Windows 11 Fluent Design styling
- **Per-monitor V2 DPI awareness** — crisp text on 1080p and 4K displays
- **Professional MSI installer** with EULA license agreement
- **Completely offline** — no telemetry, no cloud, no internet required

## Supported File Types

| Category | Extensions |
|----------|-----------|
| Documents | PDF, DOC/DOCX, XLS/XLSX, PPT/PPTX, TXT, RTF, CSV, MD |
| Images (OCR) | JPG, PNG, TIFF, BMP, GIF, WebP |

## How It Works

1. **Add Folder** — Index → Add Folder to Index (or auto-san every hour)
2. **Extract Content** — Index → Extract Content Now (or click the Extract
   button in the toolbar). This extracts text from all documents and runs OCR
   on scanned PDFs / images.
3. **Search** — Type in the search bar. Results appear instantly with
   snippet previews. Click a result to see metadata, tags, notes, and the
   full extracted text in the preview pane.

## Technology Stack

| Component | Technology |
|-----------|-----------|
| Language | C++20 |
| UI Framework | Qt 6.7 (Widgets) |
| Database | SQLite 3 + FTS5 (full-text search) |
| OCR | oneocr.dll (from Windows 11 Snipping Tool) |
| PDF | Poppler (cpp binding) |
| Build | CMake + vcpkg (manifest mode) |
| Installer | WiX v4 (MSI) |
| CI | GitHub Actions (Windows Server 2022) |

## Download

Download the latest build from [GitHub Actions](https://github.com/jesdswr-creator/docusearch/actions):
- **DocuSearch-Setup-msi** — MSI installer (recommended, includes EULA)
- **DocuSearch-portable** — Portable ZIP (no installation required)

## OCR Setup (oneocr)

DocuSearch uses **oneocr.dll** — the native OCR engine from the Windows 11
Snipping Tool — for text recognition. The DLL is **not bundled**; you install
it from your own locally-installed Snipping Tool:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\get_oneocr.ps1
```

See **[ONEOCR_SETUP.md](ONEOCR_SETUP.md)** for full instructions and troubleshooting.

## Build from Source

See **[BUILD.md](BUILD.md)** for full build instructions.

```bat
:: Configure (vcpkg manifest mode auto-installs all dependencies)
cmake -B build -S . ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake ^
  -DCMAKE_PREFIX_PATH=C:\Qt\6.7.3\msvc2019_64 ^
  -DVCPKG_TARGET_TRIPLET=x64-windows

:: Build
cmake --build build --config Release --parallel
```

## Architecture

```
src/
  core/         Config, Logger, FileUtils, Constants, Types
  database/     Database (RAII SQLite), FileRepository, Schema
  documents/    DocumentExtractorRegistry, PdfExtractor, DocxExtractor, ...
  ocr/          WindowsOcrEngine (oneocr.dll wrapper), ocr_helper_main.cpp
  indexer/      ContentIndexer, MetadataIndexer, PriorityScheduler
  search/       SearchEngine, QueryParser (AST-based)
  preview/      ThumbnailGenerator
  monitoring/   FileWatcher (ReadDirectoryChangesW)
  backup/       BackupManager
  settings/     SettingsManager
  ui/           MainWindow, SearchBar, ResultsPane, PreviewPane,
                MetadataPane, TagsNotesPane, SettingsDialog, Theme
  win/          JumpList
```

## License

BSD 3-Clause. See [installer/LICENSE.rtf](installer/LICENSE.rtf) for the full
EULA including third-party license notices.

---

*DocuSearch — by MinZ*
