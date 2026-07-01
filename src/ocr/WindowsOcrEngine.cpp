// ============================================================
// WindowsOcrEngine.cpp - Stub implementation (OCR disabled)
// ============================================================
// Windows OCR is temporarily disabled. The WinRT ABI headers
// auto-link runtimeobject.lib which conflicts with Qt's WIN32
// entry point. Will be re-enabled via a separate DLL plugin.
// ============================================================

#include "WindowsOcrEngine.h"
#include "../core/Logger.h"

#include <QImage>
#include <QFileInfo>

namespace DocuSearch {

WindowsOcrEngine::WindowsOcrEngine() = default;
WindowsOcrEngine::~WindowsOcrEngine() = default;

bool WindowsOcrEngine::init() {
    DS_WARN("WinOCR", "Windows OCR is not available in this build");
    return false;
}

QString WindowsOcrEngine::ocrImage(const QImage& img) {
    Q_UNUSED(img);
    return {};
}

QString WindowsOcrEngine::ocrFile(const QString& path) {
    Q_UNUSED(path);
    return {};
}

QStringList WindowsOcrEngine::availableLanguages() {
    return {};
}

} // namespace DocuSearch
