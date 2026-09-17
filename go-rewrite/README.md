# DocuSearch — Go + Wails Rewrite (PoC)

This is the **Proof-of-Concept** of a fresh implementation of DocuSearch in
**Go + Wails v2 + React + TypeScript + Tailwind CSS**, replacing the original
C++20 / Qt 6 desktop application.

> Branch: `go-rewrite/poc` on the same repo. The original C++ app remains
> untouched on `main`.

## Why a rewrite?

The original DocuSearch is a sophisticated C++20 / Qt 6 app, but iterating on
the UI is slow (Qt Widgets + custom delegates) and the vcpkg / CMake / Qt
toolchain is heavy. This rewrite targets the same product goals with a modern
**web-based UI** stack while keeping the same Windows-only, fully-offline,
no-cloud philosophy.

| Original (C++/Qt)            | Rewrite (Go/Wails/React)             |
| ---------------------------- | ------------------------------------ |
| C++20                        | Go 1.22                             |
| Qt 6.7 Widgets               | React 18 + Vite + Tailwind          |
| CMake + vcpkg                | `wails build` (single static binary)|
| SQLite + FTS5                | SQLite + FTS5 (same schema)         |
| PDFium (CGO-ready in Go)     | `ledongthuc/pdf` (PoC, swap later)  |
| Windows.Media.Ocr (WinRT)    | Stub interface; WinRT bridge later  |
| BGE-small ONNX embeddings    | Not in PoC; pipeline reserved       |
| ~150 source files            | ~30 source files                    |

## PoC scope (what's in this branch)

### Implemented
- **Folder indexing** — add / list / remove folders, recursive walk, skip
  `$RECYCLE.BIN` / `System Volume Information` / dotfiles.
- **Text extraction** for PDF, DOCX, XLSX, PPTX, TXT, MD, CSV, LOG, RTF.
- **SQLite + FTS5** schema with triggers keeping the FTS index in sync.
- **Full-text search** with BM25 ranking, snippet highlighting, suggestions.
- **Modern 3-pane UI**: sidebar (folders + stats) · results list · preview pane.
- **Show in Explorer**, copy extracted text, file metadata grid.
- **Dark / light theme** via CSS variables (`html.dark` toggle).
- **Stats dashboard**: total / pending / errors / index size.

### Not yet implemented (roadmap to parity)
- **OCR** — `internal/ocr` is a stub. Will use WinRT via go-ole or a small CGO
  bridge.
- **Semantic search** — BGE-small ONNX embeddings. The DB schema doesn't yet
  have the `embeddings` table; will add when this lands.
- **Duplicate detection** — `sha256` column exists, scanner not yet wired.
- **Tags, notes, favorites, saved searches** — DB tables exist, UI not built.
- **Backup / restore** — not yet implemented.
- **Auto-scan every hour** — scheduler not yet implemented.
- **Jump List, theme picker, SettingsDialog** — UI not yet built.
- **MSI installer** — use `wails build` for now; WiX can wrap it later.
- **Adaptive performance tiers** — not yet implemented.

## Architecture

```
go-rewrite/
├── wails.json                     # Wails config
├── go.mod                         # Go module
├── main.go                        # Wails entrypoint
├── app.go                         # Wails-bound methods (the API surface)
├── explorer.go                    # Cross-platform "show in explorer"
├── explorer_windows.go            # Windows impl (explorer.exe /select,)
├── explorer_other.go              # Non-Windows stub (dev builds)
├── internal/
│   ├── db/
│   │   ├── db.go                  # SQLite connection + migration
│   │   ├── schema.sql             # Full schema (embedded via //go:embed)
│   │   └── repository.go          # FileRecord / FolderRecord / Stats
│   ├── extract/
│   │   ├── extractor.go           # Extractor interface + Registry
│   │   ├── pdf.go                 # PDF (ledongthuc/pdf — swap to PDFium later)
│   │   ├── docx.go                # DOCX (zip + xml)
│   │   ├── xlsx.go                # XLSX (zip + sharedStrings + sheets)
│   │   ├── pptx.go                # PPTX (zip + slides)
│   │   └── text.go                # TXT/MD/CSV/LOG/RTF (RTF tag stripping)
│   ├── indexer/
│   │   └── pipeline.go            # Folder walker + extraction pipeline
│   ├── search/
│   │   └── engine.go              # FTS5 + BM25 + suggestions
│   ├── config/
│   │   └── config.go              # APPDATA-based config + slog setup
│   └── ocr/
│       └── ocr.go                 # OCR interface + NoopEngine (WinRT later)
└── frontend/
    ├── package.json
    ├── vite.config.ts
    ├── tsconfig.json
    ├── tailwind.config.js
    ├── postcss.config.js
    ├── index.html
    └── src/
        ├── main.tsx
        ├── App.tsx                # 3-pane layout
        ├── styles/globals.css     # Design tokens (dark/light)
        ├── lib/
        │   ├── api.ts             # Typed wrappers around Wails bindings
        │   └── utils.ts           # cn(), formatBytes(), timeAgo()
        ├── stores/
        │   └── app-store.ts       # Zustand store (search/select/folders/stats)
        └── components/
            ├── Sidebar.tsx        # Folders + stats
            ├── SearchBar.tsx      # Debounced search + suggestions
            ├── ResultsList.tsx    # Virtualized later; plain list for PoC
            ├── PreviewPane.tsx    # File info + extracted text
            └── ui/button.tsx      # shadcn-style button
```

## Build

See [BUILD.md](BUILD.md) for prerequisites, dev mode, and release builds.

## Query syntax

The search bar accepts FTS5 queries:

| Query | Meaning |
|---|---|
| `report` | Files containing "report" |
| `"annual report"` | Files containing the phrase "annual report" |
| `report AND 2024` | Files containing both terms |
| `report OR memo` | Files containing either term |
| `report NOT draft` | "report" but not "draft" |
| `repo*` | Prefix match — "report", "repository", etc. |
| `file_name:report` | Match only in the file name column |

Invalid queries are automatically retried as a phrase.

## License

Same as the parent project — see `LICENSE` at the repo root.
