// ============================================================
// ocr_helper_main.cpp - Windows OCR helper (C++/WinRT)
// ============================================================
//
// Same approach as PowerToys:
// - Uses C++/WinRT to call Windows.Media.Ocr
// - Runs on MTA thread (required for RecognizeAsync().get())
// - Uses WIC to load the image file (no StorageFile needed)
// - Uses ISoftwareBitmapNativeFactory for zero-copy HBITMAP→SoftwareBitmap
//
// The C++/WinRT headers are generated at build time by cppwinrt.exe
// (pre-installed on Windows SDK / GitHub Actions runners).
//
// Usage: docusearch_ocr_helper.exe <image_path>
// Output: recognized text to stdout (UTF-8)
// ============================================================

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/base.h>

#include <windows.h>
#include <wincodec.h>
#include <Windows.Graphics.Imaging.Interop.h>
#include <objbase.h>

#include <iostream>
#include <string>
#include <future>
#include <comdef.h>

#pragma comment(lib, "runtimeobject.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace winrt {
    using namespace Windows::Foundation;
    using namespace Windows::Graphics::Imaging;
    using namespace Windows::Media::Ocr;
}

std::wstring StringToWide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], len);
    if (len > 0) w.resize(len - 1);
    return w;
}

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], len, nullptr, nullptr);
    if (len > 0) s.resize(len - 1);
    return s;
}

std::wstring OcrImageFile(const std::wstring& filePath) {
    // Run on MTA thread (required for RecognizeAsync().get())
    auto future = std::async(std::launch::async, [&filePath]() -> std::wstring {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        // 1. Load image via WIC (Windows Imaging Component)
        IWICImagingFactory* wicFactory = nullptr;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));
        if (!wicFactory) return L"";

        IWICBitmapDecoder* wicDecoder = nullptr;
        wicFactory->CreateDecoderFromFilename(filePath.c_str(), nullptr,
            GENERIC_READ, WICDecodeMetadataCacheOnLoad, &wicDecoder);
        if (!wicDecoder) { wicFactory->Release(); return L""; }

        IWICBitmapFrameDecode* wicFrame = nullptr;
        wicDecoder->GetFrame(0, &wicFrame);
        if (!wicFrame) { wicDecoder->Release(); wicFactory->Release(); return L""; }

        // 2. Convert to IWICBitmap
        IWICBitmap* wicBitmap = nullptr;
        wicFactory->CreateBitmapFromSource(wicFrame, WICBitmapNoCache, &wicBitmap);
        if (!wicBitmap) { wicFrame->Release(); wicDecoder->Release(); wicFactory->Release(); return L""; }

        // 3. Convert IWICBitmap → SoftwareBitmap (zero-copy via interop)
        winrt::SoftwareBitmap softwareBitmap = nullptr;
        try {
            auto factory = winrt::create_instance<ISoftwareBitmapNativeFactory>(
                winrt::guid_of<ISoftwareBitmapNativeFactory>());
            winrt::check_hresult(factory->CreateFromWICBitmap(
                wicBitmap, false, winrt::guid_of<winrt::SoftwareBitmap>(),
                winrt::put_abi(softwareBitmap)));
        } catch (...) {
            wicBitmap->Release(); wicFrame->Release(); wicDecoder->Release(); wicFactory->Release();
            return L"";
        }

        // 4. Ensure correct pixel format (Bgra8 Premultiplied)
        if (softwareBitmap.BitmapPixelFormat() != winrt::BitmapPixelFormat::Bgra8 ||
            softwareBitmap.BitmapAlphaMode() != winrt::BitmapAlphaMode::Premultiplied) {
            softwareBitmap = winrt::SoftwareBitmap::Convert(softwareBitmap,
                winrt::BitmapPixelFormat::Bgra8,
                winrt::BitmapAlphaMode::Premultiplied);
        }

        // Cleanup WIC objects
        wicBitmap->Release(); wicFrame->Release(); wicDecoder->Release(); wicFactory->Release();

        // 5. Create OCR engine
        winrt::OcrEngine engine = winrt::OcrEngine::TryCreateFromUserProfileLanguages();
        if (!engine) return L"";

        // 6. Run OCR
        winrt::OcrResult result = engine.RecognizeAsync(softwareBitmap).get();
        return std::wstring(result.Text().c_str());
    });
    return future.get();
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <image_path>" << std::endl;
        return 1;
    }

    try {
        winrt::init_apartment();

        std::wstring filePath = StringToWide(argv[1]);
        std::wstring text = OcrImageFile(filePath);

        if (text.empty()) {
            return 1;
        }

        std::cout << WideToUtf8(text) << std::endl;
        return 0;
    } catch (const winrt::hresult_error& e) {
        std::cerr << "WinRT error: " << WideToUtf8(e.message().c_str()) << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown error" << std::endl;
        return 1;
    }
}
