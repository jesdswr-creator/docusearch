#pragma once

// ============================================================
// OcrEngine.h — Tesseract wrapper (lazy-init per worker thread)
// ============================================================

#include <QString>
#include <QByteArray>
#include <QImage>

namespace DocuSearch {

class OcrEngine {
public:
    OcrEngine(const QString& tessdataPath = QString(),
              const QString& language = "eng");
    ~OcrEngine();

    OcrEngine(const OcrEngine&)            = delete;
    OcrEngine& operator=(const OcrEngine&) = delete;

    // Initialize the underlying Tesseract API. Returns true on success.
    bool init();

    // OCR an image (already loaded as QImage). Returns extracted text.
    QString ocrImage(const QImage& img);

    // Convenience: load file and OCR. Returns empty on failure.
    QString ocrFile(const QString& path);

    // Set page-segmentation mode (3 = auto, 6 = uniform block, 11 = sparse).
    void setPsm(int psm);
    int  psm() const { return psm_; }

    bool isInitialized() const { return initialized_; }

private:
    QString tessdataPath_;
    QString language_;
    int     psm_ = 3;
    bool    initialized_ = false;
    void*   api_ = nullptr;  // tesseract::TessBaseAPI*
};

} // namespace DocuSearch
