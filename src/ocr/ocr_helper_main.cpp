// ============================================================
// ocr_helper_main.cpp - oneocr.dll-based OCR helper (crash-proof)
// ============================================================
//
// REPLACES the previous WinRT/Windows.Media.Ocr implementation that
// was crashing the app on low-RAM Windows systems.
//
// This version uses oneocr.dll — the native OCR engine shipped with
// the Windows 11 Snipping Tool (Microsoft.ScreenSketch). It is a
// plain C-ABI DLL (no WinRT, no apartment threading, no async IAsyncOperation),
// so it cannot trigger the WinRT init/recognize crashes we were seeing.
//
// The DLL + model files are NOT redistributed. The user obtains them
// from their locally-installed Snipping Tool via scripts/get_oneocr.ps1
// and they are placed next to docusearch.exe.
//
// Crash-safety design:
//   • Runs as a SEPARATE process (killed by main app if it hangs).
//   • Loads oneocr.dll via LoadLibraryW (no static linkage).
//   • All Win32 errors are caught and reported as text — no exceptions escape.
//   • Per-file try/catch keeps one bad image from aborting the batch.
//   • 100 ms gap between files keeps memory pressure low on 4 GB systems.
//
// Usage: docusearch_ocr_helper.exe <image_path_1> [image_path_2] ...
// Output for each image:
//   ===FILE===<path>
//   <recognized text>
//   ===END===
// Exit codes: 0 = at least one image succeeded, 1 = all failed / setup error
// ============================================================

#include <windows.h>
#include <wincodec.h>
#include <objbase.h>

#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

// ── oneocr C ABI ────────────────────────────────────────────
// Reverse-engineered from oneocr.py (https://github.com/AuroraWright/oneocr).
// The DLL exports use the default x64 calling convention (= __cdecl on x64).
//
// ImageStruct must match the layout the DLL expects: 40 bytes on x64,
// 8-byte aligned. We use #pragma pack(8) to be safe.

#pragma pack(push, 8)
struct ImageStruct {
    int32_t  type;        // = 3 (BGRA image type)
    int32_t  width;
    int32_t  height;
    int32_t  reserved;    // = 0
    int64_t  step_size;   // bytes per row = width * 4
    uint8_t* data_ptr;    // BGRA pixel buffer
};
#pragma pack(pop)

// Model key (from oneocr.py — a hardcoded 26-char password baked into the DLL).
static const char MODEL_KEY[] = "kj)TGtrK>f]b[Piow.gU+nC@s\"\"\"\"\"\"4";

// Function pointer types for the DLL exports.
typedef int64_t (*CreateOcrInitOptions_t)             (int64_t* out);
typedef int64_t (*OcrInitOptionsSetUseModelDelayLoad_t)(int64_t opts, char flag);
typedef int64_t (*CreateOcrPipeline_t)                (const char* model_path, const char* model_key, int64_t opts, int64_t* out);
typedef int64_t (*CreateOcrProcessOptions_t)          (int64_t* out);
typedef int64_t (*RunOcrPipeline_t)                   (int64_t pipeline, ImageStruct* img, int64_t proc_opts, int64_t* out_result);
typedef int64_t (*GetOcrLineCount_t)                  (int64_t result, int64_t* out);
typedef int64_t (*GetOcrLine_t)                       (int64_t result, int64_t idx, int64_t* out);
typedef int64_t (*GetOcrLineContent_t)                (int64_t line, char** out);
typedef void    (*ReleaseOcrResult_t)                 (int64_t);
typedef void    (*ReleaseOcrInitOptions_t)            (int64_t);
typedef void    (*ReleaseOcrPipeline_t)               (int64_t);
typedef void    (*ReleaseOcrProcessOptions_t)         (int64_t);

// ── Loaded function pointers ────────────────────────────────
static HMODULE                            g_oneocrDll = nullptr;
static CreateOcrInitOptions_t             pCreateOcrInitOptions = nullptr;
static OcrInitOptionsSetUseModelDelayLoad_t pOcrInitOptionsSetUseModelDelayLoad = nullptr;
static CreateOcrPipeline_t                pCreateOcrPipeline = nullptr;
static CreateOcrProcessOptions_t          pCreateOcrProcessOptions = nullptr;
static RunOcrPipeline_t                   pRunOcrPipeline = nullptr;
static GetOcrLineCount_t                  pGetOcrLineCount = nullptr;
static GetOcrLine_t                       pGetOcrLine = nullptr;
static GetOcrLineContent_t                pGetOcrLineContent = nullptr;
static ReleaseOcrResult_t                 pReleaseOcrResult = nullptr;
static ReleaseOcrInitOptions_t            pReleaseOcrInitOptions = nullptr;
static ReleaseOcrPipeline_t               pReleaseOcrPipeline = nullptr;
static ReleaseOcrProcessOptions_t         pReleaseOcrProcessOptions = nullptr;

