// ============================================================
// ocr_helper_main.cpp - Windows OCR helper (same as PowerToys)
// ============================================================
//
// Standalone CONSOLE app using C++/WinRT to call Windows.Media.Ocr.
// This exe links runtimeobject.lib directly — since it's NOT a Qt
// WIN32 app, there's NO entry point conflict.
//
// This is the same approach PowerToys Text Extractor uses.
//
// Usage: docusearch_ocr_helper.exe <image_path>
// Output: recognized text to stdout (UTF-8)
// ============================================================

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <iostream>
#include <string>

#pragma comment(lib, "runtimeobject.lib")

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <image_path>" << std::endl;
        return 1;
    }

    try {
        winrt::init_apartment();

        // Convert argv[1] to wide string
        std::string pathUtf8 = argv[1];
        std::wstring pathWide(pathUtf8.begin(), pathUtf8.end());

        // 1. Open the image file
        auto file = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(
            winrt::hstring(pathWide.c_str())).get();

        // 2. Open a read stream
        auto stream = file.OpenAsync(
            winrt::Windows::Storage::FileAccessMode::Read).get();

        // 3. Decode the image
        auto decoder = winrt::Windows::Graphics::Imaging::BitmapDecoder::CreateAsync(stream).get();

        // 4. Get the SoftwareBitmap
        auto bitmap = decoder.GetSoftwareBitmapAsync().get();
        if (!bitmap) {
            std::cerr << "Failed to get bitmap" << std::endl;
            return 1;
        }

        // 5. Create OCR engine from user's installed languages
        auto engine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromUserProfileLanguages();
        if (!engine) {
            std::cerr << "No OCR languages installed" << std::endl;
            return 1;
        }

        // 6. Run OCR
        auto result = engine.RecognizeAsync(bitmap).get();

        // 7. Print recognized text to stdout
        auto text = result.Text();
        std::wcout << text.c_str() << std::endl;

        return 0;
    } catch (const winrt::hresult_error& e) {
        std::cerr << "WinRT error: " << winrt::to_string(e.message()) << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown error" << std::endl;
        return 1;
    }
}
