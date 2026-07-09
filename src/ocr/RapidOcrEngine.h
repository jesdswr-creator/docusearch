#pragma once

// ============================================================
// RapidOcrEngine.h - Lightweight ONNX-based OCR engine
// ============================================================
//
// Uses RapidOCR (onnxruntime-based) for OCR. RapidOCR is:
//   - Lightweight (~12MB model, no Python/CUDA dependency)
//   - Fast (CPU inference via ONNX Runtime)
//   - Accurate (PaddleOCR-trained models)
//   - Free for commercial use (Apache 2.0)
//
// The ONNX models are bundled with the app (~12MB total):
//   - det.onnx: text detection model
//   - rec.onnx: text recognition model
//   - cls.onnx: text direction classification model (optional)
//
// For PDFs, each page is rendered to an image via Poppler, then OCR'd.
// For images (PNG/JPG/etc.), the file is loaded directly and OCR'd.
//
// This replaces the Windows OCR (PowerShell bridge) approach which was
// slow (~2s per invocation) and didn't work on some Windows 10 systems.
// ============================================================

#include <QString>
#include <QImage>

namespace DocuSearch {

class RapidOcrEngine {
public:
    RapidOcrEngine();
    ~RapidOcrEngine();

    RapidOcrEngine(const RapidOcrEngine&)            = delete;
    RapidOcrEngine& operator=(const RapidOcrEngine&) = delete;

    // Initialize the OCR engine by loading the ONNX models.
    // Returns true on success, false on failure.
    // Model files are expected at:
    //   <appDir>/models/det.onnx
    //   <appDir>/models/rec.onnx
    //   <appDir>/models/dict.txt
    bool init();

    // OCR an image. Returns extracted text (empty on failure).
    QString ocrImage(const QImage& img);

    // OCR a file directly (PNG, JPG, BMP, TIFF, WebP).
    // Returns extracted text (empty on failure).
    QString ocrFile(const QString& path);

    bool isInitialized() const { return initialized_; }

    // Returns the list of available OCR languages.
    // RapidOCR supports: en, zh, chinese_sim, chinese_tra, korean, japanese
    static QStringList availableLanguages();

private:
    bool initialized_ = false;
    void* detSession_  = nullptr;  // Ort::Session* (void* to avoid ONNX headers in .h)
    void* recSession_  = nullptr;  // Ort::Session*
    void* env_         = nullptr;  // Ort::Env*
    QStringList dict_;             // character dictionary for recognition
    QString modelsDir_;            // path to models directory
};

} // namespace DocuSearch
