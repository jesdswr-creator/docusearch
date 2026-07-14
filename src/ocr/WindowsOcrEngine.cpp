// ============================================================
// WindowsOcrEngine.cpp - RapidOCR via separate helper exe
// ============================================================
//
// OCR runs in a SEPARATE PROCESS (docusearch_ocr_helper.exe) that
// links RapidOCR. The main app calls it via QProcess — if the OCR
// engine crashes, only the helper crashes, not the main app.
//
// Usage: docusearch_ocr_helper.exe <image_path>
// Output: recognized text to stdout
// ============================================================

#include "WindowsOcrEngine.h"
#include "../core/Logger.h"

#include <QImage>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QDateTime>

namespace DocuSearch {

WindowsOcrEngine::WindowsOcrEngine() = default;
WindowsOcrEngine::~WindowsOcrEngine() = default;

bool WindowsOcrEngine::init() {
    // Check if the helper exe exists.
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString helper = appDir + "/docusearch_ocr_helper.exe";

    if (QFileInfo::exists(helper)) {
        // Check if models exist.
        const QString modelsDir = appDir + "/models";
        if (QFileInfo::exists(modelsDir + "/ch_PP-OCRv4_det_infer.onnx") &&
            QFileInfo::exists(modelsDir + "/ch_ppocr_mobile_v2.0_cls_infer.onnx") &&
            QFileInfo::exists(modelsDir + "/ch_PP-OCRv4_rec_infer.onnx") &&
            QFileInfo::exists(modelsDir + "/ppocr_keys_v1.txt")) {
            initialized_ = true;
            DS_INFO("OCR", "RapidOCR helper found: " + helper);
            return true;
        }
        DS_WARN("OCR", "Helper found but models missing in: " + modelsDir);
        return false;
    }

    DS_WARN("OCR", "OCR helper not found: " + helper);
    return false;
}

QString WindowsOcrEngine::ocrImage(const QImage& img) {
    if (!initialized_) return {};
    if (img.isNull()) return {};

    // Save to temp file, then call ocrFile.
    const QString tempPath = QDir::tempPath() + "/docusearch_ocr_" +
        QString::number(QDateTime::currentMSecsSinceEpoch()) + ".png";
    if (!img.save(tempPath, "PNG")) return {};
    const QString text = ocrFile(tempPath);
    QFile::remove(tempPath);
    return text;
}

QString WindowsOcrEngine::ocrFile(const QString& path) {
    if (!initialized_) return {};
    if (!QFileInfo::exists(path)) return {};

    // Run the OCR helper exe in a separate process.
    // If it crashes, the main app is completely unaffected.
    const QString helper = QCoreApplication::applicationDirPath() + "/docusearch_ocr_helper.exe";

    QProcess proc;
    proc.setProgram(helper);
    proc.setArguments({path});

    // Set working directory to app dir so the helper can find models/
    proc.setWorkingDirectory(QCoreApplication::applicationDirPath());

    proc.start();
    if (!proc.waitForStarted(5000)) {
        DS_WARN("OCR", "Failed to start OCR helper");
        return {};
    }
    // Allow up to 60 seconds for OCR.
    if (!proc.waitForFinished(60000)) {
        DS_WARN("OCR", "OCR helper timed out");
        proc.kill();
        return {};
    }

    if (proc.exitCode() != 0) {
        DS_WARN("OCR", "OCR helper exited with code " + QString::number(proc.exitCode()));
        return {};
    }

    return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
}

QStringList WindowsOcrEngine::availableLanguages() {
    return {"en", "zh", "chinese_sim", "chinese_tra", "korean", "japanese"};
}

} // namespace DocuSearch
