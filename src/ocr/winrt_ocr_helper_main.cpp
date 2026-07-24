// ============================================================
// winrt_ocr_helper_main.cpp - WinRT Windows.Media.Ocr helper (UNLIMITED)
// ============================================================
//
// Truly unlimited OCR replacement for oneocr.dll:
//   • Uses Windows.Media.Ocr — built into Windows 10+ (no DLL to bundle).
//   • No license issues — Microsoft's OCR is part of the OS.
//   • 50+ languages available via Windows language packs.
//   • Auto-detects the best installed language by default.
//
// Why a separate exe (not in-process)?
//   • Previous in-process WinRT OCR crashed the app on low-RAM systems
//     (apartment threading + IAsyncOperation issues).
//   • Running as a separate process means crashes are isolated.
//
// Uses C++/WinRT (the headers ship with Windows SDK 10.0.17763+ — no
// vcpkg dependency needed). The helper is built as a separate exe with
// /MD so it doesn't conflict with the main app's /MT linkage.
//
// Usage: docusearch_winrt_ocr_helper.exe <image_path_1> [image_path_2] ...
//   Optional: env var DOCUSEARCH_OCR_LANG=xx-XX forces a language tag.
// Output for each image:
//   ===FILE===<path>
//   <recognized text>
//   ===END===
// Exit codes: 0 = at least one image succeeded, 1 = all failed / setup error
// ============================================================

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Storage.h>

#include <iostream>
#include <string>
#include <vector>
#include <cstdint>

#pragma comment(lib, "windowsapp.lib")

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Graphics::Imaging;
using namespace winrt::Windows::Media::Ocr;
using namespace winrt::Windows::Storage::Streams;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Globalization;

// ── Helpers ─────────────────────────────────────────────────

static std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                  nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                        out.data(), len, nullptr, nullptr);
    return out;
}

static std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), len);
    return out;
}

// Synchronously wait for an IAsyncAction / IAsyncOperation<T> with a timeout.
// Returns true on completion, false on timeout.
template<typename TAsync>
static bool wait_async(const TAsync& op, uint32_t timeoutMs = 120000) {
    op.Completed([](const auto&, auto) {});  // no-op callback to ensure progress
    uint32_t elapsed = 0;
    while (op.Status() == AsyncStatus::Started && elapsed < timeoutMs) {
        Sleep(20);
        elapsed += 20;
    }
    return op.Status() == AsyncStatus::Completed;
}

// Run OCR on a single image file. Returns recognized text (UTF-8) or empty on failure.
static std::string ocr_image(const std::wstring& path, const std::wstring& langTag) {
    try {
        // 1. Open the file via WinRT StorageFile (handles PNG/JPG/BMP/TIFF/WebP).
        auto file = StorageFile::GetFileFromPathAsync(path).get();
        if (!file) return std::string();

        // 2. Decode into a SoftwareBitmap (BGRA8).
        auto stream = file.OpenAsync(FileAccessMode::Read).get();
        BitmapDecoder decoder = BitmapDecoder::CreateAsync(stream).get();
        SoftwareBitmap bitmap = decoder.GetSoftwareBitmapAsync().get();

        if (!bitmap) return std::string();

        // Ensure BGRA8 (WinRT OcrEngine requires this).
        if (bitmap.BitmapPixelFormat() != BitmapPixelFormat::Bgra8) {
            bitmap = SoftwareBitmap::Convert(bitmap, BitmapPixelFormat::Bgra8,
                                             BitmapAlphaMode::Premultiplied);
        }

        // 3. Build the OcrEngine.
        OcrEngine engine = nullptr;
        if (!langTag.empty()) {
            auto language = Language(hstring(langTag));
            if (OcrEngine::IsLanguageSupported(language)) {
                engine = OcrEngine::TryCreateFromLanguage(language);
            }
        }
        if (!engine) {
            // Fall back to user profile languages (auto-detect).
            engine = OcrEngine::TryCreateFromUserProfileLanguages();
        }
        if (!engine) {
            std::cerr << "[winrt_ocr] No OCR engine available. "
                         "Install a language pack from Windows Settings."
                      << std::endl;
            return std::string();
        }

        // 4. Run OCR.
        OcrResult result = engine.RecognizeAsync(bitmap).get();

        // 5. Concatenate lines.
        std::wstring text;
        for (const auto& line : result.Lines()) {
            if (!text.empty()) text += L"\n";
            text += std::wstring(line.Text());
        }
        return wide_to_utf8(text);
    } catch (const winrt::hresult_error& e) {
        std::cerr << "[winrt_ocr] HRESULT error: "
                  << wide_to_utf8(std::wstring(e.message()))
                  << " (0x" << std::hex << static_cast<uint32_t>(e.code()) << ")"
                  << std::endl;
        return std::string();
    } catch (const std::exception& e) {
        std::cerr << "[winrt_ocr] Exception: " << e.what() << std::endl;
        return std::string();
    } catch (...) {
        std::cerr << "[winrt_ocr] Unknown exception" << std::endl;
        return std::string();
    }
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::cerr << "Usage: docusearch_winrt_ocr_helper.exe <image_path_1> [...]"
                  << std::endl;
        std::cerr << "Note: Windows 10+ with at least one OCR language pack is required."
                  << std::endl;
        std::cerr << "Optional: set DOCUSEARCH_OCR_LANG=xx-XX to force a language tag."
                  << std::endl;
        return 1;
    }

    // Initialize WinRT.
    try {
        init_apartment(apartment_type::single_threaded);
    } catch (...) {
        std::cerr << "ERROR: Failed to initialize WinRT." << std::endl;
        return 1;
    }

    // Read optional language override.
    std::wstring langTag;
    if (const char* env = std::getenv("DOCUSEARCH_OCR_LANG")) {
        if (*env) langTag = utf8_to_wide(env);
    }

    int successCount = 0;

    for (int i = 1; i < argc; ++i) {
        std::wstring path = argv[i];
        std::string text = ocr_image(path, langTag);

        if (text.empty()) continue;

        std::cout << "===FILE===" << wide_to_utf8(path) << "\n";
        std::cout << text << "\n";
        std::cout << "===END===" << std::endl;
        ++successCount;

        Sleep(50);  // keep memory pressure low on low-RAM systems
    }

    uninit_apartment();
    return (successCount > 0) ? 0 : 1;
}
