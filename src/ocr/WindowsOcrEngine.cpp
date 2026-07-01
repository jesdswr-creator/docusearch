// ============================================================
// WindowsOcrEngine.cpp - Windows OCR via dynamic WinRT loading
// ============================================================
//
// Loads WinRT functions dynamically via LoadLibrary to avoid linking
// runtimeobject.lib (which contains /INCLUDE:WINRT_CRT_MAIN that
// conflicts with Qt's WIN32 entry point).
// ============================================================

#include "WindowsOcrEngine.h"
#include "../core/Logger.h"

#include <QImage>
#include <QFileInfo>
#include <QCoreApplication>

#ifdef DOCUSEARCH_HAS_WINDOWS_OCR

// Suppress runtimeobject.lib auto-linking. The WinRT ABI headers
// (included via <windows.media.ocr.h> etc.) contain:
//   #pragma comment(lib, "runtimeobject.lib")
// which pulls in /INCLUDE:WINRT_CRT_MAIN, conflicting with Qt's
// WIN32 entry point. Defining WINRT_NO_MAKE_LINK before including
// any WinRT headers prevents the #pragma from being emitted.
#define WINRT_NO_MAKE_LINK

#include <windows.h>
#include <wrl/client.h>

#include <windows.foundation.h>
#include <windows.media.ocr.h>
#include <windows.graphics.imaging.h>
#include <windows.storage.streams.h>

using namespace Microsoft::WRL;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Media::Ocr;
using namespace ABI::Windows::Graphics::Imaging;
using namespace ABI::Windows::Storage::Streams;

// ---- Dynamic WinRT function pointers ----
// These functions are in api-ms-win-core-winrt-l1-1-0.dll (always
// available on Windows 10+). We load them dynamically to avoid
// linking runtimeobject.lib.
static HRESULT (STDAPICALLTYPE *pfnRoInitialize)(int) = nullptr;
static HRESULT (STDAPICALLTYPE *pfnRoActivateInstance)(HSTRING, IInspectable**) = nullptr;
static HRESULT (STDAPICALLTYPE *pfnRoGetActivationFactory)(HSTRING, REFIID, void**) = nullptr;
static HRESULT (STDAPICALLTYPE *pfnWindowsCreateString)(LPCWSTR, UINT32, HSTRING*) = nullptr;
static HRESULT (STDAPICALLTYPE *pfnWindowsDeleteString)(HSTRING) = nullptr;
static LPCWSTR (STDAPICALLTYPE *pfnWindowsGetStringRawBuffer)(HSTRING, UINT32*) = nullptr;

static bool g_winrtLoaded = false;

static bool loadWinRT() {
    if (g_winrtLoaded) return true;

    HMODULE hWinRT = LoadLibraryW(L"api-ms-win-core-winrt-l1-1-0.dll");
    if (!hWinRT) hWinRT = LoadLibraryW(L"combase.dll");
    if (!hWinRT) {
        DS_ERROR("WinOCR", "Cannot load WinRT runtime DLL");
        return false;
    }

    HMODULE hWinStr = LoadLibraryW(L"api-ms-win-core-winrt-string-l1-1-0.dll");
    if (!hWinStr) hWinStr = hWinRT;  // combase.dll also has string functions

    pfnRoInitialize = (HRESULT(STDAPICALLTYPE*)(int))GetProcAddress(hWinRT, "RoInitialize");
    pfnRoActivateInstance = (HRESULT(STDAPICALLTYPE*)(HSTRING, IInspectable**))GetProcAddress(hWinRT, "RoActivateInstance");
    pfnRoGetActivationFactory = (HRESULT(STDAPICALLTYPE*)(HSTRING, REFIID, void**))GetProcAddress(hWinRT, "RoGetActivationFactory");
    pfnWindowsCreateString = (HRESULT(STDAPICALLTYPE*)(LPCWSTR, UINT32, HSTRING*))GetProcAddress(hWinStr, "WindowsCreateString");
    pfnWindowsDeleteString = (HRESULT(STDAPICALLTYPE*)(HSTRING))GetProcAddress(hWinStr, "WindowsDeleteString");
    pfnWindowsGetStringRawBuffer = (LPCWSTR(STDAPICALLTYPE*)(HSTRING, UINT32*))GetProcAddress(hWinStr, "WindowsGetStringRawBuffer");

    if (!pfnRoActivateInstance || !pfnWindowsCreateString) {
        DS_ERROR("WinOCR", "Cannot find required WinRT functions");
        return false;
    }

    g_winrtLoaded = true;
    return true;
}

// Helper: create HSTRING from wchar_t*
static HSTRING makeHString(const wchar_t* str) {
    HSTRING hs = nullptr;
    if (pfnWindowsCreateString) {
        pfnWindowsCreateString(str, static_cast<UINT32>(wcslen(str)), &hs);
    }
    return hs;
}

// Helper: convert HSTRING to QString
static QString hstringToQString(HSTRING hs) {
    if (!hs) return {};
    UINT32 len = 0;
    auto ptr = pfnWindowsGetStringRawBuffer ? pfnWindowsGetStringRawBuffer(hs, &len) : nullptr;
    if (!ptr) return {};
    return QString::fromUtf16(reinterpret_cast<const char16_t*>(ptr), len);
}

// Helper: activate a WinRT class by name
template<typename T>
static ComPtr<T> activateInstance(const wchar_t* className) {
    ComPtr<T> instance;
    HSTRING hs = makeHString(className);
    if (hs) {
        ComPtr<IInspectable> inspectable;
        pfnRoActivateInstance(hs, &inspectable);
        if (inspectable) {
            inspectable.As(&instance);
        }
        if (pfnWindowsDeleteString) pfnWindowsDeleteString(hs);
    }
    return instance;
}

