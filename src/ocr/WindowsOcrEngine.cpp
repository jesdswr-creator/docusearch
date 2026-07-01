// ============================================================
// WindowsOcrEngine.cpp - Windows.Media.Ocr via WinRT/C++
// ============================================================

#include "WindowsOcrEngine.h"
#include "../core/Logger.h"

#include <QImage>
#include <QFileInfo>

#ifdef DOCUSEARCH_HAS_WINDOWS_OCR

// WinRT headers — part of the Windows SDK (no vcpkg needed).
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>

#include <vector>
#include <cstring>

namespace winrt {
    using namespace Windows::Foundation;
    using namespace Windows::Media::Ocr;
    using namespace Windows::Graphics::Imaging;
    using namespace Windows::Storage::Streams;
}

#endif // DOCUSEARCH_HAS_WINDOWS_OCR

namespace DocuSearch {

WindowsOcrEngine::WindowsOcrEngine() = default;
WindowsOcrEngine::~WindowsOcrEngine() = default;

bool WindowsOcrEngine::init() {
#ifdef DOCUSEARCH_HAS_WINDOWS_OCR
    if (initialized_) return true;
    try {
        winrt::init_apartment();
        auto engine = winrt::OcrEngine::TryCreateFromUserProfileLanguages();
        if (engine) {
            initialized_ = true;
            DS_INFO("WinOCR", "Windows OCR engine initialized");
            return true;
        }
        DS_WARN("WinOCR", "No OCR languages available");
        return false;
    } catch (const winrt::hresult_error& e) {
        DS_ERROR("WinOCR", QString("WinRT init failed: %1")
                  .arg(QString::fromWCharArray(e.message().c_str())));
        return false;
    } catch (...) {
        DS_ERROR("WinOCR", "Unknown WinRT init error");
        return false;
    }
#else
    DS_WARN("WinOCR", "Built without Windows OCR support");
    return false;
#endif
}

QString WindowsOcrEngine::ocrImage(const QImage& img) {
#ifdef DOCUSEARCH_HAS_WINDOWS_OCR
    if (!initialized_ && !init()) return {};
    if (img.isNull()) return {};

    try {
        // Convert QImage to BGRA format.
        QImage conv = img.convertToFormat(QImage::Format_RGBA8888);
        if (conv.isNull()) return {};

        const int w = conv.width();
        const int h = conv.height();
        std::vector<uint8_t> bgra(w * h * 4);
        const uint8_t* src = conv.bits();
        for (int i = 0; i < w * h; ++i) {
            bgra[i*4+0] = src[i*4+2]; // B
            bgra[i*4+1] = src[i*4+1]; // G
            bgra[i*4+2] = src[i*4+0]; // R
            bgra[i*4+3] = src[i*4+3]; // A
        }

        // Create SoftwareBitmap and copy pixels.
        auto sb = winrt::SoftwareBitmap(winrt::BitmapPixelFormat::Bgra8, w, h,
                                        winrt::BitmapAlphaMode::Premultiplied);
        {
            auto buf = sb.LockBuffer(winrt::BitmapBufferAccessMode::Write);
            auto ref = buf.CreateReference();
            auto byteAccess = ref.as<Windows::Foundation::IMemoryBufferByteAccess>();
            uint8_t* dst = nullptr;
            uint32_t cap = 0;
            byteAccess->GetBuffer(&dst, &cap);
            if (dst && cap >= static_cast<uint32_t>(bgra.size())) {
                std::memcpy(dst, bgra.data(), bgra.size());
            }
        }

        // Run OCR.
        auto engine = winrt::OcrEngine::TryCreateFromUserProfileLanguages();
        if (!engine) return {};

        auto result = engine.RecognizeAsync(sb).get();
        auto text = result.Text();
        return QString::fromWCharArray(text.c_str());
    } catch (const winrt::hresult_error& e) {
        DS_WARN("WinOCR", QString("OCR failed: %1")
                 .arg(QString::fromWCharArray(e.message().c_str())));
        return {};
    } catch (...) {
        DS_WARN("WinOCR", "OCR failed (unknown)");
        return {};
    }
#else
    Q_UNUSED(img);
    return {};
#endif
}

QString WindowsOcrEngine::ocrFile(const QString& path) {
    if (!QFileInfo::exists(path)) return {};
    QImage img(path);
    if (img.isNull()) return {};
    return ocrImage(img);
}

QStringList WindowsOcrEngine::availableLanguages() {
    QStringList list;
#ifdef DOCUSEARCH_HAS_WINDOWS_OCR
    try {
        winrt::init_apartment();
        auto langs = winrt::OcrEngine::AvailableRecognizerLanguages();
        for (auto const& lang : langs) {
            list << QString::fromWCharArray(lang.LanguageTag().c_str());
        }
    } catch (...) {}
#endif
    return list;
}

} // namespace DocuSearch
