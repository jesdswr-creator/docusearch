// ============================================================
// WindowsOcrEngine.cpp - Windows OCR via raw COM (no C++/WinRT)
// ============================================================
//
// Uses the Windows.Media.Ocr COM API directly via WRL/COM instead of
// C++/WinRT. This avoids the linker conflict between C++/WinRT's
// entry point redirection and Qt's WIN32 entry point.
//
// The Windows.Media.Ocr API is a Windows Runtime (WinRT) API, but we
// can call it via raw COM using the ABI headers from the Windows SDK.
// ============================================================

#include "WindowsOcrEngine.h"
#include "../core/Logger.h"

#include <QImage>
#include <QFileInfo>
#include <QCoreApplication>

#ifdef DOCUSEARCH_HAS_WINDOWS_OCR

#include <windows.h>
#include <roapi.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>

// Windows Runtime ABI headers
#include <windows.foundation.h>
#include <windows.media.ocr.h>
#include <windows.graphics.imaging.h>
#include <windows.storage.streams.h>

#pragma comment(lib, "runtimeobject.lib")

using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Media::Ocr;
using namespace ABI::Windows::Graphics::Imaging;
using namespace ABI::Windows::Storage::Streams;

// Helper: convert HSTRING to QString
static QString hstringToQString(HSTRING hs) {
    if (!hs) return {};
    UINT32 len = 0;
    auto ptr = WindowsGetStringRawBuffer(hs, &len);
    return QString::fromUtf16(reinterpret_cast<const char16_t*>(ptr), len);
}

// Helper: activate a WinRT class by name
template<typename T>
static ComPtr<T> ActivateInstance(const wchar_t* className) {
    ComPtr<T> instance;
    HSTRING_HEADER header;
    HSTRING hs;
    if (SUCCEEDED(WindowsCreateStringReference(className, static_cast<UINT32>(wcslen(className)), &header, &hs))) {
        ComPtr<IInspectable> inspectable;
        RoActivateInstance(hs, &inspectable);
        if (inspectable) {
            inspectable.As(&instance);
        }
    }
    return instance;
}

#endif // DOCUSEARCH_HAS_WINDOWS_OCR

namespace DocuSearch {

WindowsOcrEngine::WindowsOcrEngine() = default;
WindowsOcrEngine::~WindowsOcrEngine() = default;

bool WindowsOcrEngine::init() {
#ifdef DOCUSEARCH_HAS_WINDOWS_OCR
    if (initialized_) return true;
    try {
        HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
            DS_ERROR("WinOCR", "RoInitialize failed");
            return false;
        }

        auto engine = ActivateInstance<IOcrEngine>(
            RuntimeClass_Windows_Media_Ocr_OcrEngine);
        if (!engine) {
            DS_WARN("WinOCR", "Failed to create OcrEngine");
            return false;
        }

        engine_ = engine;
        initialized_ = true;
        DS_INFO("WinOCR", "Windows OCR engine initialized");
        return true;
    } catch (...) {
        DS_ERROR("WinOCR", "Unknown init error");
        return false;
    }
#else
    return false;
#endif
}

