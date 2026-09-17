# DocuSearch Installer (Go + Wails build)

This directory holds the **WiX v4** source for the DocuSearch MSI installer.

## Files

- `DocuSearch.wxs` — WiX v4 source. Builds a 64-bit MSI that installs the
  Wails-built `DocuSearch.exe` into `C:\Program Files\DocuSearch\` and adds a
  Start Menu shortcut + optional Desktop shortcut.

## Why so simple?

The C++ app's `installer/DocuSearch.wxs` is huge because it has to harvest
hundreds of files (Qt DLLs, plugins, themes, the BGE ONNX model, PDFium
binaries, etc.). The Wails build produces a **single self-contained .exe**,
so this MSI source only needs to install one file. No `heat` harvesting, no
harvest filters, no per-file Component entries.

## Local build

Prerequisites:
- WiX v4 (`dotnet tool install -g wix --version 4.0.5`)
- WixToolset.UI.wixext (`wix extension add WixToolset.UI.wixext/4.0.5`)
- The EXE produced by `wails build` at `../build/bin/DocuSearch.exe`

```powershell
cd go-rewrite
wails build -clean -platform windows/amd64 -webview2 embed
wix build installer\DocuSearch.wxs `
    -o build\bin\DocuSearch-Setup.msi `
    -d ProductVersion=2.0.0.0 `
    -d BuildOutputDir=build\bin `
    -ext WixToolset.UI.wixext
```

## CI build

The GitHub Actions workflow at `.github/workflows/build-go-rewrite.yml`
runs `wails build` then `wix build` automatically on every push that
touches `go-rewrite/`. Both the EXE and MSI are uploaded as workflow
artifacts; tagged releases (`go-v*`) also create a GitHub Release with
both artifacts attached.

## What's not in the PoC installer

- **Bitmaps / banner / dialog**: WixUI uses its default bitmaps. To brand
  the installer, drop `bitmaps/banner.bmp` (493×63) and `bitmaps/dialog.bmp`
  (493×312) here and uncomment the `WixVariable` lines in `DocuSearch.wxs`.
- **License RTF**: same deal — drop `installer/LICENSE.rtf` and uncomment
  the `WixUILicenseRtf` line.
- **File associations** (`.pdf`, `.docx` etc.): the C++ installer registers
  these. Parity will add them in a later iteration.
- **Custom action for OCR language pack prompt**: deferred until OCR lands.
