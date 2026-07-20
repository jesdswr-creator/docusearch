# DocuSearch FAQ

Frequently asked questions about DocuSearch.

---

## General

### Q: Is DocuSearch free?

Yes — DocuSearch is open source under the BSD 3-Clause license. You
can use it freely for personal and commercial purposes.

### Q: Does DocuSearch send my data to the cloud?

**No.** DocuSearch is 100% offline. No telemetry, no analytics, no
cloud sync. Your documents never leave your machine.

### Q: What platforms does DocuSearch support?

- Windows 10 (1809+) — 64-bit only
- Windows 11 — 64-bit

There are no macOS or Linux builds at this time.

### Q: What are the system requirements?

- Windows 10/11 64-bit
- 4 GB RAM minimum (8 GB recommended for large folders)
- 500 MB free disk space (plus space for your indexed files)
- Microsoft Snipping Tool installed (for OCR support — see below)

### Q: How is DocuSearch different from Windows Search?

- DocuSearch is **fully offline** — no Bing results, no web suggestions.
- DocuSearch extracts text from PDFs, DOCX, XLSX, PPTX natively
  (Windows Search relies on iFilters which are often missing).
- DocuSearch runs **OCR on scanned PDFs and images** using the
  same engine as the Windows 11 Snipping Tool.
- DocuSearch supports **advanced search syntax** (phrases, boolean,
  field filters).
- DocuSearch has **tags, notes, favorites, saved searches**.

---

## OCR

### Q: Why do I need to run `get_oneocr.ps1` for OCR?

The `oneocr.dll`, `oneocr.onemodel`, and `onnxruntime.dll` files are
Microsoft proprietary binaries that ship with the Windows 11 Snipping
Tool. DocuSearch **cannot redistribute them legally**, so each user
installs them from their own locally-installed Snipping Tool.

The `get_oneocr.ps1` script automates this — it locates your Snipping
Tool installation and copies the three files into the DocuSearch
folder.

### Q: Can I use DocuSearch without OCR?

Yes. OCR is optional. Without OCR, DocuSearch still:
- Indexes all file metadata (filename, size, dates)
- Extracts text from born-digital PDFs, DOCX, XLSX, PPTX, TXT
- Provides full-text search

Only scanned PDFs and images require OCR.

### Q: What languages does OCR support?

The oneocr model auto-detects:
- English
- Chinese (Simplified & Traditional)
- Korean
- Japanese

### Q: OCR is slow. Is that normal?

OCR is computationally expensive. A single page typically takes 1-3
seconds; a 10-page PDF takes 10-30 seconds. This is normal for any
OCR engine running locally on a CPU.

If OCR is consistently very slow:
- Close other CPU-heavy applications
- Make sure your images are not larger than needed (>200 DPI rarely
  improves accuracy but doubles processing time)
- For very large PDFs, consider splitting them into smaller files

### Q: OCR returned empty text for a clearly readable image. Why?