QString WindowsOcrEngine::ocrImage(const QImage& img) {
#ifdef DOCUSEARCH_HAS_WINDOWS_OCR
    if (!initialized_ && !init()) return {};
    if (img.isNull()) return {};
    if (!engine_) return {};

    try {
        // Convert QImage to BGRA.
        QImage conv = img.convertToFormat(QImage::Format_RGBA8888);
        if (conv.isNull()) return {};

        const int w = conv.width();
        const int h = conv.height();

        // Swap RGBA → BGRA.
        std::vector<uint8_t> bgra(w * h * 4);
        const uint8_t* src = conv.bits();
        for (int i = 0; i < w * h; ++i) {
            bgra[i*4+0] = src[i*4+2]; // B
            bgra[i*4+1] = src[i*4+1]; // G
            bgra[i*4+2] = src[i*4+0]; // R
            bgra[i*4+3] = src[i*4+3]; // A
        }

        // Create a DataWriter to write bytes into an InMemoryRandomAccessStream.
        auto stream = ActivateInstance<IRandomAccessStream>(
            RuntimeClass_Windows_Storage_Streams_InMemoryRandomAccessStream);
        if (!stream) return {};

        // Write BGRA bytes to the stream.
        ComPtr<IDataWriter> dataWriter;
        ComPtr<IInspectable> dwInspectable;
        HSTRING_HEADER dwHeader;
        HSTRING dwHs;
        WindowsCreateStringReference(
            RuntimeClass_Windows_Storage_Streams_DataWriter,
            static_cast<UINT32>(wcslen(RuntimeClass_Windows_Storage_Streams_DataWriter)),
            &dwHeader, &dwHs);
        RoActivateInstance(dwHs, &dwInspectable);
        if (dwInspectable) {
            dwInspectable.As(&dataWriter);
        }
        if (!dataWriter) return {};

        // Set the output stream.
        ComPtr<IOutputStream> outputStream;
        stream.As(&outputStream);
        dataWriter->put_OutputStream(outputStream.Get());

        // Write bytes.
        ComPtr<IBuffer> buffer;
        dataWriter->WriteBytes(
            static_cast<UINT32>(bgra.size()),
            bgra.data());
        dataWriter->StoreAsync();

        // Flush and seek to beginning.
        stream->Seek(0);

        // Create SoftwareBitmap from the buffer.
        ComPtr<ISoftwareBitmapFactory> sbFactory;
        ComPtr<IInspectable> sbfInspectable;
        HSTRING_HEADER sbfHeader;
        HSTRING sbfHs;
        WindowsCreateStringReference(
            RuntimeClass_Windows_Graphics_Imaging_SoftwareBitmap,
            static_cast<UINT32>(wcslen(RuntimeClass_Windows_Graphics_Imaging_SoftwareBitmap)),
            &sbfHeader, &sbfHs);
        RoGetActivationFactory(sbfHs, IID_PPV_ARGS(&sbFactory));
        if (!sbFactory) return {};

        ComPtr<IBuffer> dataBuffer;
        dataWriter->DetachBuffer(&dataBuffer);
        if (!dataBuffer) return {};

        ComPtr<ISoftwareBitmap> sb;
        sbFactory->CreateCopyFromBuffer(
            BitmapPixelFormat_Bgra8,
            w, h,
            dataBuffer.Get(),
            BitmapAlphaMode_Premultiplied,
            &sb);
        if (!sb) return {};

        // Run OCR.
        ComPtr<IAsyncOperation<OcrResult*>> asyncOp;
        engine_->RecognizeAsync(sb.Get(), &asyncOp);
        if (!asyncOp) return {};

        // Wait for async operation to complete (synchronous).
        ComPtr<IAsyncInfo> asyncInfo;
        asyncOp.As(&asyncInfo);
        AsyncStatus status = AsyncStatus::Started;
        while (status == AsyncStatus::Started) {
            asyncInfo->get_Status(&status);
            if (status == AsyncStatus::Started) {
                QCoreApplication::processEvents();
                Sleep(10);
            }
        }

        if (status != AsyncStatus::Completed) return {};

        ComPtr<IOcrResult> result;
        asyncOp->GetResults(&result);
        if (!result) return {};

        HSTRING textHs = nullptr;
        result->get_Text(&textHs);
        QString text = hstringToQString(textHs);
        if (textHs) WindowsDeleteString(textHs);
        return text;
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
        RoInitialize(RO_INIT_MULTITHREADED);
        ComPtr<IOcrEngineStatics> statics;
        ComPtr<IInspectable> inspectable;
        HSTRING_HEADER header;
        HSTRING hs;
        WindowsCreateStringReference(
            RuntimeClass_Windows_Media_Ocr_OcrEngine,
            static_cast<UINT32>(wcslen(RuntimeClass_Windows_Media_Ocr_OcrEngine)),
            &header, &hs);
        RoGetActivationFactory(hs, IID_PPV_ARGS(&statics));
        if (statics) {
            ComPtr<IVectorView<Language*>> langs;
            statics->get_AvailableRecognizerLanguages(&langs);
            if (langs) {
                UINT32 size = 0;
                langs->get_Size(&size);
                for (UINT32 i = 0; i < size; ++i) {
                    ComPtr<ILanguage> lang;
                    langs->GetAt(i, &lang);
                    if (lang) {
                        HSTRING tag = nullptr;
                        lang->get_LanguageTag(&tag);
                        list << hstringToQString(tag);
                        if (tag) WindowsDeleteString(tag);
                    }
                }
            }
        }
    } catch (...) {}
#endif
    return list;
}

} // namespace DocuSearch