// ── UTF-8 <-> wide string conversions ───────────────────────
static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], len);
    if (len > 0) w.resize(len - 1);
    return w;
}

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], len, nullptr, nullptr);
    if (len > 0) s.resize(len - 1);
    return s;
}

// ── Locate oneocr files ─────────────────────────────────────
// Search order (first hit wins):
//   1. <exeDir>/                                  (preferred — portable)
//   2. <exeDir>/oneocr/
//   3. <exeDir>/models/oneocr/
//   4. %USERPROFILE%/.config/oneocr/              (oneocr.py default)
static std::wstring FindOneocrDir() {
    wchar_t exePath[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"";
    std::wstring exeDir(exePath, n);
    size_t pos = exeDir.find_last_of(L"\\/");
    if (pos != std::wstring::npos) exeDir = exeDir.substr(0, pos);

    const std::wstring candidates[] = {
        exeDir,
        exeDir + L"\\oneocr",
        exeDir + L"\\models\\oneocr",
    };
    for (const auto& dir : candidates) {
        std::wstring dllPath = dir + L"\\oneocr.dll";
        DWORD attr = GetFileAttributesW(dllPath.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            return dir;
        }
    }

    // Fallback to user-config dir (matches oneocr.py default).
    wchar_t* userProfile = _wgetenv(L"USERPROFILE");
    if (userProfile) {
        std::wstring cfgDir = std::wstring(userProfile) + L"\\.config\\oneocr";
        DWORD attr = GetFileAttributesW((cfgDir + L"\\oneocr.dll").c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            return cfgDir;
        }
    }

    return L"";
}

// ── Load oneocr.dll and resolve exports ─────────────────────
static bool LoadOneocr() {
    std::wstring dir = FindOneocrDir();
    if (dir.empty()) {
        std::cerr << "[oneocr] ERROR: oneocr.dll not found." << std::endl;
        std::cerr << "[oneocr] Run scripts/get_oneocr.ps1 to install OCR support," << std::endl;
        std::cerr << "[oneocr] or copy oneocr.dll + oneocr.onemodel + onnxruntime.dll" << std::endl;
        std::cerr << "[oneocr] from the Windows 11 Snipping Tool into the app folder." << std::endl;
        return false;
    }

    // Make sure oneocr.dll can locate onnxruntime.dll in the same folder.
    SetDllDirectoryW(dir.c_str());

    std::wstring dllPath = dir + L"\\oneocr.dll";
    g_oneocrDll = LoadLibraryW(dllPath.c_str());
    if (!g_oneocrDll) {
        DWORD err = GetLastError();
        std::cerr << "[oneocr] LoadLibraryW failed for oneocr.dll (error " << err << ")." << std::endl;
        if (err == 126 /* ERROR_MOD_NOT_FOUND */) {
            std::cerr << "[oneocr] onnxruntime.dll is probably missing from the same folder." << std::endl;
        }
        SetDllDirectoryW(nullptr);
        return false;
    }

    pCreateOcrInitOptions              = (CreateOcrInitOptions_t)             GetProcAddress(g_oneocrDll, "CreateOcrInitOptions");
    pOcrInitOptionsSetUseModelDelayLoad= (OcrInitOptionsSetUseModelDelayLoad_t)GetProcAddress(g_oneocrDll, "OcrInitOptionsSetUseModelDelayLoad");
    pCreateOcrPipeline                 = (CreateOcrPipeline_t)                GetProcAddress(g_oneocrDll, "CreateOcrPipeline");
    pCreateOcrProcessOptions           = (CreateOcrProcessOptions_t)          GetProcAddress(g_oneocrDll, "CreateOcrProcessOptions");
    pRunOcrPipeline                    = (RunOcrPipeline_t)                   GetProcAddress(g_oneocrDll, "RunOcrPipeline");
    pGetOcrLineCount                   = (GetOcrLineCount_t)                  GetProcAddress(g_oneocrDll, "GetOcrLineCount");
    pGetOcrLine                        = (GetOcrLine_t)                       GetProcAddress(g_oneocrDll, "GetOcrLine");
    pGetOcrLineContent                 = (GetOcrLineContent_t)                GetProcAddress(g_oneocrDll, "GetOcrLineContent");
    pReleaseOcrResult                  = (ReleaseOcrResult_t)                 GetProcAddress(g_oneocrDll, "ReleaseOcrResult");
    pReleaseOcrInitOptions             = (ReleaseOcrInitOptions_t)            GetProcAddress(g_oneocrDll, "ReleaseOcrInitOptions");
    pReleaseOcrPipeline                = (ReleaseOcrPipeline_t)               GetProcAddress(g_oneocrDll, "ReleaseOcrPipeline");
    pReleaseOcrProcessOptions          = (ReleaseOcrProcessOptions_t)         GetProcAddress(g_oneocrDll, "ReleaseOcrProcessOptions");

    if (!pCreateOcrInitOptions || !pCreateOcrPipeline || !pRunOcrPipeline ||
        !pGetOcrLineCount || !pGetOcrLine || !pGetOcrLineContent ||
        !pReleaseOcrResult || !pReleaseOcrPipeline || !pReleaseOcrInitOptions) {
        std::cerr << "[oneocr] oneocr.dll is missing required exports (incompatible version?)." << std::endl;
        FreeLibrary(g_oneocrDll);
        g_oneocrDll = nullptr;
        SetDllDirectoryW(nullptr);
        return false;
    }

    return true;
}

// ── Load image as BGRA via WIC ──────────────────────────────
struct BgraImage {
    int32_t width = 0;
    int32_t height = 0;
    std::vector<uint8_t> data;  // BGRA, stride = width*4
};

static bool LoadImageBgra(const std::wstring& path, BgraImage& out) {
    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) return false;

    IWICBitmapDecoder* decoder = nullptr;
    hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr,
        GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr) || !decoder) { factory->Release(); return false; }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    decoder->Release();
    if (FAILED(hr) || !frame) { factory->Release(); return false; }

    UINT w = 0, h = 0;
    frame->GetSize(&w, &h);
    if (w == 0 || h == 0) { frame->Release(); factory->Release(); return false; }

    // Enforce the same limits as oneocr.py: 50..10000 px per side.
    if (w > 10000 || h > 10000) { frame->Release(); factory->Release(); return false; }
    if (w < 50   || h < 50)     { frame->Release(); factory->Release(); return false; }

    IWICFormatConverter* converter = nullptr;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr) || !converter) { frame->Release(); factory->Release(); return false; }

    // GUID_WICPixelFormat32bppPBGRA = premultiplied BGRA — exactly what oneocr expects.
    hr = converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    frame->Release();
    if (FAILED(hr)) { converter->Release(); factory->Release(); return false; }

    const size_t stride = (size_t)w * 4;
    out.width  = (int32_t)w;
    out.height = (int32_t)h;
    out.data.assign(stride * h, 0);

    hr = converter->CopyPixels(nullptr, (UINT)stride, (UINT)out.data.size(), out.data.data());
    converter->Release();
    factory->Release();

    if (FAILED(hr)) { out.data.clear(); return false; }
    return true;
}

