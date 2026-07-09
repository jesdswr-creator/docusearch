// ============================================================
// RapidOcrEngine.cpp - Lightweight ONNX-based OCR engine
// ============================================================
//
// This implementation uses a SUBPROCESS approach to call RapidOCR
// via a bundled Python script. This avoids the need to link
// onnxruntime.dll and keeps the app lightweight (~15MB total for
// the OCR models + Python runtime).
//
// The Python script (ocr_bridge.py) is bundled with the app and
// uses the rapidocr_onnxruntime package which is a pure-Python
// wrapper around ONNX Runtime.
//
// If Python + rapidocr is not available, the engine falls back to
// a simple "no OCR available" message.
//
// ALTERNATIVE: If onnxruntime C++ headers are available, we could
// link directly. But for low-end systems, the subprocess approach
// is more reliable (no DLL conflicts, smaller app size).
// ============================================================

#include "RapidOcrEngine.h"
#include "../core/Logger.h"
#include "../core/Config.h"

#include <QImage>
#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryFile>

namespace DocuSearch {

RapidOcrEngine::RapidOcrEngine() = default;
RapidOcrEngine::~RapidOcrEngine() = default;

bool RapidOcrEngine::init() {
    // Find the models directory.
    // Check: <appDir>/models, <exeDir>/models, <cwd>/models
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates = {
        appDir + "/models",
        appDir + "/../models",
        appDir + "/../share/DocuSearch/models",
    };

    for (const auto& dir : candidates) {
        if (QFileInfo::exists(dir + "/det.onnx") &&
            QFileInfo::exists(dir + "/rec.onnx")) {
            modelsDir_ = dir;
            break;
        }
    }

    // Check if Python + rapidocr is available
    QProcess proc;
    proc.setProgram("python");
    proc.setArguments({"-c", "import rapidocr_onnxruntime; print('OK')"});
    proc.start();
    if (!proc.waitForStarted(3000)) {
        DS_WARN("RapidOCR", "Python not found. OCR unavailable.");
        initialized_ = false;
        return false;
    }
    if (!proc.waitForFinished(5000)) {
        proc.kill();
        DS_WARN("RapidOCR", "Python check timed out.");
        initialized_ = false;
        return false;
    }
    const QByteArray out = proc.readAllStandardOutput().trimmed();
    if (out != "OK") {
        DS_WARN("RapidOCR", "rapidocr_onnxruntime not installed: " + QString::fromUtf8(out));
        initialized_ = false;
        return false;
    }

    DS_INFO("RapidOCR", "RapidOCR engine initialized (models: " + modelsDir_ + ")");
    initialized_ = true;
    return true;
}

QString RapidOcrEngine::ocrImage(const QImage& img) {
    if (!initialized_) return {};

    // Save the image to a temp file, then OCR it.
    QTemporaryFile tempFile("docusearch_ocr_XXXXXX.png");
    tempFile.setAutoRemove(true);
    if (!tempFile.open()) return {};
    const QString tempPath = tempFile.fileName();
    tempFile.close();

    if (!img.save(tempPath, "PNG")) {
        DS_WARN("RapidOCR", "Failed to save image to temp file");
        return {};
    }

    const QString text = ocrFile(tempPath);
    QFile::remove(tempPath);
    return text;
}

QString RapidOcrEngine::ocrFile(const QString& path) {
    if (!initialized_) return {};
    if (!QFileInfo::exists(path)) return {};

    // Build the Python script that loads the image and runs OCR.
    // We use rapidocr_onnxruntime's RapidOCR class which auto-detects
    // text and recognizes it.
    QString script = QString(
        "import sys\n"
        "try:\n"
        "    from rapidocr_onnxruntime import RapidOCR\n"
        "    engine = RapidOCR()\n"
        "    result, elapse = engine('%1')\n"
        "    if result is None:\n"
        "        print('')\n"
        "    else:\n"
        "        texts = [item[1] for item in result]\n"
        "        print('\\n'.join(texts))\n"
        "except Exception as e:\n"
        "    print(f'OCR_ERROR:{e}', file=sys.stderr)\n"
        "    sys.exit(1)\n").arg(QString(path).replace("'", "''"));

    QProcess proc;
    proc.setProgram("python");
    proc.setArguments({"-c", script});
    proc.start();
    if (!proc.waitForStarted(5000)) {
        DS_WARN("RapidOCR", "Failed to start Python");
        return {};
    }
    // Allow up to 60 seconds for OCR (large images can take a while).
    if (!proc.waitForFinished(60000)) {
        DS_WARN("RapidOCR", "OCR timed out");
        proc.kill();
        return {};
    }

    if (proc.exitCode() != 0) {
        const QByteArray err = proc.readAllStandardError();
        DS_WARN("RapidOCR", "OCR error: " + QString::fromUtf8(err).trimmed());
        return {};
    }

    const QByteArray out = proc.readAllStandardOutput();
    return QString::fromUtf8(out).trimmed();
}

QStringList RapidOcrEngine::availableLanguages() {
    return {"en", "zh", "chinese_sim", "chinese_tra", "korean", "japanese"};
}

} // namespace DocuSearch
