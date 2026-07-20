# DocuSearch Release Checklist

Pre-release verification checklist for DocuSearch releases.

---

## Build Verification

- [ ] **CI build is green** on `main` for the release commit
  - URL: https://github.com/jesdswr-creator/docusearch/actions
- [ ] **No new compiler warnings** introduced (check CI build log)
- [ ] **All unit tests pass** (CI "Run unit tests" step)
  - tst_StringUtils
  - tst_FileUtils
  - tst_QueryParser
  - tst_PriorityScheduler
  - tst_FileRepository
  - tst_FtsTokenizer
  - tst_OcrHelper
  - tst_ExtractorFuzz
- [ ] **Smoke test passes** (CI "Verify build output" step)
  - DocuSearch.exe exists and is the right size
  - docusearch_ocr_helper.exe exists and exits non-zero with no args

## Artifacts Produced

- [ ] **DocuSearch-portable.zip** artifact uploaded to CI run
- [ ] **DocuSearch-Setup-msi** artifact uploaded to CI run
- [ ] **DocuSearch-bare-exe** artifact uploaded to CI run (for debugging)

## Portable ZIP Contents

Extract the portable ZIP on a clean Windows 10/11 machine and verify:

- [ ] `DocuSearch.exe` runs without errors
- [ ] `docusearch_ocr_helper.exe` exists
- [ ] `Qt6Core.dll`, `Qt6Gui.dll`, `Qt6Widgets.dll`, `Qt6Sql.dll`, `Qt6Concurrent.dll` present
- [ ] `poppler*.dll` present (PDF text extraction)
- [ ] `zlib*.dll` present (DOCX/XLSX/PPTX extraction)
- [ ] `sqldrivers\qsqlite.dll` present (SQLite driver plugin)
- [ ] `scripts\get_oneocr.ps1` present (OCR installer)
- [ ] `scripts\verify_setup.ps1` present (setup verifier)
- [ ] `ONEOCR_SETUP.md` present
- [ ] `HELP.md` present
- [ ] `FAQ.md` present
- [ ] `docs\OCR_LICENSING.md` present

## MSI Installer Verification

- [ ] **MSI installs without errors** on clean Windows 10
- [ ] **MSI installs without errors** on clean Windows 11
- [ ] **Start Menu shortcut** created
- [ ] **Add/Remove Programs** entry exists with correct version
- [ ] **Uninstall** removes all installed files
- [ ] **Per-machine install** (Program Files, not user-local)

## Functional Testing (Manual)

On a clean Windows 11 machine with Snipping Tool installed:

### Setup
- [ ] Run `scripts\verify_setup.ps1` — should report all checks pass
  except oneocr files (warn)
- [ ] Run `scripts\get_oneocr.ps1` — should install oneocr files
- [ ] Run `scripts\verify_setup.ps1` again — should report all checks pass

### First Run
- [ ] DocuSearch.exe launches without errors
- [ ] Status bar shows "OCR: Setup Required" before oneocr install
- [ ] Status bar shows "OCR: Ready" after oneocr install + restart
- [ ] Clicking OCR status indicator shows helpful message

### Add Folder
- [ ] Click "Add Folder" → browse → select → files appear in results
- [ ] Status bar shows indexed file count
- [ ] Auto-scan runs every hour (verify in logs)

### Extract Text
- [ ] Click "Extract" — extraction runs without crashing
- [ ] Progress bar updates
- [ ] Status bar shows "Extracted N of M files"
- [ ] Extracted text is searchable
- [ ] Large files (>100MB) are skipped gracefully
- [ ] Malformed PDFs are marked failed (not crashed)

### Search
- [ ] Simple search: "railway" returns matching files
- [ ] Phrase search: `"railway station"` returns only exact matches
- [ ] Field filter: `type:pdf` returns only PDFs
- [ ] Field filter: `date:2024` returns only 2024 files
- [ ] Boolean: `railway NOT draft` excludes drafts
- [ ] Empty search returns all files (or shows hint)
- [ ] Click result → preview pane populates
- [ ] Click result does NOT crash

### OCR
- [ ] Select a scanned PDF → click green OCR button → text appears
- [ ] Select an image (PNG/JPG) → click OCR button → text appears
- [ ] Multi-page PDF: OCR processes each page (max 10)
- [ ] OCR on corrupt image → shows error message, no crash
- [ ] OCR on missing file → shows error message, no crash