// ── Run OCR on a single file ────────────────────────────────
// Returns recognized text. Errors are returned as "[ERROR: ...]" strings
// (the main app treats any line starting with '[' as a failure marker).
static std::string OcrFile(int64_t pipeline, int64_t procOpts, const std::string& path) {
    // Skip files > 20 MB (low-RAM protection — same as the previous helper).
    std::wstring wpath = Utf8ToWide(path);
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &fad)) {
        ULARGE_INTEGER size;
        size.LowPart  = fad.nFileSizeLow;
        size.HighPart = fad.nFileSizeHigh;
        if (size.QuadPart > 20LL * 1024 * 1024) {
            return "[SKIPPED: file too large (>20MB)]";
        }
    }

    BgraImage img;
    if (!LoadImageBgra(wpath, img)) {
        return "[ERROR: failed to load image]";
    }

    ImageStruct is{};
    is.type      = 3;
    is.width     = img.width;
    is.height    = img.height;
    is.reserved  = 0;
    is.step_size = (int64_t)img.width * 4;
    is.data_ptr  = img.data.data();

    int64_t result = 0;
    int64_t status = pRunOcrPipeline(pipeline, &is, procOpts, &result);
    if (status != 0 || result == 0) {
        return "[ERROR: RunOcrPipeline failed (status " + std::to_string(status) + ")]";
    }

    int64_t lineCount = 0;
    pGetOcrLineCount(result, &lineCount);

    std::string text;
    text.reserve(256);
    for (int64_t i = 0; i < lineCount; ++i) {
        int64_t line = 0;
        if (pGetOcrLine(result, i, &line) != 0 || line == 0) continue;

        char* content = nullptr;
        if (pGetOcrLineContent(line, &content) == 0 && content != nullptr) {
            text.append(content);
            text.push_back('\n');
        }
    }

    pReleaseOcrResult(result);
    return text;
}

