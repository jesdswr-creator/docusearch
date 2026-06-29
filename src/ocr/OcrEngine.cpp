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
#include <QDir>
#include <QCoreApplication>
#include <QStandardPaths>

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

// Returns the first tessdata directory that contains at least one
// .traineddata file, or an empty string if none was found.
//
// Search order (first match wins):
//   1. The user-configured tessdataPath_ (Settings -> OCR -> Tessdata path).
//   2. <app-exe-dir>/tessdata           (bundled with MSI/ZIP install).
//   3. C:\Program Files\Tesseract-OCR\tessdata  (standalone Tesseract install).
//   4. The TESSDATA_PREFIX environment variable.
//
// Returning empty lets Tesseract fall back to its compiled-in default
// (which is usually wrong on Windows, but at least doesn't crash).
static QString findTessdataDir(const QString& userPath) {
    const QStringList candidates = {
        userPath,
        QCoreApplication::applicationDirPath() + "/tessdata",
        QStringLiteral("C:/Program Files/Tesseract-OCR/tessdata"),
        QString::fromLocal8Bit(qgetenv("TESSDATA_PREFIX"))
    };
    for (const QString& c : candidates) {
        if (c.isEmpty()) continue;
        QDir d(c);
        if (!d.exists()) continue;
        // Only accept the dir if it actually contains a .traineddata
        // file - otherwise Tesseract Init() will fail silently.
        const QStringList trained = d.entryList({"*.traineddata"},
                                                QDir::Files, QDir::Name);
        if (!trained.isEmpty()) {
            return d.absolutePath();
        }
    }
    return {};
}

bool OcrEngine::init() {
#ifdef DOCUSEARCH_HAS_TESSERACT
    if (initialized_) return true;
    auto* api = new tesseract::TessBaseAPI();

    // Resolve the tessdata directory. If the user explicitly set one
    // in Settings, respect it. Otherwise auto-discover the bundled
    // tessdata folder (next to DocuSearch.exe) so OCR works out of
    // the box after install.
    const QString resolved = findTessdataDir(tessdataPath_);
    if (!resolved.isEmpty() && resolved != tessdataPath_) {
        DS_INFO("OCR", "Auto-discovered tessdata at: " + resolved);
    }
    const QByteArray td = resolved.toUtf8();
    const QByteArray lang = language_.toUtf8();
    if (api->Init(td.isEmpty() ? nullptr : td.constData(), lang.constData()) != 0) {
        DS_ERROR("OCR", "Tesseract Init failed - tessdata dir: '"
                 + resolved + "' (user-configured: '" + tessdataPath_ + "')");
        delete api;
        return false;
    }
    api->SetPageSegMode(static_cast<tesseract::PageSegMode>(psm_));
    api_ = api;
    initialized_ = true;
    DS_INFO("OCR", QString("Tesseract ready (lang=%1, psm=%2, tessdata=%3)")
                .arg(language_).arg(psm_).arg(resolved.isEmpty()
                                              ? QStringLiteral("<default>") : resolved));
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
    // Tesseract 5.x removed ReleaseUTF8Text(). The memory returned by
    // GetUTF8Text() is allocated with 'new[]', so we free it with
    // 'delete[]'. (In Tesseract 4.x, ReleaseUTF8Text() did the same
    // thing internally — it was just a wrapper around delete[].)
    if (text) delete[] text;
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