### Tags / Notes / Favorites
- [ ] Add a tag to a file → tag appears in list
- [ ] Search by `tag:mytag` → returns the tagged file
- [ ] Add a note → note persists after restart
- [ ] Click star → file is favorited → `is:favorite` finds it

### Saved Searches
- [ ] Save a search with a name
- [ ] Saved search appears in dropdown
- [ ] Click saved search → query runs
- [ ] Delete saved search → removed from dropdown

### Backup / Restore
- [ ] Backup → creates .dbbackup file
- [ ] Restore from .dbbackup → database replaced successfully
- [ ] Tags/notes/saved searches preserved after restore

### Settings
- [ ] Indexing tab — adjust worker threads, applies on next indexing
- [ ] OCR tab — shows oneocr info + language list
- [ ] Limits tab — shows all extraction/OCR/DB limits (read-only)
- [ ] Appearance tab — dark mode toggle (currently cosmetic only)
- [ ] Saved Searches tab — manage saved searches
- [ ] Backup/Restore tab — backup and restore buttons work

## Crash Testing

- [ ] **Extraction crash test**: Create a 500 MB dummy PDF → run
      Extract → file is skipped, no crash
- [ ] **OCR crash test**: Create a corrupt PNG → click OCR →
      error message, no crash
- [ ] **Result click crash test**: Click rapidly on different
      results → no crash
- [ ] **Memory pressure test**: Index 10,000 files → no crash,
      memory stays under 500 MB

## Performance Testing

- [ ] **Cold start**: < 3 seconds to main window on a 4 GB RAM machine
- [ ] **Index 1000 files**: < 5 minutes (metadata only)
- [ ] **Extract 30 files**: < 2 minutes (mixed PDFs/DOCX)
- [ ] **Search response**: < 100 ms for any query
- [ ] **Memory usage**: < 200 MB idle, < 500 MB during extraction

## Documentation Verification

- [ ] **README.md** — accurate, no broken links
- [ ] **HELP.md** — covers all features, no broken links
- [ ] **FAQ.md** — answers common questions accurately
- [ ] **ONEOCR_SETUP.md** — install instructions work
- [ ] **docs/OCR_LICENSING.md** — accurate legal info
- [ ] **docs/RELEASE_CHECKLIST.md** — this file is up to date
- [ ] **BUILD.md** — build instructions work on clean Windows

## License / Legal

- [ ] **BSD 3-Clause** license file present (`installer/LICENSE.rtf`)
- [ ] **Third-party notices** included (Qt, Poppler, zlib, SQLite, oneocr)
- [ ] **No proprietary binaries** bundled (oneocr NOT included —
      users install via get_oneocr.ps1)
- [ ] **Version number** bumped in `src/core/Constants.h`

## Release Process

- [ ] **Tag the commit** as `v1.0.0` (or appropriate version)
- [ ] **CI builds the tag** automatically
- [ ] **Download artifacts** from the tag build
- [ ] **Create GitHub Release** with release notes
- [ ] **Attach MSI and portable ZIP** to the release
- [ ] **Update README** with download link to the new release
- [ ] **Close milestone** in GitHub issues (if using milestones)

## Post-Release

- [ ] **Smoke test** the release on a clean machine
- [ ] **Monitor GitHub Issues** for problems
- [ ] **Update FAQ.md** with any new common questions
- [ ] **Plan next release** (milestones / issues)

---

## Quick Verification Commands

From the DocuSearch folder on a Windows machine:

```powershell
# 1. Verify setup
.\scripts\verify_setup.ps1

# 2. Install OCR support
.\scripts\get_oneocr.ps1

# 3. Verify setup again
.\scripts\verify_setup.ps1

# 4. Launch DocuSearch
.\DocuSearch.exe
```

From a development checkout:

```powershell
# Build
cmake -B build -S . -DDOCUSEARCH_BUILD_TESTS=ON `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.8.1\msvc2022_64" `
  -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Release --parallel

# Run tests
ctest --test-dir build --output-on-failure -C Release --timeout 120

# Run app
.\build\bin\Release\DocuSearch.exe
```

---

This checklist should be completed before each public release.