// Helper: get activation factory
template<typename T>
static ComPtr<T> getActivationFactory(const wchar_t* className) {
    ComPtr<T> factory;
    HSTRING hs = makeHString(className);
    if (hs) {
        pfnRoGetActivationFactory(hs, __uuidof(T), &factory);
        if (pfnWindowsDeleteString) pfnWindowsDeleteString(hs);
    }
    return factory;
}

#endif // DOCUSEARCH_HAS_WINDOWS_OCR

namespace DocuSearch {

WindowsOcrEngine::WindowsOcrEngine() = default;
WindowsOcrEngine::~WindowsOcrEngine() = default;

bool WindowsOcrEngine::init() {
#ifdef DOCUSEARCH_HAS_WINDOWS_OCR
    if (initialized_) return true;
    if (!loadWinRT()) return false;

    // Initialize COM/WinRT.
    if (pfnRoInitialize) {
        HRESULT hr = pfnRoInitialize(0); // RO_INIT_MULTITHREADED
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
            DS_ERROR("WinOCR", "RoInitialize failed");
            return false;
        }
    }

    // Create OcrEngine from user profile languages.
    // Use the static factory to call TryCreateFromUserProfileLanguages.
    auto factory = getActivationFactory<IOcrEngineStatics>(
        RuntimeClass_Windows_Media_Ocr_OcrEngine);
    if (!factory) {
        DS_WARN("WinOCR", "Cannot get OcrEngineStatics factory");
        return false;
    }

    ComPtr<IOcrEngine> engine;
    factory->TryCreateFromUserProfileLanguages(&engine);
    if (!engine) {
        DS_WARN("WinOCR", "No OCR languages available");
        return false;
    }

    engine_ = engine.Detach();
    initialized_ = true;
    DS_INFO("WinOCR", "Windows OCR engine initialized");
    return true;
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

        // Swap RGBA -> BGRA.
        std::vector<uint8_t> bgra(w * h * 4);
        const uint8_t* src = conv.bits();
        for (int i = 0; i < w * h; ++i) {
            bgra[i*4+0] = src[i*4+2]; // B
            bgra[i*4+1] = src[i*4+1]; // G
            bgra[i*4+2] = src[i*4+0]; // R
            bgra[i*4+3] = src[i*4+3]; // A
        }

        // Create InMemoryRandomAccessStream.
        auto stream = activateInstance<IRandomAccessStream>(
            RuntimeClass_Windows_Storage_Streams_InMemoryRandomAccessStream);
        if (!stream) return {};

        // Create DataWriter.
        auto dataWriter = activateInstance<IDataWriter>(
            RuntimeClass_Windows_Storage_Streams_DataWriter);
        if (!dataWriter) return {};

        // Set output stream.
        ComPtr<IOutputStream> outputStream;
        stream.As(&outputStream);
        dataWriter->put_OutputStream(outputStream.Get());

        // Write bytes.
        dataWriter->WriteBytes(static_cast<UINT32>(bgra.size()), bgra.data());

        // Store async — call synchronously.
        ComPtr<IAsyncOperation<bool>> storeAsync;
        dataWriter->StoreAsync(&storeAsync);
        if (storeAsync) {
            ComPtr<IAsyncInfo> asyncInfo;
            storeAsync.As(&asyncInfo);
            AsyncStatus status = AsyncStatus::Started;
            for (int i = 0; i < 1000 && status == AsyncStatus::Started; ++i) {
                asyncInfo->get_Status(&status);
                if (status == AsyncStatus::Started) { Sleep(1); }
            }
        }

        // Detach buffer.
        ComPtr<IBuffer> buffer;
        dataWriter->DetachBuffer(&buffer);
        if (!buffer) return {};

        // Create SoftwareBitmap from buffer.
        auto sbFactory = getActivationFactory<ISoftwareBitmapFactory>(
            RuntimeClass_Windows_Graphics_Imaging_SoftwareBitmap);
        if (!sbFactory) return {};

        ComPtr<ISoftwareBitmap> sb;
        sbFactory->CreateCopyFromBuffer(
            BitmapPixelFormat_Bgra8, w, h,
            buffer.Get(),
            BitmapAlphaMode_Premultiplied,
            &sb);
        if (!sb) return {};

        // Run OCR.
        ComPtr<IOcrEngine> ocrEngine(static_cast<IOcrEngine*>(engine_));
        ComPtr<IAsyncOperation<OcrResult*>> asyncOp;
        ocrEngine->RecognizeAsync(sb.Get(), &asyncOp);
        if (!asyncOp) return {};

        // Wait for completion (poll-based).
        ComPtr<IAsyncInfo> asyncInfo;
        asyncOp.As(&asyncInfo);
        AsyncStatus status = AsyncStatus::Started;
        for (int i = 0; i < 10000 && status == AsyncStatus::Started; ++i) {
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
        if (textHs && pfnWindowsDeleteString) pfnWindowsDeleteString(textHs);
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
        if (!loadWinRT()) return list;
        if (pfnRoInitialize) pfnRoInitialize(0);

        auto statics = getActivationFactory<IOcrEngineStatics>(
            RuntimeClass_Windows_Media_Ocr_OcrEngine);
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
                        if (tag && pfnWindowsDeleteString) pfnWindowsDeleteString(tag);
                    }
                }
            }
        }
    } catch (...) {}
#endif
    return list;
}

} // namespace DocuSearch
