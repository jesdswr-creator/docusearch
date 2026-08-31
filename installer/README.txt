DocuSearch — Offline Intelligent Document Search & OCR
=======================================================

QUICK START
-----------
1. Launch DocuSearch from the Start Menu or the desktop shortcut.
2. Click "Add Folder" and pick the folder (or drive) that holds your
   documents. The folder is scanned and its files are indexed.
3. Extraction starts automatically about one minute after the app opens
   and continues in batches; the "Extract" button reads "Stop Extracting"
   while extraction is running. Click it to stop, click again to resume.
4. Type in the search box and press Enter.

WHAT GETS INDEXED
-----------------
Documents and images: .pdf .doc .docx .xls .xlsx .xlsm .ppt .pptx
.jpg .jpeg .png — plus .txt .csv .md .rtf filenames.

FOLDERS & THE INDEX
-------------------
* Settings > Indexed Drives / Folders lists exactly what is indexed.
* Removing a folder there ALSO removes its files from the index
  immediately — they stop appearing in search and duplicates.
* Files you delete or move outside the app are removed from the index
  by the automatic hourly scan (and at every app start). If you still
  see one, clicking it clears it away automatically.
* An unplugged or offline drive is never wiped from the index; it is
  refreshed when the drive returns.

SEARCH SYNTAX
-------------
  gold bin            Files containing BOTH 'gold' AND 'bin'
  "gold bin"          The exact phrase 'gold bin'
  gold -draft         Files with 'gold' but NOT 'draft'
  rail*               Prefix wildcard: railway, railroad, rails
  type:pdf            Only PDF files
  folder:Railway      Files under folders containing 'Railway'
  date:2026           Files modified in 2026
  tag:Urgent          Files tagged 'Urgent'

Tip: matching a FILE NAME ranks that file at the top of the results.

AI (SEMANTIC) SEARCH
--------------------
Toggle "Semantic" in the search bar to let AI refine ranking and find
documents by meaning. AI needs the local BGE model in:
    <install folder>\bin\models\bge-small-en-v1.5\
Keyword search always works, with or without AI.

OCR
---
OCR runs on the Windows OCR engine (Settings > Region: make sure a
language pack for your language is installed in Windows). Scanned PDFs
whose text layer is garbled are detected and routed through OCR
automatically during extraction (first 20 pages per PDF).

UNINSTALL
---------
Use Windows Settings > Apps > Installed apps > DocuSearch > Uninstall.
Your index database is offered for cleanup during uninstall.
