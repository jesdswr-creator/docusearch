#pragma once

// ============================================================
// WinRtOcrEngine.h - Unlimited OCR via Windows.Media.Ocr
// ============================================================
//
// Replacement for oneocr.dll-based WindowsOcrEngine.
//
// Why: oneocr.dll requires the user to extract it from the Windows 11
// Snipping Tool (legal gray area), is limited to ~5 languages, and the
// setup script is fragile. WinRT OCR is:
//   • Built into Windows 10+ (no DLL to bundle)
//   • Microsoft-licensed (no legal gray area)
//   • Supports 50+ languages via Windows language packs
//   • Truly "unlimited" — no per-machine setup, no extraction step
//
// The actual OCR runs in a separate helper exe
// (docusearch_winrt_ocr_helper.exe) so that any WinRT apartment
// crashes are isolated from the main app. This is the same pattern
// the oneocr helper uses.
//
// The main app tries this engine FIRST. If Windows is too old
// (pre-10.0.15063) or no language pack is installed, it falls back
// to WindowsOcrEngine (oneocr) if available.
// ============================================================

#include <QString>
#include <QImage>

namespace DocuSearch {

class WinRtOcrEngine {
public:
    WinRtOcrEngine();
    ~WinRtOcrEngine();

    WinRtOcrEngine(const WinRtOcrEngine&)            = delete;
    WinRtOcrEngine& operator=(const WinRtOcrEngine&) = delete;

    // Singleton accessor.
    static WinRtOcrEngine& instance();

    // Initialize: checks that the helper exe is present.
    // Returns true if the helper is found. Does NOT verify that WinRT
    // OCR is actually functional on this Windows install — that check
    // happens lazily on the first ocrFile() call.
    bool init();

    // Shutdown hook (no-op — fresh QProcess per call).
    void shutdown() {}

    // OCR an image (saved to a temp PNG, then OCR'd via the helper).
    QString ocrImage(const QImage& img);

    // OCR a file directly (PNG/JPG/BMP/TIFF/WebP).
    QString ocrFile(const QString& path);

    bool isInitialized() const { return initialized_; }

    // True if the WinRT helper exe is present next to the app.
    bool isAvailable() const { return helperPresent_; }

    // True if at least one OCR call has succeeded (proves the engine works
    // on this Windows install). Updated after each ocrFile() call.
    bool isFunctional() const { return functional_; }

    // Returns the list of OCR languages installed on this system.
    // (Queried lazily from the helper on first call — may be empty if
    // the helper can't be reached.)
    static QStringList availableLanguages();

    // Force a specific language tag for subsequent OCR calls.
    // Empty string = auto-detect from user profile languages.
    void setLanguageOverride(const QString& langTag);
    QString languageOverride() const { return langOverride_; }

private:
    bool initialized_     = false;
    bool helperPresent_   = false;
    bool functional_      = false;
    QString langOverride_;  // empty = auto-detect
};

} // namespace DocuSearch
