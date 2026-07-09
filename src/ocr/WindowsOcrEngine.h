#pragma once

// ============================================================
// WindowsOcrEngine.h - OCR wrapper (now uses RapidOCR)
// ============================================================
//
// This class was previously a wrapper around Windows.Media.Ocr.
// It has been refactored to use RapidOCR (ONNX-based) instead,
// which is:
//   - More reliable (works on all Windows 10/11 systems)
//   - Faster (no PowerShell subprocess per invocation)
//   - Lighter (no Windows SDK dependency)
//   - More accurate (PaddleOCR-trained models)
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

    // Initialize the OCR engine. Returns true on success.
    bool init();

    // OCR an image. Returns extracted text (empty on failure).
    QString ocrImage(const QImage& img);

    // OCR a file directly (PNG, JPG, BMP, TIFF, WebP).
    QString ocrFile(const QString& path);

    bool isInitialized() const { return initialized_; }

    // Returns the list of available OCR language tags.
    static QStringList availableLanguages();

private:
    bool initialized_ = false;
};

} // namespace DocuSearch
