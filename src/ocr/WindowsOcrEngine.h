#pragma once

// ============================================================
// WindowsOcrEngine.h - Windows.Media.Ocr wrapper
// ============================================================
//
// Uses the built-in Windows 10/11 OCR engine. No third-party
// dependencies, no model files to ship — the engine is already
// on every Windows 10+ system.
//
// Implementation note: We avoid the WinRT C++ headers because they
// auto-link `runtimeobject.lib` which conflicts with Qt's WIN32
// entry point. Instead, we invoke OCR via PowerShell which uses
// .NET's WinRT projection. This is slightly slower (~200 ms per
// invocation) but completely avoids the linker conflict.
//
// Usage:
//   WindowsOcrEngine engine;
//   if (engine.init()) {
//       QString text = engine.ocrFile("C:/path/to/scanned.pdf");
//   }
//
// For PDFs, the caller (MainWindow::onOcrThisFile) renders each
// page to a QImage via Poppler, then calls ocrImage() on each page.
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

    // Initialize the OCR engine. On Windows this loads combase.dll
    // and verifies that Windows.Media.Ocr.OcrEngine is available.
    // On non-Windows platforms, returns false.
    bool init();

    // OCR an image. The image is saved to a temp PNG file and
    // OCR'd via PowerShell + Windows.Media.Ocr.
    // Returns the extracted text (empty on failure).
    QString ocrImage(const QImage& img);

    // OCR a file directly (PNG, JPG, BMP, TIFF). Uses PowerShell
    // to invoke Windows.Media.Ocr.OcrEngine.RecognizeAsync.
    // Returns the extracted text (empty on failure).
    QString ocrFile(const QString& path);

    bool isInitialized() const { return initialized_; }

    // Returns the list of available OCR language tags (e.g.,
    // "en", "en-US", "hi", "zh-Hans-CN"). Empty if not initialized
    // or if running on non-Windows platforms.
    static QStringList availableLanguages();

private:
    bool initialized_ = false;
    // Pointer to the IOcrEngine COM interface. Kept as void* to avoid
    // pulling in WinRT headers in this file. Only used on Windows.
    void* engine_ = nullptr;
};

} // namespace DocuSearch
