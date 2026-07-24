// ============================================================
// WinRtOcrEngine.cpp - Unlimited OCR via Windows.Media.Ocr helper exe
// ============================================================

#include "WinRtOcrEngine.h"
#include "../core/Logger.h"

#include <QImage>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QDateTime>
#include <QStringList>
#include <QStandardPaths>

namespace DocuSearch {

WinRtOcrEngine::WinRtOcrEngine() = default;
WinRtOcrEngine::~WinRtOcrEngine() = default;

WinRtOcrEngine& WinRtOcrEngine::instance() {
    static WinRtOcrEngine inst;
    static bool initialized = false;
    if (!initialized) {
        inst.init();
        initialized = true;
    }
    return inst;
}

bool WinRtOcrEngine::init() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString helper = appDir + "/docusearch_winrt_ocr_helper.exe";

    if (!QFileInfo::exists(helper)) {
        DS_WARN("OCR", "WinRT OCR helper exe not found: " + helper);
        initialized_ = false;
        helperPresent_ = false;
        return false;
    }

    helperPresent_ = true;
    initialized_   = true;
    DS_INFO("OCR", "WinRT OCR helper present: " + helper);
    return true;
}

void WinRtOcrEngine::setLanguageOverride(const QString& langTag) {
    langOverride_ = langTag;
}

QString WinRtOcrEngine::ocrImage(const QImage& img) {
    if (!initialized_ || !helperPresent_) return {};
    if (img.isNull()) return {};

    const QString tempPath = QDir::tempPath() + "/docusearch_winrt_ocr_" +
        QString::number(QDateTime::currentMSecsSinceEpoch()) + ".png";
    if (!img.save(tempPath, "PNG")) return {};
    const QString text = ocrFile(tempPath);
    QFile::remove(tempPath);
    return text;
}

QString WinRtOcrEngine::ocrFile(const QString& path) {
    if (!initialized_ || !helperPresent_) return {};
    if (!QFileInfo::exists(path)) return {};

    const QString helper = QCoreApplication::applicationDirPath()
                         + "/docusearch_winrt_ocr_helper.exe";

    QProcess proc;
    proc.setProgram(helper);
    proc.setArguments({path});
    proc.setWorkingDirectory(QCoreApplication::applicationDirPath());

    // Pass language override via environment.
    if (!langOverride_.isEmpty()) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("DOCUSEARCH_OCR_LANG", langOverride_);
        proc.setProcessEnvironment(env);
    }

    proc.start();
    if (!proc.waitForStarted(5000)) {
        DS_WARN("OCR", "Failed to start WinRT OCR helper");
        return {};
    }
    if (!proc.waitForFinished(120000)) {  // 2 min timeout
        DS_WARN("OCR", "WinRT OCR helper timed out — killing");
        proc.kill();
        proc.waitForFinished(3000);
        return {};
    }

    const QByteArray stderrBytes = proc.readAllStandardError();
    if (!stderrBytes.isEmpty()) {
        const QString err = QString::fromUtf8(stderrBytes).trimmed();
        if (err.contains("error", Qt::CaseInsensitive) ||
            err.contains("not available", Qt::CaseInsensitive)) {
            DS_WARN("OCR", "WinRT helper stderr: " + err);
        } else {
            DS_INFO("OCR", "WinRT helper stderr: " + err);
        }
    }

    if (proc.exitCode() != 0) {
        DS_WARN("OCR", "WinRT OCR helper exited with code "
                 + QString::number(proc.exitCode()));
    }

    // Parse output: ===FILE===<path>\n<text>\n===END=== (same format as oneocr).
    const QString output = QString::fromUtf8(proc.readAllStandardOutput());
    const QStringList lines = output.split('\n', Qt::KeepEmptyParts);

    QString text;
    bool inFile = false;
    for (const QString& line : lines) {
        if (line.startsWith("===FILE===")) { inFile = true; continue; }
        if (line.startsWith("===END==="))  { inFile = false; continue; }
        if (inFile) text += line + "\n";
    }

    const QString trimmed = text.trimmed();
    if (!trimmed.isEmpty()) {
        if (!functional_) {
            DS_INFO("OCR", "WinRT OCR succeeded — marking engine as functional.");
        }
        functional_ = true;
    }
    return trimmed;
}

QStringList WinRtOcrEngine::availableLanguages() {
    // Windows.Media.Ocr engine reports installed OCR languages. We could
    // shell out to the helper to query, but for the status bar UI it's
    // enough to just say "auto" + the Windows user profile languages.
    // Real language list is queried by SettingsDialog directly via WinRT
    // if/when needed (deferred — not critical for the unlimited OCR goal).
    return {"auto"};
}

} // namespace DocuSearch
