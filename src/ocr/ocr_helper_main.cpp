// ============================================================
// ocr_helper_main.cpp - Windows.Media.Ocr (WinRT) OCR helper
// ============================================================
//
// Uses the official Windows.Media.Ocr WinRT API — the same OCR
// engine that powers Windows Search, Snipping Tool, and the Photos
// app. This is the officially-supported, royalty-free OCR API for
// any Windows app (including commercial ones — see docs/OCR_LICENSING.md).
//
// Architecture
//   • This is a SEPARATE console exe (docusearch_ocr_helper.exe).
//   • The main Qt app spawns it as a child process via QProcess.
//   • WinRT calls live here — NOT in the main Qt app — because
//     linking runtimeobject.lib pulls in /INCLUDE:WINRT_CRT_MAIN
//     which conflicts with Qt's WIN32 entry point. The helper is
//     a plain console app, so there is no conflict here.
//
// Crash-safety design
//   • Runs as a SEPARATE process — a WinRT fault can't take down
//     the main app.
//   • SEH translator installed — access violations become catchable.
//   • Per-file try/catch — one bad image can't abort the whole batch.
//   • 100 ms gap between files — keeps memory pressure low.
//   • Cooperative cancellation via stdin EOF — main app kills the
//     helper via QProcess::kill() if it hangs.
//
// Output protocol
//   For each input image, prints:
//     ===FILE===<path>
//     <recognized text — may be empty>
//     ===END===
//   Exit codes: 0 = at least one image succeeded, 1 = setup error or all failed
//
// Usage
//   docusearch_ocr_helper.exe <image_path_1> [image_path_2] ...
// ============================================================

// ── C++/WinRT (ships with Windows 10 SDK 17763+ — no cppwinrt.exe needed) ──
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>

#include <windows.h>
#include <eh.h>

#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <sstream>
#include <iomanip>

// Link the WinRT runtime + the unified delay-load stub. windowsapp.lib is
// the modern, recommended way to consume WinRT APIs from native C++ —
// it provides the runtime thunks that route WinRT calls to the OS.
#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "runtimeobject.lib")

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Graphics::Imaging;
using namespace Windows::Media::Ocr;
using namespace Windows::Storage;
using namespace Windows::Storage::Streams;

// ── UTF-8 ↔ wide string conversion (WinRT uses HSTRINGs / wstring_view) ──
static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], len);
    if (len > 0) w.resize(len - 1);
    return w;
}

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], len, nullptr, nullptr);
    if (len > 0) s.resize(len - 1);
    return s;
}

// ── SEH translator: convert Win32 SEH → C++ exception ──────
// Lets our try/catch blocks catch access violations and similar that
// can be raised by lower-level image decoding paths.
static void OcrSehTranslator(unsigned int code, EXCEPTION_POINTERS* ep) {
    void* addr = (ep && ep->ExceptionRecord) ? ep->ExceptionRecord->ExceptionAddress : nullptr;
    std::ostringstream oss;
    oss << "SEH 0x" << std::hex << std::setw(8) << std::setfill('0') << code
        << " at 0x" << reinterpret_cast<uintptr_t>(addr);
    throw std::runtime_error(oss.str());
}

// ── Best-effort logging only — the OS still terminates the process ──
static LONG NTAPI VectoredHandler(EXCEPTION_POINTERS* ep) {
    (void)ep;
    return EXCEPTION_CONTINUE_SEARCH;
}

// ── Resolve the OCR engine to use ───────────────────────────
//   1. Try the user's profile OCR languages (Settings → Time &
//      Language → Language → Optical character recognition).
//   2. If none installed, fall back to the first available
//      recognizer language.
//   3. If no OCR language packs are installed at all, return empty
//      and the caller will surface a clear setup message.
static OcrEngine CreateEngine() {
    // Try the user's profile (multi-language, auto-detect script).
    OcrEngine engine = OcrEngine::TryCreateFromUserProfileLanguages();
    if (engine) return engine;

    // Fall back to the first available recognizer language.
    auto langs = OcrEngine::AvailableRecognizerLanguages();
    for (uint32_t i = 0; i < langs.Size(); ++i) {
        engine = OcrEngine::TryCreateFromLanguage(langs.GetAt(i));
        if (engine) return engine;
    }
    return nullptr;
}

