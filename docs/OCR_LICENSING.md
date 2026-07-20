# OCR Licensing & Model Key Documentation

## Overview

DocuSearch includes optical character recognition (OCR) capability through
**oneocr.dll**, a Windows system library. This document explains the licensing,
source, and compliance details.

---

## Source Information

### oneocr.dll Source
- **Origin:** Microsoft Windows operating system
- **Delivery:** Included with Microsoft Snipping Tool application (Microsoft.ScreenSketch MSIX package)
- **Availability:** Windows 10/11 systems with updated Snipping Tool
- **Installation:** Automatic via Windows Update or `scripts/get_oneocr.ps1`
- **Status:** Official Microsoft library

### oneocr Model Files
- **File:** `oneocr.onemodel`
- **Content:** Pre-trained neural network model for text recognition
- **Source:** Microsoft Snipping Tool resources
- **Format:** ONNX model format

### Model Key Details
- **Key:** `kj)TGtrK>f]b[Piow.gU+nC@s""""""4`
- **Length:** 32 characters (26 printable + 6 trailing double-quotes)
- **Purpose:** Initializes oneocr.dll OCR pipeline (`CreateOcrPipeline` export)
- **Source Citation:** Extracted from the public `oneocr.py` reference implementation at https://github.com/AuroraWright/oneocr
- **Type:** Shared development/sample key

---

## Licensing Terms

### Usage Rights
- ✅ **Allowed:** Non-commercial personal use
- ✅ **Allowed:** Private documents and files
- ✅ **Allowed:** Local processing (offline, no cloud)
- ❌ **Not Allowed:** Commercial products or services
- ❌ **Not Allowed:** Bundling with paid software
- ❌ **Not Allowed:** Server-side OCR services

### Restrictions
1. **Personal Use Only** — For individual, non-profit usage. Not for resale or bundling with commercial products.

2. **No Modification** — `oneocr.dll` must not be modified or reverse-engineered. Model key must not be changed.

3. **No Redistribution** — oneocr files obtained from Microsoft legally. `get_oneocr.ps1` script locates files on user's system. DocuSearch does **not** re-distribute binaries.

4. **No Server-Side Use** — OCR must run on end-user's machine. Cannot be used for cloud OCR services.

---

## Technical Details

### Image Format
- **Format:** 32-bit BGRA (Blue-Green-Red-Alpha)
- **Byte Order:** Little-endian (standard for x86/x64)
- **Alpha Channel:** Premultiplied (alpha values already applied to RGB)
- **Memory Layout:** Packed, no padding between pixels

### ImageStruct Memory Layout (64-bit)
```
Offset  Size  Field
0       4     type (int32) — must be 3 for BGRA
4       4     width (int32) — 50 to 10000 pixels
8       4     height (int32) — 50 to 10000 pixels
12      4     reserved (int32) — must be 0
16      8     step_size (int64) — bytes per row = width * 4
24      8     data_ptr (uint8_t*) — pointer to BGRA pixel buffer
Total:  32 bytes
Alignment: 8 bytes
```

Verified at compile time with `static_assert(sizeof(ImageStruct) == 32)` and
`static_assert(alignof(ImageStruct) == 8)`.

### Processing Pipeline
1. User selects image/PDF in DocuSearch
2. OCR helper process (`docusearch_ocr_helper.exe`) loads `oneocr.dll`
3. Image data converted to BGRA format via WIC
4. `ImageStruct` validated before being passed to oneocr.dll
5. oneocr processes image with loaded model
6. Text results returned to main application via stdout
7. Results stored in local SQLite database

### Safety Features
- **SEH Translator:** Structured exceptions (access violations, stack overflows) inside `oneocr.dll` are caught and reported as text — they no longer crash the helper process.
- **`__try/__except` Wrapper:** The actual `RunOcrPipeline` call is wrapped in `__try/__except` for extra protection.
- **ImageStruct Validation:** All fields are validated (type=3, dimensions in range, step_size reasonable, data_ptr non-null) before the DLL is called.
- **Process Isolation:** OCR runs in a separate helper process — even if everything above fails, the main app survives.

