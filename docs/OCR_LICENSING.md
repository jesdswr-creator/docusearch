# OCR Licensing — Windows.Media.Ocr

## Overview

DocuSearch performs optical character recognition (OCR) using the
**Windows.Media.Ocr** WinRT API — the same OCR engine that powers
Windows Search, the Snipping Tool, and the Photos app.

This is the officially-supported, royalty-free OCR API for any
Windows application, **including paid commercial software**.

---

## Licensing Summary

| Use case | Allowed? |
|----------|----------|
| Personal use | ✅ |
| Commercial use | ✅ |
| Bundling with paid software | ✅ |
| Server-side OCR services | ❌ (use Azure Computer Vision instead) |
| Reverse engineering / extracting the model | ❌ (WinRT EULA) |
| Modifying the OCR engine | ❌ (it's an OS component) |

### Why this is different from oneocr.dll

Earlier DocuSearch versions used `oneocr.dll` — the OCR engine extracted
from the Windows 11 Snipping Tool via the `get_oneocr.ps1` script.
That DLL is **Microsoft proprietary** and its license explicitly
prohibits bundling with paid software. Each user had to extract it
from their own Snipping Tool install — a clunky UX that also broke
Store certification.

Windows.Media.Ocr is the **publicly-documented, officially-supported
WinRT API** that exposes the same underlying OCR capability. There
are no DLLs to ship, no scripts to run, no licensing risk. It just
works on any Windows 10 1809+ install that has at least one OCR
language pack.

---

## How it works

```
DocuSearch.exe (Qt 6 app)
      │
      │ spawns as child process via QProcess
      ▼
docusearch_ocr_helper.exe (plain C++ console app)
      │
      │ calls Windows.Media.Ocr.OcrEngine.RecognizeAsync
      │ via C++/WinRT headers (ships with Windows 10 SDK 17763+)
      ▼
Windows OCR engine (OS component, ships with Windows 10/11)
```

### Why a separate helper exe?

`runtimeobject.lib` (the WinRT runtime support library) contains
`/INCLUDE:WINRT_CRT_MAIN`, which conflicts with Qt's WIN32 entry
point. Linking `runtimeobject.lib` directly into the main Qt app
triggers `LNK2019: unresolved external symbol main`.

The fix is to put ALL WinRT calls into a separate plain console
exe (`docusearch_ocr_helper.exe`). Console apps don't have Qt's
`WinMain`, so `WINRT_CRT_MAIN` fits cleanly. The main Qt app spawns
the helper via `QProcess` and reads the OCR output from stdout.

This also gives us free process isolation: even if the WinRT
OCR subsystem faults, the main Qt app is unaffected.

---

## Language packs

Windows.Media.Ocr requires at least one **OCR language pack** to be
installed. On most consumer Windows 10/11 installs, the user's
display language comes with an OCR pack by default. On LTSC / N
editions, the user may need to install one manually.

### How to install an OCR language pack

1. Open **Settings → Time & Language → Language & Region**
2. Click **Add a language**
3. Pick a language (e.g., English (United States))
4. In the language options, check **Optical character recognition**
5. Click Install
6. Restart DocuSearch

After install, the helper will automatically use the user's profile
languages for multi-language auto-detection. No restart of Windows
is required.

### Supported languages

Windows.Media.Ocr supports every language pack that Windows ships
for OCR — typically 25+ languages including:

- English (US, UK, AU, CA, IN)
- Chinese (Simplified, Traditional — HK, TW)
- Japanese, Korean
- German, French, Spanish, Italian, Portuguese (BR + PT)
- Russian, Polish, Czech, Hungarian
- Arabic, Hebrew, Hindi, Thai, Vietnamese
- And many more

The helper picks the user's profile languages automatically via
`OcrEngine::TryCreateFromUserProfileLanguages()`.

---

## Technical details

### Image formats supported

The helper uses `Windows.Graphics.Imaging.BitmapDecoder` for image
decoding. This handles:

- PNG, JPEG, JPEG-XR, HEIF/HEIC
- TIFF (single + multi-page)
- BMP, GIF, WebP

Images are decoded into BGRA8 premultiplied format (the format
expected by the OCR engine).

### Output protocol

The helper writes to stdout, one block per file:

```
===FILE===C:\path\to\image.png
<recognized text — may be empty>
===END===
```

Error lines start with `[`. The main app treats any text starting
with `[` as a failure marker and displays it as an error message.

### Safety features

- **SEH translator:** Structured exceptions (access violations,
  stack overflows) inside the WinRT stack are caught and reported
  as text — they no longer crash the helper.
- **Per-file try/catch:** One bad image can't abort the whole batch.
- **20 MB file size cap:** Low-RAM protection.
- **100 ms gap between files:** Keeps memory pressure low on 4 GB
  systems.
- **Process isolation:** OCR runs in a separate helper process —
  even if everything above fails, the main app survives.

### Security model

- **Offline processing:** No data sent to internet.
- **Process isolation:** OCR runs in separate helper process.
- **Local storage:** Results stored locally only.
- **No telemetry:** No usage tracking or reporting.
- **No account required:** Works without internet connection.

---

## Compliance checklist

- ✅ Uses officially-documented public WinRT API
  (`Windows.Media.Ocr.OcrEngine`)
- ✅ No DLLs redistributed (Windows OS provides the engine)
- ✅ No model files redistributed (Windows OS provides the model)
- ✅ No install scripts required (helper exe ships with the app)
- ✅ Compatible with Microsoft Store commercial policy
- ✅ Works on any Windows 10 1809+ with OCR language packs
- ✅ No reverse engineering of Microsoft binaries
- ✅ No telemetry, no cloud calls, fully offline
- ✅ Source code is open (BSD 3-Clause)

---

## Alternatives

If you prefer not to use Windows.Media.Ocr, the source is open and
the integration point is `src/ocr/WindowsOcrEngine.h`. Replace the
helper exe with a Tesseract or PaddleOCR wrapper and the rest of
the app will continue to work unchanged.

Note: Tesseract is Apache 2.0 licensed and bundles cleanly with
commercial software, but adds ~40 MB to the install size and
requires per-language model files.

---

## Troubleshooting

### "No OCR language packs installed"

1. Open **Settings → Time & Language → Language & Region**
2. Add a language with the "Optical character recognition" option
   checked
3. Restart DocuSearch

### OCR returned empty text for a clearly readable image

Possible causes:
1. The image is below 50×50 pixels (Windows.Media.Ocr's minimum)
2. The image is mostly decorative (gradient, blurry, low contrast)
3. Text is rotated 90°/180° — Windows.Media.Ocr handles small
   rotations but not extreme angles
4. The image is a screenshot of code with very thin fonts — try
   increasing the DPI/resolution

### OCR is slow

OCR is computationally expensive. A single page typically takes
1–3 seconds; a 10-page PDF takes 10–30 seconds. This is normal
for any OCR engine running locally on a CPU.

If OCR is consistently very slow:
- Close other CPU-heavy applications
- Use lower-DPI images (200 DPI is plenty for OCR)
- For very large PDFs, consider splitting them into smaller files

---

## References

- Windows.Media.Ocr API reference:
  https://learn.microsoft.com/uwp/api/windows.media.ocr
- Windows.Media.Ocr language packs:
  https://learn.microsoft.com/windows/powertoys/text-extractor#how-to-ocr-text-in-another-language
- Microsoft Store policy on third-party APIs:
  https://learn.microsoft.com/windows/apps/publish/store-policies
- C++/WinRT language projection:
  https://learn.microsoft.com/windows/uwp/cpp-and-winrt-apis/intro-to-using-cpp-with-winrt

---

## Version information

- **OCR engine:** Windows.Media.Ocr (ships with Windows 10 1809+)
- **Helper version:** 1.1.0
- **Last updated:** 2026-08-24
- **DocuSearch version:** 1.1.0+
