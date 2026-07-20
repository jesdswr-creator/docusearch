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

WindowsOcrEngine& WindowsOcrEngine::instance() {
    static WindowsOcrEngine inst;
    static bool initialized = false;
    if (!initialized) {
        inst.init();
        initialized = true;
    }
    return inst;
}

// ── Check if oneocr.dll is available next to the app ────────
// Searches the same directories the helper exe searches:
//   <appDir>/oneocr.dll
//   <appDir>/oneocr/oneocr.dll
//   <appDir>/models/oneocr/oneocr.dll
//   %USERPROFILE%/.config/oneocr/oneocr.dll
//
// NOTE: These paths MUST match FindOneocrDir() in ocr_helper_main.cpp
// exactly. If they diverge, the status bar will show "Setup Required"
// even though the helper can still find the DLL.
QString WindowsOcrEngine::findOneocrDir() const {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir,
        appDir + "/oneocr",
        appDir + "/models/oneocr",
    };

    DS_INFO("OCR", "Searching for oneocr.dll...");
    DS_INFO("OCR", "  appDir = " + appDir);

    for (const QString& dir : candidates) {
        const QString dllPath = dir + "/oneocr.dll";
        const bool exists = QFileInfo::exists(dllPath);
        DS_INFO("OCR", "  checking " + dllPath + " -> " + (exists ? "FOUND" : "missing"));
        if (exists) {
            return dir;
        }
    }

    // Fallback: %USERPROFILE%/.config/oneocr/ (matches oneocr.py default).
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    if (!home.isEmpty()) {
        const QString cfgDir = home + "/.config/oneocr";
        const QString dllPath = cfgDir + "/oneocr.dll";
        const bool exists = QFileInfo::exists(dllPath);
        DS_INFO("OCR", "  checking " + dllPath + " -> " + (exists ? "FOUND" : "missing"));
        if (exists) {
            return cfgDir;
        }
    }

    DS_WARN("OCR", "  oneocr.dll NOT FOUND in any search path.");
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
        DS_WARN("OCR", "oneocr.dll not found. OCR will be unavailable.");
        DS_WARN("OCR", "Run scripts/get_oneocr.ps1 to install oneocr files.");
        oneocrAvailable_ = false;
    } else {
        DS_INFO("OCR", "oneocr.dll found at: " + oneocrDir);
        // Verify model file is also present.
        const QString modelPath = oneocrDir + "/oneocr.onemodel";
        if (QFileInfo::exists(modelPath)) {
            oneocrAvailable_ = true;
            DS_INFO("OCR", "oneocr.onemodel also present. OCR is fully ready.");
        } else {
            DS_WARN("OCR", "oneocr.dll found but oneocr.onemodel is missing at: " + modelPath);
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
        // If the helper complains that oneocr files are missing, mark
        // oneocr as unavailable. This handles the case where the user
        // installed DocuSearch but hasn't run get_oneocr.ps1 yet.
        if (err.contains("oneocr.dll not found", Qt::CaseInsensitive) ||
            err.contains("oneocr.onemodel not found", Qt::CaseInsensitive)) {
            oneocrAvailable_ = false;
        }
        // The helper prints "[oneocr] ..." info messages to stderr too.
        // Only log as warning if it actually looks like an error.
        if (err.contains("ERROR", Qt::CaseInsensitive) ||
            err.contains("not found", Qt::CaseInsensitive)) {
            DS_WARN("OCR", "Helper stderr: " + err);
        } else {
            DS_INFO("OCR", "Helper stderr: " + err);
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

    // If we got back any non-error text, oneocr is definitely working.
    // This is the most reliable signal — it means the helper actually
    // loaded oneocr.dll, loaded the model, and ran OCR successfully.
    // We update the flag so the next call to isOneocrAvailable()
    // (e.g. from the status bar refresh) returns true.
    const QString trimmed = text.trimmed();
    if (!trimmed.isEmpty() && !trimmed.startsWith('[')) {
        if (!oneocrAvailable_) {
            DS_INFO("OCR", "OCR succeeded — marking oneocr as available.");
        }
        oneocrAvailable_ = true;
    }

    return trimmed;
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