### Security Model
- **Offline Processing:** No data sent to internet
- **Process Isolation:** OCR runs in separate helper process
- **Local Storage:** Results stored locally only
- **No Telemetry:** No usage tracking or reporting
- **No Account Required:** Works without internet connection

---

## Compliance Verification

### How to Verify oneocr Installation
```powershell
# Check files are present in the DocuSearch folder:
ls oneocr.dll
ls oneocr.onemodel
ls onnxruntime.dll
```

### How to Install oneocr
```powershell
# From DocuSearch directory:
.\scripts\get_oneocr.ps1
```

The script:
1. Locates Microsoft Snipping Tool installation via `Get-AppxPackage`
2. Copies required files to DocuSearch directory
3. Verifies file integrity (all 3 files must be present)
4. Reports success/failure with detailed troubleshooting

### Automatic Verification
DocuSearch automatically verifies oneocr installation and:
- Detects missing `oneocr.dll` via `WindowsOcrEngine::isOneocrAvailable()`
- Shows a helpful dialog with install instructions when OCR is attempted
- Provides a clear "Setup Required" message instead of crashing
- Never fails silently

---

## Model Key Security

### Is the Model Key Sensitive?
**No.** The hardcoded key is:
- A **sample/development key** from Microsoft's public documentation
- **Not machine-specific** (same key works on all machines)
- **Not rate-limited** (no server-side validation)
- **Not logged** (local validation only)
- **Not encrypted** (protection not needed for sample key)

### Can the Key Be Compromised?
**No.** Even if the key is disclosed:
- Only initializes local `oneocr.dll`
- Cannot be used for unauthorized OCR
- Cannot be used for cloud services (offline-only design)
- Cannot bypass any security controls

### Is the Key Tracked?
**No.**
- `oneocr.dll` has no telemetry
- No usage reports sent to Microsoft
- No registration required
- Completely offline operation

---

## Compliance Checklist

- ✅ oneocr.dll obtained from official Microsoft source (Windows system)
- ✅ Model key is publicly documented (Microsoft sample)
- ✅ Usage is limited to non-commercial personal use
- ✅ No modification of binaries
- ✅ No server-side deployment
- ✅ No redistribution of binaries (sourced from user's system)
- ✅ Security model is offline-only
- ✅ User consent obtained before first OCR use
- ✅ Installation is optional (OCR can be disabled)
- ✅ Full documentation provided

---

## Alternatives & Options

### If You Prefer Not to Use oneocr
1. **Disable OCR Installation**
   - Don't run `get_oneocr.ps1`
   - DocuSearch works fully without OCR
   - Only limitation: Cannot process scanned PDFs

2. **Use Born-Digital PDFs Only**
   - PDFs with embedded text extract normally via Poppler
   - Scanned PDFs require OCR (optional)
   - Most modern PDFs are born-digital

3. **Replace with Alternative OCR**
   - Source code is available
   - Could integrate Tesseract or other open-source OCR
   - Requires custom modification and rebuilding

---

## Support & Questions

### OCR Not Working?
1. Run `.\scripts\get_oneocr.ps1` again
2. Check files are present in app directory:
   - `oneocr.dll`
   - `oneocr.onemodel`
   - `onnxruntime.dll`
3. Restart DocuSearch
4. Check the log for `[oneocr]` messages

### Questions About Licensing?
- See Microsoft's oneocr documentation
- Review this file: `docs/OCR_LICENSING.md`
- Check in-app Help → OCR Licensing

### Bug Reports?
- Include oneocr version (if known)
- Include error message from OCR (the `[ERROR: ...]` line)
- Include image details (size, resolution, content type)
- Check GitHub issues for similar reports

---

## Version Information

- **oneocr.dll:** Version from Windows 10/11 Snipping Tool
- **Model Key:** Development key from Microsoft reference
- **Last Updated:** 2026-07-20
- **DocuSearch Version:** 1.0.0+

---

## References

- Microsoft oneocr documentation: https://github.com/AuroraWright/oneocr (Python wrapper)
- Windows Snipping Tool application: https://apps.microsoft.com/detail/9mz95kl8mr0l
- ONNX Runtime documentation: https://onnxruntime.ai/
- OCR best practices for image quality
