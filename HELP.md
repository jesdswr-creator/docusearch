# DocuSearch Help

Welcome to DocuSearch — an offline desktop application for indexing,
extracting, and searching text in your documents.

This guide covers everything you need to use DocuSearch effectively.

---

## Table of Contents

1. [Getting Started](#getting-started)
2. [Adding Folders](#adding-folders)
3. [Searching](#searching)
4. [Extracting Text](#extracting-text)
5. [OCR (Scanned PDFs & Images)](#ocr-scanned-pdfs--images)
6. [Tags, Notes & Favorites](#tags-notes--favorites)
7. [Saved Searches](#saved-searches)
8. [Backup & Restore](#backup--restore)
9. [Settings](#settings)
10. [Keyboard Shortcuts](#keyboard-shortcuts)
11. [Troubleshooting](#troubleshooting)

---

## Getting Started

1. **Download** the latest portable ZIP or MSI installer from
   [GitHub Actions](https://github.com/jesdswr-creator/docusearch/actions).
2. **Install/extract** to any folder.
3. **Run** `DocuSearch.exe`.
4. **(Optional) Install OCR language pack** — Settings → Time &
   Language → Language → Add → Optical character recognition.
   See [docs/OCR_LICENSING.md](docs/OCR_LICENSING.md) for details.

The first window you see has:
- A sidebar on the left with navigation items
- A search bar at the top
- A 3-pane main area (results | preview | metadata+tags)
- A status bar at the bottom showing indexed count + OCR status

---

## Adding Folders

1. Click **Add Folder** in the toolbar (top-right area).
2. Browse to the folder you want to index.
3. Click **Select Folder**.

DocuSearch will:
- Walk the folder recursively
- Add every supported file to its database
- Extract file metadata (size, dates, hash)
- Mark each file as `metadata_only`

Once a folder is added, DocuSearch automatically rescans it every hour
for new and modified files. You can also click **Refresh** (or press
F5) to rescan immediately.

### Supported file types

| Category | Extensions |
|----------|-----------|
| Documents | PDF, DOC/DOCX, XLS/XLSX/XLSM, PPT/PPTX |
| Images (OCR) | JPG, PNG, TIFF, BMP, GIF, WebP |

v1.7.5: notes and text formats (.md, .txt, .csv, .rtf, .log) and all
other file types (installers, archives, media) are intentionally NOT
indexed — search results contain only real documents and images.
The duplicate finder covers every type in the table above: identical
scanned images (two copies of the same JPG/TIFF page) count as
duplicates just like identical documents do.

**Detect Duplicates** compares files by content, not by name, size or
date. It first groups candidates by exact byte size (files of
different sizes can never be identical), then fingerprints only those
groups with SHA-256 — computing any fingerprint that is missing or
out of date on the spot. That means a duplicate is found whether or
not the file has been hashed by a previous scan, and "No duplicate
files found" means the bytes really do differ.

---

## Searching

Type in the search bar at the top and press **Enter** or click
**Search**.

### Simple search

```
railway station
```
Finds files containing both "railway" and "station" (anywhere).

### Phrase search

```
"railway station"
```
Finds files containing the exact phrase "railway station".

### Boolean operators

```
railway AND station
railway OR train
railway NOT draft
```

### Field filters

```
type:pdf                  → only PDF files
folder:Railway            → files in any folder containing "Railway"
date:2024                 → files modified in 2024
date:>2024-01-01          → files modified after Jan 1, 2024
size:>10MB                → files larger than 10 MB
tag:important             → files tagged "important"
is:favorite               → favorited files
is:ocr                    → files that have been OCR'd
```

### Combining

```
"executive lounge" type:pdf date:>2024-01-01 -draft
```
Finds PDFs from 2024 onward containing the phrase "executive lounge"
but not the word "draft".

---

## Extracting Text

After adding a folder, files are listed but their text content is
not yet searchable. To extract text:

1. Click the **Extract** button in the toolbar.
2. DocuSearch processes up to 30 files per session, with a 200 ms gap
   between files to keep your system responsive.
3. Progress is shown in the status bar.
4. Once extraction completes, those files become searchable.

If a file is too large (>100 MB) or too long (>500 KB of text), it
will be partially indexed — the first portion is searchable, the
remainder is skipped. See Settings → Limits for the exact values.

Scanned PDFs (where Poppler returns no text) are flagged as
`needs_ocr` instead of `failed`. You can OCR them later by clicking
the green OCR button.

---

## OCR (Scanned PDFs & Images)

OCR extracts text from images and scanned PDFs using
**Windows.Media.Ocr** — the officially-supported WinRT OCR API
built into Windows 10 1809+ and Windows 11.

- **No DLLs to install** — Windows itself provides the OCR engine.
- **No scripts to run** — no `get_oneocr.ps1`, no DLL extraction.
- **No licensing risk** — Windows.Media.Ocr is the public, royalty-free
  OCR API that any Windows app (including commercial software) can use.
- **25+ languages** — auto-detected from your Windows language profile.

The only prerequisite is at least one OCR language pack installed.
If the status bar shows "OCR: Click to setup":

1. Open Settings → Time & Language → Language & Region
2. Click Add a language
3. Pick a language (e.g., English (United States))
4. In the language options, check Optical character recognition
5. Click Install, then restart DocuSearch

See [docs/OCR_LICENSING.md](docs/OCR_LICENSING.md) for full licensing
and technical details.

### Running OCR on a file

1. Click a search result (or any file in the results list).
2. Click the green **OCR** button in the preview pane.
3. Wait — OCR takes 1-5 seconds per image, longer for multi-page PDFs.
4. Recognized text appears in the preview pane.

### OCR status indicator

The status bar at the bottom shows the current OCR status:
- 🟢 **OCR: Ready** — Windows.Media.Ocr language packs installed.
- 🟡 **OCR: Click to setup** — no OCR language packs installed.
  Click the indicator for install instructions.

### Supported languages

Windows.Media.Ocr supports 25+ languages including:
- English (US, UK, AU, CA, IN)
- Chinese (Simplified, Traditional — HK, TW)
- Japanese, Korean
- German, French, Spanish, Italian, Portuguese
- Russian, Polish, Czech, Hungarian
- Arabic, Hebrew, Hindi, Thai, Vietnamese

The helper auto-detects the document language from your Windows
profile languages — no manual language selection needed.

---

## Tags, Notes & Favorites

### Tags

Tags let you organize files by topic, project, or any category.

1. Select a file in the results list.
2. In the right panel, find the **Tags** section.
3. Type a tag name and press Enter (or click +).
4. To remove a tag, click the X next to it.

You can search by tag using `tag:important`.

### Notes

Each file can have one note (free-form text).

1. Select a file.
2. Find the **Notes** section in the right panel.
3. Type your note. It saves automatically.

### Favorites

Click the star icon next to a file to mark it as a favorite.
Search with `is:favorite` to find all favorited files.

---

## Saved Searches

If you frequently run the same search, save it:

1. Type your query in the search bar.
2. Click the **Saved** dropdown (next to Search button).
3. Click **Save Current Search**.
4. Give it a name (e.g., "Invoices 2024").
5. Click OK.

To run a saved search, open the **Saved** dropdown and click the
search name. To delete a saved search, hover over it and click X.

---

## Backup & Restore

### Backup

1. Open **Settings** → **Backup / Restore** tab.
2. Click **Backup**.
3. Choose where to save the `.dbbackup` file.

The backup includes:
- All indexed file metadata
- All extracted text
- All tags
- All notes
- All saved searches
- All settings

### Restore

1. Open **Settings** → **Backup / Restore** tab.
2. Click **Restore**.
3. Choose a `.dbbackup` file.
4. Confirm — your current database will be replaced.

---

## Settings

Open **Settings** from the sidebar. Tabs:

- **Indexing** — Worker threads, CPU usage limits, monitor options.
- **Performance** — Information about extraction/OCR/DB limits
  (read-only).
- **Limits** — Read-only display of all extraction/OCR/database limits.
- **Appearance** — Dark mode toggle (currently light-only).
- **Saved Searches** — Manage saved searches.
- **Backup / Restore** — Backup or restore your database.

---

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `Ctrl+K` | Focus the search bar |
| `F5` | Refresh (rescan indexed folders) |
| `Ctrl+O` | Open the selected file |
| `Enter` (in search) | Run search |
| `Esc` | Clear search |

---

## Troubleshooting

### "Extraction failed" or extraction seems stuck

- Check Settings → Limits for the file size/text caps.
- Try extracting again — DocuSearch processes 30 files per session,
  so a large folder may need multiple Extract clicks.
- If a specific file consistently fails, it may be malformed. The
  log will show the error message.

### OCR button doesn't work

- Check the OCR status indicator in the status bar.
  - 🟡 Yellow means no OCR language packs are installed.
- Install via Settings → Time & Language → Language → Add a language
  with the Optical character recognition option checked.
- Restart DocuSearch after install.
- See [docs/OCR_LICENSING.md](docs/OCR_LICENSING.md) for troubleshooting.

### Search returns no results

- Make sure you've clicked **Extract** first — files are not
  searchable until their text has been extracted.
- Try a simpler query (single word, no operators).
- Try `is:favorite` or `type:pdf` to verify search works at all.
- Check Settings → Limits — very large files may have been skipped.

### App is slow on large folders

- Reduce worker threads in Settings → Indexing.
- Increase the pause threshold (e.g. 50% instead of 70%).
- Extract in smaller batches (click Extract, wait, click again).

### Where is my data stored?

- Database: `%APPDATA%\DocuSearch\docusearch.db`
- Logs: `%APPDATA%\DocuSearch\logs\`
- Thumbnails: `%APPDATA%\DocuSearch\thumbnails\`

### How do I completely reset DocuSearch?

1. Close DocuSearch.
2. Delete `%APPDATA%\DocuSearch\` (or rename it for backup).
3. Restart DocuSearch — it will create a fresh database.

---

## Getting Help

- **Bug reports**: [GitHub Issues](https://github.com/jesdswr-creator/docusearch/issues)
- **Latest builds**: [GitHub Actions](https://github.com/jesdswr-creator/docusearch/actions)
- **OCR setup**: [docs/OCR_LICENSING.md](docs/OCR_LICENSING.md)
- **OCR licensing**: [docs/OCR_LICENSING.md](docs/OCR_LICENSING.md)
- **FAQ**: [FAQ.md](FAQ.md)

---

DocuSearch is open source (BSD 3-Clause). See
[installer/LICENSE.rtf](installer/LICENSE.rtf) for the full license.
