#pragma once

// ============================================================
// WindowsOcrEngine.h - Windows.Media.Ocr wrapper (WinRT/C++)
// ============================================================
//
// Uses the built-in Windows 10/11 OCR engine. No third-party
// dependencies, no model files to ship — the engine is already
// on every Windows 10+ system.
//
// Free for commercial use (Windows SDK EULA).
//
// Usage:
//   WindowsOcrEngine engine;
//   if (engine.init()) {
//       QString text = engine.ocrImage(qimage);
//   }
//
// For PDFs, render each page to a QImage via Poppler, then call
// ocrImage() on each page.
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

    // Initialize the OCR engine. Returns true on success.
    // Uses the user's installed OCR languages (English is always
    // available on English Windows; other languages via Settings).
    bool init();

    // OCR an image. Returns extracted text (empty on failure).
    QString ocrImage(const QImage& img);

    // Convenience: load file and OCR. Returns empty on failure.
    QString ocrFile(const QString& path);

    bool isInitialized() const { return initialized_; }

    // Returns the list of available OCR language tags (e.g.,
    // "en", "en-US", "hi", "zh-Hans-CN"). Empty if not initialized.
    static QStringList availableLanguages();

private:
    bool initialized_ = false;
};

} // namespace DocuSearch
