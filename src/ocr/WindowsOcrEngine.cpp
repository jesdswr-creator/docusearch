// ============================================================
// WindowsOcrEngine.cpp - RapidOcrCpp-based OCR (pure C++, no Python)
// ============================================================

#include "WindowsOcrEngine.h"
#include "../core/Logger.h"
#include "../core/Config.h"

#include <QImage>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QFile>

// RapidOcrCpp headers — wrapped in #ifdef so the app still builds
// even if RapidOcrCpp is not available (OCR will be disabled).
#ifdef DOCUSEARCH_HAS_RAPIDOCR
#include "OcrLiteAPI.h"
#include "OcrStructAPI.h"
#endif

namespace DocuSearch {

WindowsOcrEngine::WindowsOcrEngine() = default;

WindowsOcrEngine::~WindowsOcrEngine() {
#ifdef DOCUSEARCH_HAS_RAPIDOCR
    if (ocrLite_) {
        delete static_cast<OcrLite*>(ocrLite_);
        ocrLite_ = nullptr;
    }
#endif
}

bool WindowsOcrEngine::init() {
    if (initialized_) return true;

#ifndef DOCUSEARCH_HAS_RAPIDOCR
    DS_WARN("OCR", "RapidOCR not compiled in. OCR unavailable.");
    return false;
#else

    // Find the models directory.
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates = {
        appDir + "/models",
        appDir + "/../models",
        appDir + "/../share/DocuSearch/models",
    };

    for (const auto& dir : candidates) {
        if (QFileInfo::exists(dir + "/ch_PP-OCRv4_det_infer.onnx") &&
            QFileInfo::exists(dir + "/ch_ppocr_mobile_v2.0_cls_infer.onnx") &&
            QFileInfo::exists(dir + "/ch_PP-OCRv4_rec_infer.onnx") &&
            QFileInfo::exists(dir + "/ppocr_keys_v1.txt")) {
            modelsDir_ = dir;
            break;
        }
    }

    if (modelsDir_.isEmpty()) {
        DS_WARN("OCR", "OCR models not found. Expected in <appDir>/models/: "
                        "ch_PP-OCRv4_det_infer.onnx, ch_ppocr_mobile_v2.0_cls_infer.onnx, "
                        "ch_PP-OCRv4_rec_infer.onnx, ppocr_keys_v1.txt");
        return false;
    }

    auto* ocr = new OcrLite();
    ocr->setProvider("OnnxRuntime");
    ocr->setNumThread(2);
    ocr->initLogger(false, false, false);

    const std::string detPath  = (modelsDir_ + "/ch_PP-OCRv4_det_infer.onnx").toStdString();
    const std::string clsPath  = (modelsDir_ + "/ch_ppocr_mobile_v2.0_cls_infer.onnx").toStdString();
    const std::string recPath  = (modelsDir_ + "/ch_PP-OCRv4_rec_infer.onnx").toStdString();
    const std::string keysPath = (modelsDir_ + "/ppocr_keys_v1.txt").toStdString();

    if (!ocr->initModels(detPath, clsPath, recPath, keysPath)) {
        DS_WARN("OCR", "Failed to load OCR models from: " + modelsDir_);
        delete ocr;
        return false;
    }

    ocrLite_ = ocr;
    initialized_ = true;
    DS_INFO("OCR", "RapidOCR engine initialized (models: " + modelsDir_ + ")");
    return true;
#endif
}

QString WindowsOcrEngine::ocrImage(const QImage& img) {
#ifndef DOCUSEARCH_HAS_RAPIDOCR
    Q_UNUSED(img);
    return {};
#else
    if (!initialized_ || !ocrLite_) return {};

    auto* ocr = static_cast<OcrLite*>(ocrLite_);
    QImage rgbImg = img.convertToFormat(QImage::Format_RGB888);
    if (rgbImg.isNull()) return {};

    OcrResult result = ocr->detectBitmap(
        rgbImg.bits(),
        rgbImg.width(),
        rgbImg.height(),
        3,
        50, 1024,
        0.5f, 0.3f, 1.6f,
        true, true
    );

    QString text;
    for (const auto& block : result.textBlocks) {
        if (!block.text.empty()) {
            text.append(QString::fromUtf8(block.text.c_str())).append('\n');
        }
    }
    return text.trimmed();
#endif
}

QString WindowsOcrEngine::ocrFile(const QString& path) {
#ifndef DOCUSEARCH_HAS_RAPIDOCR
    Q_UNUSED(path);
    return {};
#else
    if (!initialized_ || !ocrLite_) return {};
    if (!QFileInfo::exists(path)) return {};

    QImage img(path);
    if (img.isNull()) {
        DS_WARN("OCR", "Failed to load image: " + path);
        return {};
    }
    return ocrImage(img);
#endif
}

QStringList WindowsOcrEngine::availableLanguages() {
    return {"en", "zh", "chinese_sim", "chinese_tra", "korean", "japanese"};
}

} // namespace DocuSearch
