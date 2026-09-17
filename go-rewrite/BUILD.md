# Build Guide — DocuSearch Go + Wails Rewrite

## Prerequisites

### Required (Windows)

1. **Go 1.22+** — <https://go.dev/dl/>
2. **Node.js 20+** (with npm) — <https://nodejs.org/>
3. **Wails CLI v2**:
   ```powershell
   go install github.com/wailsapp/wails/v2/cmd/wails@latest
   ```
   Verify: `wails version`

4. **WebView2 Runtime** — preinstalled on Windows 11. On Windows 10, install
   from <https://developer.microsoft.com/microsoft-edge/webview2/>.

5. **C compiler** (for CGO — `mattn/go-sqlite3` requires it):
   - Install **Build Tools for Visual Studio 2022** with the
     "Desktop development with C++" workload.
   - Or install **MSYS2** and ensure `gcc` is on PATH.

### Verify the environment

```powershell
go version
node --version
wails doctor
```

`wails doctor` will flag any missing dependencies.

---

## Development mode (hot reload)

From the `go-rewrite/` directory:

```powershell
cd go-rewrite
wails dev
```

This launches:
- Vite dev server on <http://127.0.0.1:5173> (with HMR)
- The Wails desktop window, pointing at the Vite dev server
- Hot reload on both Go and frontend changes

The app stores its database at `%APPDATA%\DocuSearch\docusearch.db`.
Logs at `%APPDATA%\DocuSearch\logs\docusearch.log`.

---

## Release build

```powershell
cd go-rewrite
wails build -clean -platform windows/amd64
```

Output: `go-rewrite/build/bin/DocuSearch.exe` — a single static binary
including the React frontend, all Go dependencies, and (when added) the
PDFium / ONNX / WinRT bridges.

### Build with optimizations

```powershell
wails build -clean -ldflags "-s -w" -trimpath
```

- `-s -w` strips debug symbols (smaller binary)
- `-trimpath` removes local file paths from the binary

---

## Frontend-only dev (no Go backend)

For pure UI iteration you can run Vite alone:

```powershell
cd go-rewrite/frontend
npm install
npm run dev
```

Open <http://127.0.0.1:5173>. The app will render but every API call will
throw "DocuSearch backend not available" — useful for CSS / layout work
but you can't actually search.

---

## Project structure

See [README.md](README.md) for the full directory layout.

## Troubleshooting

### `wails: command not found` after `go install`

Ensure `$GOPATH/bin` is on your PATH:

```powershell
$env:Path += ";$env:USERPROFILE\go\bin"
```

(Or add it permanently via System Properties → Environment Variables.)

### `gcc: command not found` during `wails build`

`mattn/go-sqlite3` requires CGO. Install a C compiler:

- **MSYS2**: <https://www.msys2.org/> then `pacman -S mingw-w64-x86_64-gcc`
  and add `C:\msys64\mingw64\bin` to PATH.
- **Or** Visual Studio Build Tools with C++ workload.

### `failed to bind methods: cannot find package` after adding new files

Run `go mod tidy` to refresh dependencies, then `wails dev` again.

### Frontend changes not showing up

- Confirm `wails dev` is running (not just `vite dev`)
- Hard-reload the webview: Ctrl+Shift+R
- Check the Wails console output for compile errors

### Database locked / SQLITE_BUSY

The Go backend uses a single-writer-connection pool (see `db.Open`), so this
shouldn't happen under normal use. If you see it during dev, kill any
orphan `DocuSearch.exe` processes via Task Manager.

---

## CI / GitHub Actions

A dedicated workflow at `.github/workflows/build-go-rewrite.yml` builds the
EXE and MSI on every push that touches `go-rewrite/`. It runs on
`windows-2022`, installs Go + Node + MSVC + Wails CLI + WiX v4, runs
`wails build` and `wix build`, and uploads both artifacts.

**Artifacts** (downloadable from the Actions tab for 30 days):
- `DocuSearch-exe-<version>` — portable `DocuSearch-<version>.exe`
- `DocuSearch-msi-<version>` — `DocuSearch-Setup-<version>.msi`

**Releases**: tag a commit as `go-v1.0.0` (note the `go-` prefix to avoid
colliding with the C++ app's `v*` tags) and the workflow will:
1. Build both artifacts as usual.
2. Create a GitHub Release named "DocuSearch (Go) go-v1.0.0".
3. Attach the EXE and MSI to the release.
4. Auto-generate release notes from commits since the last tag.

The workflow is separate from the C++ app's `build.yml` — they coexist and
don't interfere. The `paths:` filter ensures the Go workflow only runs when
files under `go-rewrite/` change.

### Trigger a manual build

GitHub → Actions → "Build DocuSearch (Go + Wails)" → Run workflow.

---

## Replacing the PoC PDF library with PDFium (production)

The PoC uses `github.com/ledongthuc/pdf` (pure Go, simpler, less accurate).
To swap to PDFium (matches the C++ app):

1. Add a CGO wrapper using `github.com/klippa-app/go-pdfium` or directly
   via `pdfium.go`.
2. Update `internal/extract/pdf.go` to call the new binding. The
   `Extractor` interface stays the same — only the implementation changes.
3. Update `go.mod` and the build instructions to include the PDFium
   dynamic library.

## Adding OCR (WinRT bridge)

The `internal/ocr` package exposes an `Engine` interface. To add WinRT:

1. Implement `WinRTEngine` in `internal/ocr/winrt.go` (build tag: `windows`).
2. Use `github.com/go-ole/go-ole` for COM activation, or write a small CGO
   bridge in C that calls `Windows.Media.Ocr`.
3. Update `ocr.New()` to return `&WinRTEngine{}` on Windows.
4. Wire OCR into the indexer pipeline: after `SetFileExtracted`, if the
   file is an image or a scanned PDF, call OCR and `SetFileOCRText`.