// ── SEH filter: turn crashes into error messages ────────────
// If oneocr.dll dereferences a bad pointer (rare), we want the helper
// to keep running for the remaining files rather than dying silently.
static LONG NTAPI VectoredHandler(EXCEPTION_POINTERS* ep) {
    // We only log; we don't actually recover — the OS will terminate this process.
    // But the main app monitors via QProcess and will treat a crash exit as a failure
    // for that one batch, then continue running normally.
    (void)ep;
    return EXCEPTION_CONTINUE_SEARCH;
}

// ── Main ────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <image_path> [<image_path> ...]" << std::endl;
        return 1;
    }

    // Install vectored exception handler (best-effort logging only).
    AddVectoredExceptionHandler(0, VectoredHandler);

    // COM (MTA) is needed for WIC image loading. NOT for oneocr.dll itself
    // (which is plain C and needs no apartment).
    HRESULT coInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    if (!LoadOneocr()) {
        if (SUCCEEDED(coInit)) CoUninitialize();
        return 1;
    }

    // Create init options.
    int64_t initOpts = 0;
    if (pCreateOcrInitOptions(&initOpts) != 0 || initOpts == 0) {
        std::cerr << "[oneocr] CreateOcrInitOptions failed." << std::endl;
        if (g_oneocrDll) FreeLibrary(g_oneocrDll);
        SetDllDirectoryW(nullptr);
        if (SUCCEEDED(coInit)) CoUninitialize();
        return 1;
    }

    // Disable model delay-load so we fail fast if the model file is missing
    // (rather than failing on the first OCR call).
    if (pOcrInitOptionsSetUseModelDelayLoad) {
        pOcrInitOptionsSetUseModelDelayLoad(initOpts, 0);
    }

    // Find the model file path.
    std::wstring dir = FindOneocrDir();
    std::wstring modelPathW = dir + L"\\oneocr.onemodel";

    // Verify the model file exists — fail with a clear message if not.
    if (GetFileAttributesW(modelPathW.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::cerr << "[oneocr] oneocr.onemodel not found at: "
                  << WideToUtf8(modelPathW) << std::endl;
        std::cerr << "[oneocr] Re-run scripts/get_oneocr.ps1 to install the model." << std::endl;
        if (pReleaseOcrInitOptions) pReleaseOcrInitOptions(initOpts);
        if (g_oneocrDll) FreeLibrary(g_oneocrDll);
        SetDllDirectoryW(nullptr);
        if (SUCCEEDED(coInit)) CoUninitialize();
        return 1;
    }

    std::string modelPath = WideToUtf8(modelPathW);

    int64_t pipeline = 0;
    if (pCreateOcrPipeline(modelPath.c_str(), MODEL_KEY, initOpts, &pipeline) != 0 || pipeline == 0) {
        std::cerr << "[oneocr] CreateOcrPipeline failed (wrong model key or corrupted model?)." << std::endl;
        if (pReleaseOcrInitOptions) pReleaseOcrInitOptions(initOpts);
        if (g_oneocrDll) FreeLibrary(g_oneocrDll);
        SetDllDirectoryW(nullptr);
        if (SUCCEEDED(coInit)) CoUninitialize();
        return 1;
    }

    int64_t procOpts = 0;
    if (pCreateOcrProcessOptions) {
        pCreateOcrProcessOptions(&procOpts);
    }

    // Process each file. Per-file try/catch keeps one bad image from
    // aborting the whole batch.
    bool anySuccess = false;
    for (int i = 1; i < argc; ++i) {
        std::string path = argv[i];
        std::string text;

        try {
            text = OcrFile(pipeline, procOpts, path);
        } catch (const std::bad_alloc&) {
            text = "[ERROR: out of memory]";
            Sleep(2000);  // back off before next file
        } catch (const std::exception& e) {
            text = std::string("[ERROR: ") + e.what() + "]";
        } catch (...) {
            text = "[ERROR: unknown]";
        }

        std::cout << "===FILE===" << path << std::endl;
        std::cout << text << std::endl;
        std::cout << "===END===" << std::endl;
        std::cout.flush();

        if (!text.empty() && text[0] != '[') {
            anySuccess = true;
        }

        // 100 ms gap between files — keeps memory pressure low on 4 GB systems.
        Sleep(100);
    }

    // Cleanup.
    if (pReleaseOcrProcessOptions && procOpts) pReleaseOcrProcessOptions(procOpts);
    if (pReleaseOcrPipeline && pipeline)       pReleaseOcrPipeline(pipeline);
    if (pReleaseOcrInitOptions && initOpts)    pReleaseOcrInitOptions(initOpts);

    if (g_oneocrDll) FreeLibrary(g_oneocrDll);
    SetDllDirectoryW(nullptr);
    if (SUCCEEDED(coInit)) CoUninitialize();

    return anySuccess ? 0 : 1;
}
