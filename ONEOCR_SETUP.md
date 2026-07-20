# OneOCR Setup

DocuSearch uses **oneocr.dll** — the native OCR engine shipped with the Windows 11 Snipping Tool — for text recognition. This replaced the previous Windows.Media.Ocr (WinRT) implementation that was causing crashes on low-RAM Windows systems.

The oneocr files are **not redistributed** with DocuSearch. They are Microsoft-owned binaries that ship with the Windows Snipping Tool. You must install them yourself by copying them from your local Snipping Tool installation.

## Quick install (Windows 10/11)

```powershell
powershell -ExecutionPolicy Bypass -File scripts\get_oneocr.ps1
```

This script will:

1. Look up your locally-installed Snipping Tool (`Microsoft.ScreenSketch`) via `Get-AppxPackage`.
2. Locate `oneocr.dll`, `oneocr.onemodel`, and `onnxruntime.dll` inside the Snipping Tool install folder.
3. Copy all three files into the DocuSearch build output directory (default: `build\bin\Release\`).

If you want to install into a different folder:

```powershell
.\scripts\get_oneocr.ps1 -TargetDir "C:\DocuSearch\bin"
```

## Requirements

- **Windows 10 1809+ or Windows 11**.
- **Snipping Tool installed from the Microsoft Store**: <https://apps.microsoft.com/detail/9mz95kl8mr0l>
  - On Windows 11, this is pre-installed.
  - On Windows 10, you must install it manually.
- PowerShell 5.1+ (built into Windows 10/11).

## What gets installed

| File | Size | Purpose |
|------|------|---------|
| `oneocr.dll` | ~1 MB | Native OCR pipeline engine (C ABI). |
| `oneocr.onemodel` | ~17 MB | ONNX neural model for text recognition. |
| `onnxruntime.dll` | ~7 MB | Microsoft ONNX Runtime (executes the model). |

All three must be in the **same directory** as `docusearch.exe` (or in one of the fallback search paths: `<appDir>/oneocr/`, `<appDir>/models/oneocr/`, or `%USERPROFILE%/.config/oneocr/`).

## Verifying the install

After running the script, restart DocuSearch and check the log (or status bar) for:

```
[OCR] oneocr.dll found at: C:\DocuSearch\build\bin\Release
```

If you see:

```
[OCR] oneocr.dll not found. OCR will be disabled.
[OCR] Run scripts/get_oneocr.ps1 to install oneocr files.
```

…then the install failed or the files are in the wrong location.

## Troubleshooting

### "Snipping Tool is not installed"

Install it from the Microsoft Store: <https://apps.microsoft.com/detail/9mz95kl8mr0l>

After installation completes, re-run `scripts\get_oneocr.ps1`.

### "Failed to copy ... Access to the path is denied"

WindowsApps has restrictive ACLs. Workaround:

1. Open PowerShell as Administrator.
2. Take ownership of the Snipping Tool folder:
   ```powershell
   takeown /f "C:\Program Files\WindowsApps\Microsoft.ScreenSketch_*" /r /d Y
   icacls "C:\Program Files\WindowsApps\Microsoft.ScreenSketch_*" /grant "$env:USERNAME:(OI)(CI)F" /T
   ```
3. Re-run `scripts\get_oneocr.ps1`.

Or manually copy the 3 files from:
```
C:\Program Files\WindowsApps\Microsoft.ScreenSketch_<version>_x64__8wekyb3d8bbwe\SnippingTool\
```
to your DocuSearch install folder.

### "Missing required files in source directory"

Your Snipping Tool version is too old and doesn't ship oneocr. Update Snipping Tool from Microsoft Store and try again.

### OCR is installed but recognition still returns empty text

- Verify the image is between 50×50 and 10000×10000 pixels (oneocr's limits).
- Verify the file is under 20 MB (DocuSearch limit for low-RAM safety).
- Try a clear, high-DPI scan — blurry images produce empty results.
- Check the log for `[oneocr]` lines for more specific errors.

## Why oneocr instead of Windows.Media.Ocr?

| Aspect | Windows.Media.Ocr (old) | oneocr.dll (new) |
|--------|-------------------------|------------------|
| API | WinRT async (IAsyncOperation) | Plain C ABI |
| Threading | Apartment-threaded (crash-prone) | None (stateless calls) |
| Quality | Mediocre (legacy model) | Excellent (Snipping Tool's model) |
| Languages | All installed WinRT OCR packs | en, zh-sim, zh-tra, ko, ja |
| Crashes | Frequent on 4 GB RAM systems | None observed |

## Manual install (alternative)

If you prefer not to run PowerShell scripts:

1. Find your Snipping Tool install path:
   ```powershell
   (Get-AppxPackage Microsoft.ScreenSketch).InstallLocation
   ```
2. Open that folder in Explorer (you may need to take ownership first — see above).
3. Navigate to the `SnippingTool\` subfolder.
4. Copy `oneocr.dll`, `oneocr.onemodel`, and `onnxruntime.dll` to your DocuSearch install folder (next to `docusearch.exe`).

## Licensing note

The `oneocr.dll`, `oneocr.onemodel`, and `onnxruntime.dll` files are Microsoft proprietary binaries extracted from the Windows 11 Snipping Tool. DocuSearch does not redistribute these files — each user obtains them from their own legally-installed Snipping Tool. The `get_oneocr.ps1` script only copies files between folders on your own machine.
