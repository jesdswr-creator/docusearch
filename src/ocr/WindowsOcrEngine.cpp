// ============================================================
// WindowsOcrEngine.cpp - Delegates to RapidOcrEngine
// ============================================================
//
// This file now delegates all OCR operations to RapidOcrEngine
// (ONNX-based). The class name is kept as WindowsOcrEngine for
// backward compatibility with existing code.
// ============================================================

#include "WindowsOcrEngine.h"
#include "RapidOcrEngine.h"
#include "../core/Logger.h"

#include <QImage>
#include <QFileInfo>
#include <memory>

namespace DocuSearch {

// Static instance — created on first init() call, reused thereafter.
static std::unique_ptr<RapidOcrEngine> s_rapidOcr;

WindowsOcrEngine::WindowsOcrEngine() = default;
WindowsOcrEngine::~WindowsOcrEngine() = default;

bool WindowsOcrEngine::init() {
    if (initialized_) return true;

    if (!s_rapidOcr) {
        s_rapidOcr = std::make_unique<RapidOcrEngine>();
    }

    if (!s_rapidOcr->init()) {
        DS_WARN("OCR", "RapidOCR initialization failed. OCR unavailable.");
        initialized_ = false;
        return false;
    }

    initialized_ = true;
    DS_INFO("OCR", "RapidOCR engine initialized successfully");
    return true;
}

QString WindowsOcrEngine::ocrImage(const QImage& img) {
    if (!initialized_ || !s_rapidOcr) return {};
    return s_rapidOcr->ocrImage(img);
}

QString WindowsOcrEngine::ocrFile(const QString& path) {
    if (!initialized_ || !s_rapidOcr) return {};
    return s_rapidOcr->ocrFile(path);
}

QStringList WindowsOcrEngine::availableLanguages() {
    return RapidOcrEngine::availableLanguages();
}

} // namespace DocuSearch
