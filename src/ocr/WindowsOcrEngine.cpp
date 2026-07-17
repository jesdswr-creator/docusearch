// ============================================================
// WindowsOcrEngine.cpp - Windows OCR via helper exe
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
#include <QStringList>

namespace DocuSearch {

WindowsOcrEngine::WindowsOcrEngine() = default;
WindowsOcrEngine::~WindowsOcrEngine() = default;

bool WindowsOcrEngine::init() {
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
    if (!proc.waitForFinished(120000)) {  // 2 min timeout (allows for large images)
        DS_WARN("OCR", "OCR helper timed out");
        proc.kill();
        return {};
    }

    if (proc.exitCode() != 0) {
        DS_WARN("OCR", "OCR helper exited with code " + QString::number(proc.exitCode()));
    }

    // Parse output: ===FILE===<path>\n<text>\n===END===
    QString output = QString::fromUtf8(proc.readAllStandardOutput());
    QStringList lines = output.split('\n', Qt::KeepEmptyParts);

    QString text;
    bool inFile = false;
    for (const QString& line : lines) {
        if (line.startsWith("===FILE===")) {
            inFile = true;
            continue;
        }
        if (line.startsWith("===END===")) {
            inFile = false;
            continue;
        }
        if (inFile) {
            text += line + "\n";
        }
    }

    return text.trimmed();
}

QStringList WindowsOcrEngine::availableLanguages() {
    return {"en", "zh", "chinese_sim", "chinese_tra", "korean", "japanese"};
}

} // namespace DocuSearch
