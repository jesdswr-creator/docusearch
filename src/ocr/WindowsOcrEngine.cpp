// ============================================================
// WindowsOcrEngine.cpp - Windows.Media.Ocr wrapper via helper exe
// ============================================================
//
// Wraps the docusearch_ocr_helper.exe subprocess. The helper uses
// C++/WinRT to call Windows.Media.Ocr.OcrEngine.RecognizeAsync —
// the same OCR engine that powers Windows Search, Snipping Tool,
// and the Photos app.
//
// Availability model:
//   Windows.Media.Ocr is part of the Windows 10+ runtime. The only
//   file we ship is the helper exe. No DLLs, no model files, no
//   install scripts.
//
// Class name WindowsOcrEngine is kept for backward compatibility
// with existing code that references it.
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
#include <atomic>

namespace DocuSearch {

WindowsOcrEngine::WindowsOcrEngine() = default;
WindowsOcrEngine::~WindowsOcrEngine() = default;

WindowsOcrEngine& WindowsOcrEngine::instance() {
    static WindowsOcrEngine inst;
    static bool initialized = false;
    if (!initialized) {
        inst.init();
        initialized = true;
    }
    return inst;
}

bool WindowsOcrEngine::init() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString helper = appDir + "/docusearch_ocr_helper.exe";

    if (!QFileInfo::exists(helper)) {
        DS_WARN("OCR", "OCR helper exe not found: " + helper);
        initialized_ = false;
        return false;
    }

    // Windows.Media.Ocr is always available on Windows 10 1809+.
    // We optimistically mark available_=true so the status bar shows
    // "Ready" until/unless a real OCR call surfaces a "no language
    // packs" message from the helper.
    available_ = true;
    noLanguagePacks_ = false;
    initialized_ = true;
    DS_INFO("OCR", "Windows.Media.Ocr helper present — OCR available.");
    return true;
}

QString WindowsOcrEngine::ocrImage(const QImage& img) {
    if (!initialized_) return {};
    if (img.isNull()) return {};

    // Use native separators so the path we hand to the WinRT helper is
    // accepted by StorageFile::GetFileFromPathAsync — WinRT rejects
    // forward slashes with the misleading "path contains invalid
    // characters" error.
    //
    // v1.7.10: the name used to be timestamp-only. Two OCR workers
    // calling within the same millisecond generated the SAME temp file
    // — one worker's helper read a half-written/clobbered PNG and
    // returned empty text. A process-wide counter makes collisions
    // impossible.
    static std::atomic<quint64> seq{0};
    const QString tempPath = QDir::toNativeSeparators(
        QDir::tempPath() + "/docusearch_ocr_" +
        QString::number(QDateTime::currentMSecsSinceEpoch()) + "_" +
        QString::number(seq.fetch_add(1) + 1) + ".png");
    if (!img.save(tempPath, "PNG")) return {};
    const QString text = ocrFile(tempPath);
    QFile::remove(tempPath);
    return text;
}

QString WindowsOcrEngine::ocrFile(const QString& path) {
    if (!initialized_) return {};
    if (!QFileInfo::exists(path)) return {};

    // Normalize to native Windows separators. The OCR helper exe calls
    // WinRT's StorageFile::GetFileFromPathAsync which throws
    // IllegalArgumentException ("The path contains one or more invalid
    // characters") when the path uses forward slashes — even though
    // CreateFileW and most Win32 APIs accept them. This is the fix
    // for the bug reported as:
    //   "The specified path (C:/Users/.../Temp/docusearch_ocr_page_0.png)
    //    contains one or more invalid characters."
    const QString nativePath = QDir::toNativeSeparators(path);

    const QString helper = QCoreApplication::applicationDirPath() + "/docusearch_ocr_helper.exe";

    QProcess proc;
    proc.setProgram(helper);
    proc.setArguments({nativePath});
    proc.setWorkingDirectory(QCoreApplication::applicationDirPath());

    proc.start();
    if (!proc.waitForStarted(5000)) {
        DS_WARN("OCR", "Failed to start OCR helper");
        return {};
    }
    if (!proc.waitForFinished(120000)) {  // 2 min timeout
        DS_WARN("OCR", "OCR helper timed out — killing");
        proc.kill();
        proc.waitForFinished(3000);
        return {};
    }

    // Capture stderr so we can surface setup errors in the log.
    const QByteArray stderrBytes = proc.readAllStandardError();
    if (!stderrBytes.isEmpty()) {
        const QString err = QString::fromUtf8(stderrBytes).trimmed();
        // The helper prints "[OCR] ..." info messages to stderr. Only
        // log as warning if it actually looks like an error.
        if (err.contains("ERROR", Qt::CaseInsensitive) ||
            err.contains("not found", Qt::CaseInsensitive)) {
            DS_WARN("OCR", "Helper stderr: " + err);
        } else {
            DS_INFO("OCR", "Helper stderr: " + err);
        }

        // Surface the "no OCR language packs" condition to the status bar.
        if (err.contains("No OCR language", Qt::CaseInsensitive)) {
            noLanguagePacks_ = true;
            available_ = false;
        }
    }

    if (proc.exitCode() != 0) {
        DS_WARN("OCR", "OCR helper exited with code " + QString::number(proc.exitCode()));
    }

    // Parse output: ===FILE===<path>\n<text>\n===END===
    const QString output = QString::fromUtf8(proc.readAllStandardOutput());
    const QStringList lines = output.split('\n', Qt::KeepEmptyParts);

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

    // If we got back non-error text, OCR is definitely working. Update
    // the cached available_ flag so the next status-bar refresh shows
    // "Ready" instead of "Setup Required".
    const QString trimmed = text.trimmed();
    if (!trimmed.isEmpty() && !trimmed.startsWith('[')) {
        if (!available_) {
            DS_INFO("OCR", "OCR succeeded — marking Windows.Media.Ocr as available.");
        }
        available_ = true;
        noLanguagePacks_ = false;
    }

    return trimmed;
}

QStringList WindowsOcrEngine::availableLanguages() {
    // Windows.Media.Ocr exposes the installed OCR language packs via
    // OcrEngine::AvailableRecognizerLanguages(). We don't want to spawn
    // the helper just to list languages, so we return the common set
    // here. For an exact list, the Settings dialog calls this and shows
    // "Multi-language auto-detect" — the helper picks the user's
    // profile languages at startup.
    return {"auto", "en", "en-US", "en-GB", "zh", "zh-Hans-CN",
            "zh-Hant-TW", "ja", "ko", "de", "fr", "es", "it", "pt",
            "ru", "ar", "hi"};
}

bool WindowsOcrEngine::isAvailable() const {
    return available_ && !noLanguagePacks_;
}

} // namespace DocuSearch
