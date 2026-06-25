// ============================================================
// OcrEngine.cpp
// ============================================================

#include "OcrEngine.h"
#include "../core/Logger.h"

#ifdef DOCUSEARCH_HAS_TESSERACT
#  include <tesseract/baseapi.h>
#  include <leptonica/allheaders.h>
#endif

#include <QImage>
#include <QFileInfo>

namespace DocuSearch {

OcrEngine::OcrEngine(const QString& tessdataPath, const QString& language)
    : tessdataPath_(tessdataPath), language_(language.isEmpty() ? "eng" : language) {}

OcrEngine::~OcrEngine() {
#ifdef DOCUSEARCH_HAS_TESSERACT
    if (api_) {
        static_cast<tesseract::TessBaseAPI*>(api_)->End();
        delete static_cast<tesseract::TessBaseAPI*>(api_);
    }
#endif
}

bool OcrEngine::init() {
#ifdef DOCUSEARCH_HAS_TESSERACT
    if (initialized_) return true;
    auto* api = new tesseract::TessBaseAPI();
    const QByteArray td = tessdataPath_.toUtf8();
    const QByteArray lang = language_.toUtf8();
    if (api->Init(td.isEmpty() ? nullptr : td.constData(), lang.constData()) != 0) {
        DS_ERROR("OCR", "Tesseract Init failed");
        delete api;
        return false;
    }
    api_->SetPageSegMode(static_cast<tesseract::PageSegMode>(psm_));
    api_ = api;
    initialized_ = true;
    DS_INFO("OCR", QString("Tesseract ready (lang=%1, psm=%2)").arg(language_).arg(psm_));
    return true;
#else
    DS_WARN("OCR", "Built without Tesseract - OCR unavailable");
    return false;
#endif
}

void OcrEngine::setPsm(int psm) {
    psm_ = psm;
#ifdef DOCUSEARCH_HAS_TESSERACT
    if (api_) static_cast<tesseract::TessBaseAPI*>(api_)->SetPageSegMode(
        static_cast<tesseract::PageSegMode>(psm_));
#endif
}

QString OcrEngine::ocrImage(const QImage& img) {
#ifdef DOCUSEARCH_HAS_TESSERACT
    if (!initialized_ && !init()) return {};
    if (img.isNull()) return {};
    auto* api = static_cast<tesseract::TessBaseAPI*>(api_);

    // Tesseract needs RGB or 8-bit. Convert QImage to a compatible format.
    QImage conv = img.convertToFormat(QImage::Format_RGB888);
    if (conv.isNull()) conv = img.convertToFormat(QImage::Format_Grayscale8);
    if (conv.isNull()) return {};

    api->SetImage(conv.bits(), conv.width(), conv.height(),
                  conv.bytesPerLine() / conv.width(), conv.bytesPerLine());
    char* text = api->GetUTF8Text();
    QString result = text ? QString::fromUtf8(text) : QString();
    if (text) api->ReleaseUTF8Text(text);
    return result;
#else
    Q_UNUSED(img);
    return {};
#endif
}

QString OcrEngine::ocrFile(const QString& path) {
    if (!QFileInfo::exists(path)) return {};
    QImage img(path);
    if (img.isNull()) {
        DS_WARN("OCR", "Cannot load image: " + path);
        return {};
    }
    return ocrImage(img);
}

} // namespace DocuSearch