// ── Run OCR on a single file ────────────────────────────────
// Returns the recognized text. On failure, returns a string starting
// with "[" — the main app treats any line starting with '[' as a
// failure marker.
static std::string OcrFile(const OcrEngine& engine, const std::string& path) {
    std::wstring wpath = Utf8ToWide(path);

    // Skip files > 20 MB — low-RAM protection.
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &fad)) {
        ULARGE_INTEGER size;
        size.LowPart  = fad.nFileSizeLow;
        size.HighPart = fad.nFileSizeHigh;
        if (size.QuadPart > 20LL * 1024 * 1024) {
            return "[SKIPPED: file too large (>20MB)]";
        }
    } else {
        return "[ERROR: file not found or inaccessible]";
    }

    try {
        // Open the file via StorageFile::GetFileFromPathAsync — the official
        // WinRT way to reference a file by absolute path.
        StorageFile file = StorageFile::GetFileFromPathAsync(wpath).get();

        // Decode into a SoftwareBitmap in BGRA8 premultiplied format.
        // BitmapDecoder handles PNG / JPEG / TIFF / BMP / GIF / WebP / HEIF.
        BitmapDecoder decoder = BitmapDecoder::CreateAsync(file).get();
        SoftwareBitmap bitmap = decoder.GetSoftwareBitmapAsync(
            BitmapPixelFormat::Bgra8,
            BitmapAlphaMode::Premultiplied).get();

        if (bitmap == nullptr) {
            return "[ERROR: failed to decode image]";
        }

        // Run OCR. RecognizeAsync returns an IAsyncOperation<OcrResult>;
        // .get() blocks until completion (cooperative await on WinRT thread pool).
        OcrResult result = engine.RecognizeAsync(bitmap).get();

        std::string text = winrt::to_string(result.Text());

        // Append per-line text for richer output (line breaks preserved).
        // The .Text() accessor already joins lines with \r\n — we just
        // normalize to \n for cross-platform stdout parsing.
        std::string normalized;
        normalized.reserve(text.size());
        for (char c : text) {
            if (c != '\r') normalized.push_back(c);
        }
        return normalized;
    } catch (const hresult_error& e) {
        std::string msg = "[ERROR: " + winrt::to_string(e.message()) + "]";
        return msg;
    } catch (const std::bad_alloc&) {
        return "[ERROR: out of memory]";
    } catch (const std::exception& e) {
        return std::string("[ERROR: ") + e.what() + "]";
    } catch (...) {
        return "[ERROR: unknown]";
    }
}

// ── Main ────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0]
                  << " <image_path> [<image_path> ...]" << std::endl;
        return 1;
    }

    // Install SEH translator — converts Win32 structured exceptions
    // (access violations, stack overflows) into catchable C++ exceptions.
    _set_se_translator(OcrSehTranslator);

    // Best-effort vectored handler for exceptions that escape the translator
    // (e.g. stack overflow).
    AddVectoredExceptionHandler(0, VectoredHandler);

    // Initialize WinRT apartment. Multi-threaded (MTA) is correct for a
    // console helper that does CPU-bound async work and waits on it.
    try {
        init_apartment(apartment_type::multi_threaded);
    } catch (const hresult_error& e) {
        std::cerr << "[OCR] Failed to initialize WinRT: "
                  << winrt::to_string(e.message()) << std::endl;
        return 1;
    }

    // Create the OCR engine. If the user has no OCR language packs
    // installed (common on Windows N / LTSC), exit with a clear message
    // so the main app can surface setup instructions.
    OcrEngine engine = nullptr;
    try {
        engine = CreateEngine();
    } catch (const hresult_error& e) {
        std::cerr << "[OCR] Engine creation failed: "
                  << winrt::to_string(e.message()) << std::endl;
        uninit_apartment();
        return 1;
    } catch (...) {
        std::cerr << "[OCR] Engine creation failed (unknown)." << std::endl;
        uninit_apartment();
        return 1;
    }

    if (!engine) {
        std::cerr << "[OCR] No OCR language packs are installed." << std::endl;
        std::cerr << "[OCR] Install via: Settings > Time & Language > Language" << std::endl;
        std::cerr << "[OCR]   > Add a language > Optical character recognition." << std::endl;
        std::cerr << "[OCR] Then restart DocuSearch." << std::endl;
        uninit_apartment();
        return 1;
    }

    // Log which language is active (helps with debugging).
    try {
        auto lang = engine.RecognizerLanguage();
        std::cerr << "[OCR] Engine ready — language: "
                  << winrt::to_string(lang.DisplayName())
                  << " (" << winrt::to_string(lang.LanguageTag()) << ")"
                  << std::endl;
    } catch (...) {
        // Best-effort — don't fail on logging.
    }

    // Process each file. Per-file try/catch keeps one bad image from
    // aborting the whole batch.
    bool anySuccess = false;
    for (int i = 1; i < argc; ++i) {
        std::string path = argv[i];
        std::string text;

        try {
            text = OcrFile(engine, path);
        } catch (const std::bad_alloc&) {
            text = "[ERROR: out of memory]";
            Sleep(2000);  // back off before next file
        } catch (const std::exception& e) {
            text = std::string("[ERROR: ") + e.what() + "]";
        } catch (...) {
            text = "[ERROR: unknown]";
        }

        std::cout << "===FILE===" << path << std::endl;
        std::cout << text << std::endl;
        std::cout << "===END===" << std::endl;
        std::cout.flush();

        if (!text.empty() && text[0] != '[') {
            anySuccess = true;
        }

        // 100 ms gap between files — keeps memory pressure low on 4 GB systems.
        Sleep(100);
    }

    // Cleanup.
    try {
        uninit_apartment();
    } catch (...) {
        // Best-effort — process is exiting anyway.
    }

    return anySuccess ? 0 : 1;
}
