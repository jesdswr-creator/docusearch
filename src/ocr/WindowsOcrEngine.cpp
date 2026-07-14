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
#include <QDateTime>

#ifdef DOCUSEARCH_HAS_RAPIDOCR
#include "Core/OcrLite.h"
#include "Core/OcrResult.h"
#include "Core/OcrStruct.h"
#endif

namespace DocuSearch {

WindowsOcrEngine::WindowsOcrEngine() = default;

WindowsOcrEngine::~WindowsOcrEngine() {
#ifdef DOCUSEARCH_HAS_RAPIDOCR
    if (ocrLite_) {
        try { delete static_cast<OcrLite*>(ocrLite_); } catch (...) {}
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
        DS_WARN("OCR", "OCR models not found in: " + appDir + "/models/");
        return false;
    }

    try {
        auto* ocr = new OcrLite();
        ocr->setProvider("OnnxRuntime");
        ocr->setNumThread(2);
        ocr->initLogger(false, false, false);

        const std::string detPath  = (modelsDir_ + "/ch_PP-OCRv4_det_infer.onnx").toStdString();
        const std::string clsPath  = (modelsDir_ + "/ch_ppocr_mobile_v2.0_cls_infer.onnx").toStdString();
        const std::string recPath  = (modelsDir_ + "/ch_PP-OCRv4_rec_infer.onnx").toStdString();
        const std::string keysPath = (modelsDir_ + "/ppocr_keys_v1.txt").toStdString();

        if (!ocr->initModels(detPath, clsPath, recPath, keysPath)) {
            DS_WARN("OCR", "Failed to load OCR models");
            delete ocr;
            return false;
        }

        ocrLite_ = ocr;
        initialized_ = true;
        DS_INFO("OCR", "RapidOCR initialized");
        return true;
    } catch (const std::exception& e) {
        DS_WARN("OCR", QString("OCR init exception: %1").arg(e.what()));
        return false;
    } catch (...) {
        DS_WARN("OCR", "OCR init unknown exception");
        return false;
    }
#endif
}

QString WindowsOcrEngine::ocrImage(const QImage& img) {
#ifndef DOCUSEARCH_HAS_RAPIDOCR
    Q_UNUSED(img);
    return {};
#else
    if (!initialized_ || !ocrLite_) return {};
    if (img.isNull()) return {};

    try {
        auto* ocr = static_cast<OcrLite*>(ocrLite_);

        // Convert to RGB888 — RapidOCR expects 3-channel BGR via OpenCV.
        QImage rgbImg = img.convertToFormat(QImage::Format_RGB888);
        if (rgbImg.isNull()) return {};

        // Use detect() with a temp file instead of detectBitmap() —
        // detectBitmap() can crash if the image data alignment doesn't
        // match what OpenCV expects. Using a file is safer.
        QString tempPath = QDir::tempPath() + "/docusearch_ocr_" +
            QString::number(QDateTime::currentMSecsSinceEpoch()) + ".png";
        if (!rgbImg.save(tempPath, "PNG")) return {};

        QByteArray pathBytes = tempPath.toUtf8();
        QByteArray nameBytes = QFileInfo(tempPath).fileName().toUtf8();

        OcrResult result = ocr->detect(
            pathBytes.constData(),
            nameBytes.constData(),
            50,     // padding
            1024,   // maxSideLen
            0.5f,   // boxScoreThresh
            0.3f,   // boxThresh
            1.6f,   // unClipRatio
            true,   // doAngle
            true    // mostAngle
        );

        QFile::remove(tempPath);

        QString text;
        for (const auto& block : result.textBlocks) {
            if (!block.text.empty()) {
                text.append(QString::fromUtf8(block.text.c_str())).append('\n');
            }
        }
        return text.trimmed();
    } catch (const std::exception& e) {
        DS_WARN("OCR", QString("OCR exception: %1").arg(e.what()));
        return {};
    } catch (...) {
        DS_WARN("OCR", "OCR unknown exception");
        return {};
    }
#endif
}

QString WindowsOcrEngine::ocrFile(const QString& path) {
    if (!initialized_ || !ocrLite_) return {};
    if (!QFileInfo::exists(path)) return {};

    QImage img(path);
    if (img.isNull()) {
        DS_WARN("OCR", "Failed to load image: " + path);
        return {};
    }
    return ocrImage(img);
}

QStringList WindowsOcrEngine::availableLanguages() {
    return {"en", "zh", "chinese_sim", "chinese_tra", "korean", "japanese"};
}

} // namespace DocuSearch
