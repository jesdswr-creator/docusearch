#pragma once

// ============================================================
// WindowsOcrEngine.h - OCR wrapper using oneocr.dll
// ============================================================
//
// Uses oneocr.dll — the native OCR engine shipped with the Windows 11
// Snipping Tool (Microsoft.ScreenSketch). The DLL is loaded by a
// separate helper exe (docusearch_ocr_helper.exe) which calls the
// C-ABI exports. This is a drop-in replacement for the previous
// WinRT-based implementation that was crashing the app on low-RAM
// Windows systems.
//
// NO Python required. NO WinRT. NO apartment threading.
// The oneocr.dll + oneocr.onemodel + onnxruntime.dll files must be
// obtained from the locally-installed Snipping Tool via
// scripts/get_oneocr.ps1 and placed next to docusearch.exe.
//
// The class name is kept as WindowsOcrEngine for backward
// compatibility with existing code that references it.
// ============================================================

#include <QString>
#include <QImage>

namespace DocuSearch {

class WindowsOcrEngine {
public:
    WindowsOcrEngine();
    ~WindowsOcrEngine();

    WindowsOcrEngine(const WindowsOcrEngine&)            = delete;
    WindowsOcrEngine& operator=(const WindowsOcrEngine&) = delete;

    // Singleton accessor — used by status bar / UI to query OCR status
    // without creating a new instance each time (which would lose the
    // cached oneocrAvailable_ flag set by previous OCR runs).
    static WindowsOcrEngine& instance();

    // Initialize the OCR engine. Checks for the helper exe and the
    // oneocr.dll + model files. Returns true if the helper is present.
    // Even if oneocr.dll is missing, returns true so the helper can
    // surface the install-instructions error message to the user.
    bool init();

    // OCR an image. Returns extracted text (empty on failure).
    QString ocrImage(const QImage& img);

    // OCR a file directly (PNG, JPG, BMP, TIFF, WebP).
    QString ocrFile(const QString& path);

    bool isInitialized() const { return initialized_; }

    // Returns true if oneocr.dll + oneocr.onemodel are available
    // alongside the app. If false, OCR calls will return empty and
    // the user should run scripts/get_oneocr.ps1.
    bool isOneocrAvailable() const;

    // Returns the list of available OCR languages.
    static QStringList availableLanguages();

private:
    // Search for the directory containing oneocr.dll.
    // Returns empty string if not found.
    QString findOneocrDir() const;

    bool initialized_ = false;
    bool oneocrAvailable_ = false;
};

} // namespace DocuSearch
