// ============================================================
// WindowsOcrEngine.cpp - oneocr.dll-based OCR via helper exe
// ============================================================
//
// Wraps the docusearch_ocr_helper.exe subprocess, which loads
// oneocr.dll (from the Windows 11 Snipping Tool) and calls its
// C-ABI exports. This is a drop-in replacement for the previous
// WinRT-based implementation that was crashing the app.
//
// The class name WindowsOcrEngine is kept for backward compatibility
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
#include <QStandardPaths>

namespace DocuSearch {

WindowsOcrEngine::WindowsOcrEngine() = default;
WindowsOcrEngine::~WindowsOcrEngine() = default;

// ── Check if oneocr.dll is available next to the app ────────
// Searches the same directories the helper exe searches:
//   <appDir>/oneocr.dll
//   <appDir>/oneocr/oneocr.dll
//   <appDir>/models/oneocr/oneocr.dll
//   %USERPROFILE%/.config/oneocr/oneocr.dll
QString WindowsOcrEngine::findOneocrDir() const {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir,
        appDir + "/oneocr",
        appDir + "/models/oneocr",
    };
    for (const QString& dir : candidates) {
        if (QFileInfo::exists(dir + "/oneocr.dll")) {
            return dir;
        }
    }
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    if (!home.isEmpty()) {
        const QString cfgDir = home + "/.config/oneocr";
        if (QFileInfo::exists(cfgDir + "/oneocr.dll")) {
            return cfgDir;
        }
    }
    return {};
}

bool WindowsOcrEngine::init() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString helper = appDir + "/docusearch_ocr_helper.exe";

    if (!QFileInfo::exists(helper)) {
        DS_WARN("OCR", "OCR helper exe not found: " + helper);
        initialized_ = false;
        return false;
    }

    // Check for oneocr.dll — if missing, OCR will fail at runtime.
    // We still mark initialized_=true so ocrFile() can run and surface
    // the helpful error message printed by the helper itself.
    const QString oneocrDir = findOneocrDir();
    if (oneocrDir.isEmpty()) {
        DS_WARN("OCR", "oneocr.dll not found. OCR will be disabled.");
        DS_WARN("OCR", "Run scripts/get_oneocr.ps1 to install oneocr files.");
        oneocrAvailable_ = false;
    } else {
        DS_INFO("OCR", "oneocr.dll found at: " + oneocrDir);
        // Verify model file is also present.
        if (QFileInfo::exists(oneocrDir + "/oneocr.onemodel")) {
            oneocrAvailable_ = true;
        } else {
            DS_WARN("OCR", "oneocr.dll found but oneocr.onemodel is missing.");
            oneocrAvailable_ = false;
        }
    }

    initialized_ = true;
    return true;
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
    if (!proc.waitForFinished(120000)) {  // 2 min timeout
        DS_WARN("OCR", "OCR helper timed out — killing");
        proc.kill();
        proc.waitForFinished(3000);
        return {};
    }

    // Capture stderr too so we can surface setup errors in the log.
    const QByteArray stderrBytes = proc.readAllStandardError();
    if (!stderrBytes.isEmpty()) {
        const QString err = QString::fromUtf8(stderrBytes).trimmed();
        if (err.contains("oneocr.dll not found", Qt::CaseInsensitive) ||
            err.contains("not found", Qt::CaseInsensitive)) {
            oneocrAvailable_ = false;
        }
        DS_WARN("OCR", "Helper stderr: " + err);
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

    return text.trimmed();
}

QStringList WindowsOcrEngine::availableLanguages() {
    // oneocr auto-detects based on the model file; the Snipping Tool model
    // supports English, Chinese (Simplified/Traditional), Korean, Japanese.
    return {"en", "zh", "chinese_sim", "chinese_tra", "korean", "japanese"};
}

bool WindowsOcrEngine::isOneocrAvailable() const {
    return oneocrAvailable_;
}

} // namespace DocuSearch
