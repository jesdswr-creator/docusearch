#pragma once

// ============================================================
// WindowsOcrEngine.h - OCR wrapper using RapidOcrCpp (pure C++)
// ============================================================
//
// Uses RapidOcrCpp (https://github.com/RapidAI/RapidOcrCpp) — a
// pure C++ OCR library based on PaddleOCR ONNX models.
//
// NO Python required. NO PowerShell. NO Windows SDK dependency.
// Just ONNX Runtime + OpenCV (both from vcpkg) + ~17MB of models.
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

    // Initialize the OCR engine by loading the ONNX models.
    // Returns true on success, false on failure.
    // Models are expected at <appDir>/models/
    bool init();

    // OCR an image. Returns extracted text (empty on failure).
    QString ocrImage(const QImage& img);

    // OCR a file directly (PNG, JPG, BMP, TIFF, WebP).
    QString ocrFile(const QString& path);

    bool isInitialized() const { return initialized_; }

    // Returns the list of available OCR languages.
    static QStringList availableLanguages();

private:
    bool initialized_ = false;
    // OcrLite* — void* to avoid pulling RapidOcr headers here.
    void* ocrLite_ = nullptr;
    QString modelsDir_;
};

} // namespace DocuSearch
