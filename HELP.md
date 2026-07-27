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
4. **(Optional) Install OCR support** — run `scripts\get_oneocr.ps1`
   to enable OCR for scanned PDFs and images. See
   [ONEOCR_SETUP.md](ONEOCR_SETUP.md) for details.

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
| Documents | PDF, DOC/DOCX, XLS/XLSX, PPT/PPTX, TXT, RTF, CSV, MD |
| Images (OCR) | JPG, PNG, TIFF, BMP, GIF, WebP |

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

OCR extracts text from images and scanned PDFs using **oneocr.dll**
(the native OCR engine from the Windows 11 Snipping Tool).

### One-time setup

```powershell
.\scripts\get_oneocr.ps1
```

This script copies the oneocr files from your locally-installed
Snipping Tool into the DocuSearch folder. See
[ONEOCR_SETUP.md](ONEOCR_SETUP.md) for full details.

### Running OCR on a file

1. Click a search result (or any file in the results list).
2. Click the green **OCR** button in the preview pane.
3. Wait — OCR takes 1-5 seconds per image, longer for multi-page PDFs.
4. Recognized text appears in the preview pane.

### OCR status indicator

The status bar at the bottom shows the current OCR status:
- 🟢 **OCR: Ready** — oneocr is installed and ready.
- 🟡 **OCR: Setup Required** — oneocr is not installed. Click the
  indicator for install instructions.

### Supported languages

The oneocr model auto-detects:
- English
- Chinese (Simplified & Traditional)
- Korean
- Japanese

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
- **OCR** — Information about oneocr + list of supported languages.
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
  - 🟡 Yellow means oneocr isn't installed.
- Run `scripts\get_oneocr.ps1` to install.
- Restart DocuSearch.
- See [ONEOCR_SETUP.md](ONEOCR_SETUP.md) for troubleshooting.

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
- **OCR setup**: [ONEOCR_SETUP.md](ONEOCR_SETUP.md)
- **OCR licensing**: [docs/OCR_LICENSING.md](docs/OCR_LICENSING.md)
- **FAQ**: [FAQ.md](FAQ.md)

---

DocuSearch is open source (BSD 3-Clause). See
[installer/LICENSE.rtf](installer/LICENSE.rtf) for the full license.
