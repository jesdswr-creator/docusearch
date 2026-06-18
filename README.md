# DocuSearch

**Offline Intelligent Document Search & OCR System for Windows**

A professional, completely offline C++20/Qt 6 desktop application that indexes
entire drives, performs OCR on scanned documents and images, and provides
instant full-text search — all without any cloud dependency.

## Highlights

- **Two-phase indexing**
  - Phase 1: fast recursive metadata scan → filename search works immediately
  - Phase 2: background content extraction + OCR queue
- **Priority queue**: recent files (last 30 days) → last year → older → archives
- **Full-text search** powered by SQLite FTS5 with bm25 ranking
- **Advanced query syntax**: phrases, boolean, field filters (`type:pdf`,
  `folder:Railway`, `date:2026`), exclusion
- **Tesseract OCR** in a CPU-throttled worker pool (auto-pause on heavy load)
- **Live file monitoring** via Windows `ReadDirectoryChangesW` — never rescans
- **Lazy OCR**: if a file isn't indexed yet, OCR it on demand at search time
- **Tags, notes, favorites, saved searches**
- **Duplicate detection** by hash + filename similarity
- **Backup / restore** (database + tags + notes + saved searches)
- **Dark & light themes** with professional QSS styling
- **Non-blocking UI** — all long tasks run in QThreads

## Supported File Types

| Documents | Images |
|-----------|--------|
| PDF, DOC/DOCX, XLS/XLSX, PPT/PPTX, TXT, RTF, CSV, MD | JPG, PNG, TIFF, BMP, GIF, WebP |

## Performance Targets

| Metric | Target |
|--------|--------|
| Application startup | < 2 s |
| Search latency | < 100 ms |
| Indexing CPU (background) | ≤ 30 % (configurable) |
| Memory footprint | < 200 MB |
| Tested on | Intel i3/i5, 8 GB RAM, HDD or SSD |

## Quick Start

See **[BUILD.md](BUILD.md)** for full build instructions.

```bat
:: Configure
cmake -B build -S . ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake ^
  -DCMAKE_PREFIX_PATH=C:\Qt\6.7.0\msvc2022_64

:: Build
cmake --build build --config Release --parallel

:: Bundle Qt DLLs
C:\Qt\6.7.0\msvc2022_64\bin\windeployqt.exe --release build\bin\Release\DocuSearch.exe
```

## Project Structure

```
docusearch/
├── CMakeLists.txt
├── vcpkg.json
├── BUILD.md
├── README.md
├── src/
│   ├── main.cpp
│   ├── core/          Logger, Config, Constants, Types, StringUtils, FileUtils
│   ├── database/      Database, Schema, FileRepository (SQLite + FTS5)
│   ├── documents/     IDocumentExtractor + PDF/DOCX/XLSX/PPTX/Text extractors
│   ├── ocr/           OcrEngine + OcrWorkerPool (Tesseract, CPU throttled)
│   ├── indexer/       MetadataIndexer + ContentIndexer + PriorityScheduler + Indexer
│   ├── search/        QueryParser + SearchEngine (FTS5)
│   ├── monitoring/    FileWatcher (ReadDirectoryChangesW)
│   ├── preview/       ThumbnailGenerator
│   ├── settings/      SettingsManager
│   ├── backup/        BackupManager
│   └── ui/            MainWindow + 8 panes + Theme
└── resources/
    ├── app.qrc
    └── themes/
```

## Design Principles

- **SOLID** architecture — every module has a single responsibility
- **RAII** everywhere — no manual `new`/`delete`, smart pointers throughout
- **Thread-safe** — atomics for counters, mutexes for queues, queued signal delivery
- **Exception-safe** — try/catch at API boundaries, no exceptions cross thread borders
- **Modular** — swap any layer (e.g., replace Tesseract with another OCR engine) by
  reimplementing one interface
- **Tested** — Qt Test suite covering string/file utils, query parser, priority
  scheduler, and the full FileRepository data-access layer against an in-memory
  SQLite database. Enable with `-DDOCUSEARCH_BUILD_TESTS=ON` and run via `ctest`.

## License

Source code: MIT-style — see source headers.
Bundled libraries retain their original licenses (Qt, SQLite, Tesseract, Poppler).
