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

// RapidOcrCpp headers
#include "RapidOcr/OcrLiteAPI.h"
#include "RapidOcr/OcrStructAPI.h"

namespace DocuSearch {

WindowsOcrEngine::WindowsOcrEngine() = default;

WindowsOcrEngine::~WindowsOcrEngine() {
    if (ocrLite_) {
        delete static_cast<OcrLite*>(ocrLite_);
        ocrLite_ = nullptr;
    }
}

bool WindowsOcrEngine::init() {
    if (initialized_) return true;

    // Find the models directory.
    // Check: <appDir>/models, <exeDir>/models
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates = {
        appDir + "/models",
        appDir + "/../models",
        appDir + "/../share/DocuSearch/models",
    };

    for (const auto& dir : candidates) {
        // Check for the 3 required ONNX model files + keys file.
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

    // Create the OcrLite instance.
    auto* ocr = new OcrLite();
    ocr->setProvider("OnnxRuntime");
    ocr->setNumThread(2);  // Low thread count for low-end PCs
    ocr->initLogger(false, false, false);  // No logging for production

    // Load the models.
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
}

QString WindowsOcrEngine::ocrImage(const QImage& img) {
    if (!initialized_ || !ocrLite_) return {};

    auto* ocr = static_cast<OcrLite*>(ocrLite_);

    // Convert QImage to RGB888 format for RapidOCR.
    QImage rgbImg = img.convertToFormat(QImage::Format_RGB888);
    if (rgbImg.isNull()) return {};

    // Use detectBitmap for direct in-memory OCR (no temp file needed).
    OcrResult result = ocr->detectBitmap(
        rgbImg.bits(),
        rgbImg.width(),
        rgbImg.height(),
        3,  // channels (RGB)
        50,    // padding
        1024,  // maxSideLen (cap image size for speed)
        0.5f,  // boxScoreThresh
        0.3f,  // boxThresh
        1.6f,  // unClipRatio
        true,  // doAngle
        true   // mostAngle
    );

    // Extract text from all text blocks.
    QString text;
    for (const auto& block : result.textBlocks) {
        if (!block.text.empty()) {
            text.append(QString::fromUtf8(block.text.c_str())).append('\n');
        }
    }
    return text.trimmed();
}

QString WindowsOcrEngine::ocrFile(const QString& path) {
    if (!initialized_ || !ocrLite_) return {};
    if (!QFileInfo::exists(path)) return {};

    // Load the image via QImage, then OCR it.
    QImage img(path);
    if (img.isNull()) {
        DS_WARN("OCR", "Failed to load image: " + path);
        return {};
    }

    return ocrImage(img);
}

QStringList WindowsOcrEngine::availableLanguages() {
    // RapidOCR with PP-OCRv4 models supports these languages:
    return {"en", "zh", "chinese_sim", "chinese_tra", "korean", "japanese"};
}

} // namespace DocuSearch