Possible causes:
1. Image is smaller than 50×50 pixels (oneocr's minimum).
2. Image is larger than 10000×10000 pixels (oneocr's maximum).
3. Image is mostly decorative (gradient, blurry, low contrast).
4. Text in the image is rotated 90°/180° — oneocr handles small
   rotations but not extreme angles.
5. Image is a screenshot of code with very thin fonts — try
   increasing the DPI/resolution.

### Q: Can I use a different OCR engine (Tesseract, etc.)?

The source code is open — you can integrate any OCR engine you like.
The current implementation calls oneocr.dll via a separate helper
process (`docusearch_ocr_helper.exe`). Replacing it would mean
rewriting that helper to call Tesseract or another engine.

---

## Search

### Q: My search returns no results. Why?

Most common causes:
1. **You haven't extracted text yet.** After adding a folder, click
   the **Extract** button. Files are not searchable until extracted.
2. **The file was too large.** Files >100 MB are skipped. Files
   with >500 KB of text are partially indexed.
3. **The file format isn't supported.** DocuSearch handles PDF,
   DOCX, XLSX, PPTX, TXT, RTF, CSV, MD, and images (via OCR).
4. **The query syntax is wrong.** Try a simple single-word query
   first to verify search works at all.

### Q: Why does search highlighting not work?

Search highlighting was **disabled** in a recent build because it
was crashing on large documents. A crash-safe highlighter has been
implemented but is currently **off by default**. It will be enabled
in a future release once we're confident it doesn't reintroduce
crashes.

### Q: Can I search for files in a specific folder?

Yes — use the `folder:` filter:

```
invoice folder:Railway
```

This finds files containing "invoice" in any folder whose path
contains "Railway" (case-insensitive).

### Q: Can I search by date?

Yes:

```
date:2024                    → files modified in 2024
date:>2024-01-01             → files modified after Jan 1, 2024
date:<2024-12-31             → files modified before Dec 31, 2024
```

### Q: Can I search by file size?

Yes:

```
size:>10MB                   → files larger than 10 MB
size:<100KB                  → files smaller than 100 KB
```

---

## Extraction

### Q: Why are some files marked as "needs_ocr"?

When DocuSearch extracts text from a PDF and gets no text back, it
usually means the PDF is a scanned image (no embedded text layer).
These files are marked `needs_ocr` so you can OCR them later via
the green OCR button.

### Q: Why was my large PDF only partially indexed?

To protect low-RAM systems, DocuSearch caps text extraction at
500 KB per file (~150 pages of typical text). The first 500 KB is
fully searchable; the remainder is skipped. You can change this
limit in `src/core/Constants.h` and rebuild.

### Q: Extraction crashed. Will it corrupt my database?

No. DocuSearch uses SQLite with WAL journaling and per-file
transactions. A crash during extraction will at most lose the file
currently being processed — your database remains intact.

### Q: Can I extract text from password-protected PDFs?

No. DocuSearch cannot extract text from encrypted/password-protected
PDFs. The file will be flagged as failed.

---

## Performance

### Q: Indexing is slow on my 4 GB machine. What can I do?

1. Reduce worker threads to 1 (Settings → Indexing).
2. Lower the CPU pause threshold (e.g., 50% instead of 70%).
3. Index folders in smaller batches (don't add a 50,000-file folder
   all at once).
4. Disable duplicate hash computation (Settings → Indexing → uncheck
   "Compute SHA-256 hashes").

### Q: DocuSearch is using too much memory.

Memory usage depends on:
- Number of indexed files (each adds ~1 KB to the database)
- Size of extracted text (capped at 500 KB per file)
- Number of open preview documents

If memory is excessive, try:
- Closing the preview pane when not needed
- Reducing the SQLite cache size (requires rebuild — see
  `src/core/Constants.h`)
- Indexing fewer files

### Q: Can I run DocuSearch from a USB stick?

Yes — extract the portable ZIP to a USB stick and run DocuSearch.exe
directly. The database is stored in `%APPDATA%\DocuSearch\` on the
host machine, not the USB stick, so your index persists across
machines only if you manually copy the database.

---

## Privacy & Security

### Q: Does DocuSearch collect any telemetry?

**No.** DocuSearch has zero telemetry. No usage statistics, no crash
reports, no anonymous IDs, no phone-home behavior. Period.

### Q: Where is my data stored?

- Database: `%APPDATA%\DocuSearch\docusearch.db`
- Logs: `%APPDATA%\DocuSearch\logs\`
- Thumbnails: `%APPDATA%\DocuSearch\thumbnails\`

These are local files on your machine. No cloud, no sync.

### Q: Can DocuSearch index files on a network drive?

Yes, but performance may be poor. Network file enumeration is much
slower than local. If you must index a network share, map it to a
drive letter first.

### Q: Is the OCR model key safe to hardcode?

Yes — see [docs/OCR_LICENSING.md](docs/OCR_LICENSING.md) for the
full explanation. The key is a public sample from Microsoft's
oneocr.py reference implementation. It is not machine-specific,
not rate-limited, not logged, and not used for any cloud service.

---

## Build & Development

### Q: How do I build DocuSearch from source?

See **[BUILD.md](BUILD.md)** for full build instructions.

Quick version:
```bat
cmake -B build -S . ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake ^
  -DCMAKE_PREFIX_PATH=C:\Qt\6.7.3\msvc2019_64 ^
  -DVCPKG_TARGET_TRIPLET=x64-windows

cmake --build build --config Release --parallel
```

### Q: Can I contribute?

Yes! Pull requests welcome at
[github.com/jesdswr-creator/docusearch](https://github.com/jesdswr-creator/docusearch).

### Q: How do I run the tests?

```bat
cmake -B build -S . -DDOCUSEARCH_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

Tests include:
- `tst_StringUtils` — string utilities
- `tst_FileUtils` — file utilities
- `tst_QueryParser` — search query parser
- `tst_PriorityScheduler` — indexer scheduler
- `tst_FileRepository` — database repository
- `tst_FtsTokenizer` — FTS5 tokenizer behavior
- `tst_OcrHelper` — OCR helper ABI + crash safety
- `tst_ExtractorFuzz` — extractor fuzz testing (verifies SEH protection)

---

## Still have questions?

- Browse the [HELP.md](HELP.md) for full usage docs
- Check [GitHub Issues](https://github.com/jesdswr-creator/docusearch/issues)
  for known problems
- Open a new issue if your question isn't covered here
