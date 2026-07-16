// ============================================================
// WindowsOcrEngine.cpp - Windows OCR via helper exe (same as PowerToys)
// ============================================================
//
// Uses Windows' built-in OCR (Windows.Media.Ocr) via a separate
// helper exe. Same approach as PowerToys Text Extractor.
//
// No Python, no OpenCV, no ONNX Runtime, no model files.
// Just Windows 10 v1903+ or Windows 11.
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
        initialized_ = true;
        DS_INFO("OCR", "Windows OCR helper found: " + helper);
        return true;
    }

    DS_WARN("OCR", "OCR helper not found: " + helper);
    return false;
}

QString WindowsOcrEngine::ocrImage(const QImage& img) {
    if (!initialized_) return {};
    if (img.isNull()) return {};

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

    const QString helper = QCoreApplication::applicationDirPath() + "/docusearch_ocr_helper.exe";

    QProcess proc;
    proc.setProgram(helper);
    proc.setArguments({path});
    proc.setWorkingDirectory(QCoreApplication::applicationDirPath());

    proc.start();
    if (!proc.waitForStarted(5000)) {
        DS_WARN("OCR", "Failed to start OCR helper");
        return {};
    }
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
