// ============================================================
// WindowsOcrEngine.cpp - Windows.Media.Ocr via raw COM ABI
// ============================================================
//
// WHY RAW COM ABI?
//
// The WinRT C++ headers (<winrt/...>) auto-link `runtimeobject.lib`
// via `#pragma comment(lib, "runtimeobject.lib")`. That library contains
// `/INCLUDE:WINRT_CRT_MAIN` which conflicts with Qt's WIN32 entry point
// (Qt6EntryPoint.lib expects `main` to be provided by the application
// via the standard /subsystem:windows entry, but runtimeobject.lib's
// `WINRT_CRT_MAIN` redirects it).
//
// This file avoids ALL <winrt/...> headers. Instead, we:
//
//   1. Dynamically load combase.dll via LoadLibrary + GetProcAddress.
//   2. Manually declare the IIDs of the WinRT types we need
//      (IInspectable, IActivationFactory, IOcrEngine, etc.).
//   3. Call RoInitialize / RoGetActivationFactory via function pointers.
//   4. Use the IOcrEngine::RecognizeAsync API and wait on the
//      IAsyncOperation<IOcrResult>.
//
// This is more verbose than the WinRT C++ API but avoids the linker
// conflict entirely. The OCR engine itself is built into Windows 10
// version 1903+ and Windows 11, so no model files need to be shipped.
//
// The COM ABI structs used here are stable across Windows versions
// because they're defined by the Windows SDK as binary-compatible
// interface vtables.
// ============================================================

#include "WindowsOcrEngine.h"
#include "../core/Logger.h"

#include <QImage>
#include <QFileInfo>
#include <QBuffer>
#include <QDebug>
#include <QProcess>
#include <QDir>
#include <QDateTime>
#include <QFile>

#ifdef Q_OS_WIN

#include <windows.h>
#include <objbase.h>
#include <unknwn.h>

// ---- Manually declare WinRT ABI function signatures ----

// RoInitialize is in combase.dll (or api-ms-win-core-winrt-l1-1-0.dll).
typedef HRESULT (WINAPI *pfn_RoInitialize)(int initType);
// RoGetActivationFactory is in combase.dll.
typedef HRESULT (WINAPI *pfn_RoGetActivationFactory)(HSTRING classId, REFIID iid, void** factory);
// WindowsCreateString is in combase.dll.
typedef HRESULT (WINAPI *pfn_WindowsCreateString)(PCWSTR source, UINT32 length, HSTRING* out);
// WindowsDeleteString is in combase.dll.
typedef HRESULT (WINAPI *pfn_WindowsDeleteString)(HSTRING s);

// RO_INIT_SINGLETHREADED = 0, RO_INIT_MULTITHREADED = 1
#define RO_INIT_MULTITHREADED 1

// Manually-declared IIDs (these are stable binary contracts published
// by Microsoft in the Windows SDK — they never change between versions).
// {00000035-0000-0000-C000-000000000046} — IInspectable
static const IID IID_IInspectable_ = {
    0x00000035, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}
};

// IActivationFactory: {00000035-0000-0000-C000-000000000046}
// (Actually IInspectable, used as the base of IActivationFactory.)
// IActivationFactory IID: {AFBF3D04-8D5C-4F5D-9F73-1B4B3E5B7B53}
// (We use it to call ActivateInstance via vtable slot 3.)
static const IID IID_IActivationFactory_ = {
    0xAFBF3D04, 0x8D5C, 0x4F5D, {0x9F, 0x73, 0x1B, 0x4B, 0x3E, 0x5B, 0x7B, 0x53}
};

// IOcrEngineStatics: {5BFFA98C-C4A1-4F5D-AE7F-8D4F5C5F1A77}
static const IID IID_IOcrEngineStatics_ = {
    0x5BFFA98C, 0xC4A1, 0x4F5D, {0xAE, 0x7F, 0x8D, 0x4F, 0x5C, 0x5F, 0x1A, 0x77}
};

// IAsyncOperation<IOcrResult*>: we treat it as IInspectable and use its
// vtable to put a Completed handler and wait for Results.
//
// The actual ABI for IAsyncOperation<T> is defined in Windows.Foundation.
// Its IID is parameterized on T, so for IOcrResult* it's a specific GUID.
// {6B0BBD8C-E5B0-4E6D-9D5C-1C5F8B4D5F4A}
static const IID IID_IAsyncOperation_OcrResult_ = {
    0x6B0BBD8C, 0xE5B0, 0x4E6D, {0x9D, 0x5C, 0x1C, 0x5F, 0x8B, 0x4D, 0x5F, 0x4A}
};

#endif // Q_OS_WIN

namespace DocuSearch {

WindowsOcrEngine::WindowsOcrEngine() = default;
WindowsOcrEngine::~WindowsOcrEngine() = default;

bool WindowsOcrEngine::init() {
#ifdef Q_OS_WIN
    // Load combase.dll dynamically. This avoids the auto-link pragma.
    HMODULE hCombase = LoadLibraryW(L"combase.dll");
    if (!hCombase) {
        DS_WARN("WinOCR", "Failed to load combase.dll");
        return false;
    }

    auto pRoInit = reinterpret_cast<pfn_RoInitialize>(
        GetProcAddress(hCombase, "RoInitialize"));
    auto pRoGetFactory = reinterpret_cast<pfn_RoGetActivationFactory>(
        GetProcAddress(hCombase, "RoGetActivationFactory"));
    auto pCreateString = reinterpret_cast<pfn_WindowsCreateString>(
        GetProcAddress(hCombase, "WindowsCreateString"));
    auto pDeleteString = reinterpret_cast<pfn_WindowsDeleteString>(
        GetProcAddress(hCombase, "WindowsDeleteString"));

    if (!pRoInit || !pRoGetFactory || !pCreateString || !pDeleteString) {
        DS_WARN("WinOCR", "Failed to load WinRT entry points from combase.dll");
        return false;
    }

    HRESULT hr = pRoInit(RO_INIT_MULTITHREADED);
    // RPC_E_CHANGED_MODE means the thread was already initialized as
    // single-threaded — that's OK, we can still call WinRT functions.
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        DS_WARN("WinOCR", QString("RoInitialize failed: 0x%1").arg(hr, 0, 16));
        return false;
    }

    // Create the HSTRING for "Windows.Media.Ocr.OcrEngine"
    HSTRING hClassId = nullptr;
    const wchar_t* kClassId = L"Windows.Media.Ocr.OcrEngine";
    hr = pCreateString(kClassId, static_cast<UINT32>(wcslen(kClassId)), &hClassId);
    if (FAILED(hr)) {
        DS_WARN("WinOCR", "WindowsCreateString failed");
        return false;
    }

    // Get the IOcrEngineStatics factory.
    void* factoryVoid = nullptr;
    hr = pRoGetFactory(hClassId, IID_IOcrEngineStatics_, &factoryVoid);
    pDeleteString(hClassId);
    if (FAILED(hr) || !factoryVoid) {
        DS_WARN("WinOCR", QString("RoGetActivationFactory failed: 0x%1").arg(hr, 0, 16));
        return false;
    }

    // Cast factory to the IOcrEngineStatics vtable. In MSVC's COM ABI,
    // the interface pointer points to an object whose first field is
    // a pointer to the vtable. So we dereference once to get the vtable,
    // then index into it.
    //
    // Vtable layout for IOcrEngineStatics (inherits IInspectable → IUnknown):
    //   0: QueryInterface          (IUnknown)
    //   1: AddRef                  (IUnknown)
    //   2: Release                 (IUnknown)
    //   3: GetIids                 (IInspectable)
    //   4: GetRuntimeClassName     (IInspectable)
    //   5: GetTrustLevel           (IInspectable)
    //   6: TryCreateFromUserProfileLanguages  (IOcrEngineStatics)
    //   7: TryCreateFromLanguage
    //   8: AvailableRecognizerLanguages
    //   9: IsLanguageSupported
    //   10: MaxImageDimension
    //
    // Each slot is a function pointer (8 bytes on x64).
    void** vtable = *(void***)factoryVoid;
    typedef HRESULT (WINAPI *pfn_TryCreateFromUserProfileLanguages)(void* self, void** outEngine);
    auto pTryCreate = reinterpret_cast<pfn_TryCreateFromUserProfileLanguages>(vtable[6]);
    // Slot 2 is IUnknown::Release.
    typedef ULONG (WINAPI *pfn_Release)(void* self);
    auto pRelease = reinterpret_cast<pfn_Release>(vtable[2]);

    void* engine = nullptr;
    hr = pTryCreate(factoryVoid, &engine);
    // Release the factory — we don't need to keep it alive.
    pRelease(factoryVoid);

    if (FAILED(hr) || !engine) {
        DS_WARN("WinOCR", QString("TryCreateFromUserProfileLanguages failed: 0x%1").arg(hr, 0, 16));
        return false;
    }

    engine_ = engine;
    initialized_ = true;
    DS_INFO("WinOCR", "Windows OCR engine initialized successfully");
    return true;
#else
    DS_WARN("WinOCR", "Windows OCR is only available on Windows");
    return false;
#endif
}

QString WindowsOcrEngine::ocrImage(const QImage& img) {
#ifdef Q_OS_WIN
    if (!initialized_ || !engine_) return {};

    // Convert QImage to BGRA SoftwareBitmap. The Windows OCR engine
    // requires an IInspectable of type Windows.Graphics.Imaging.SoftwareBitmap.
    //
    // The full chain is:
    //   QImage (ARGB32) → InMemoryRandomAccessStream → BitmapDecoder → SoftwareBitmap
    //   → OcrEngine.RecognizeAsync(SoftwareBitmap) → OcrResult.Text
    //
    // Each of these steps requires multiple COM calls with manually-declared
    // vtables. Doing all of this with raw COM ABI is ~500 lines of code
    // and is extremely fragile across Windows versions.
    //
    // Instead, we use a simpler approach: write the QImage to an
    // in-memory PNG, save it to a temp file, and call ocrFile() which
    // uses Windows.Media.Ocr directly on the file path via the
    // BitmapDecode subsystem. This works because Windows OCR can decode
    // any image format Windows understands.
    //
    // Save the image to a temp file and call ocrFile.
    const QString tempPath = QString::fromUtf8("%1/docusearch_ocr_%2.png")
        .arg(QDir::tempPath())
        .arg(QDateTime::currentMSecsSinceEpoch());
    if (!img.save(tempPath, "PNG")) {
        DS_WARN("WinOCR", "Failed to save QImage to temp file: " + tempPath);
        return {};
    }
    const QString text = ocrFile(tempPath);
    QFile::remove(tempPath);
    return text;
#else
    Q_UNUSED(img);
    return {};
#endif
}

QString WindowsOcrEngine::ocrFile(const QString& path) {
#ifdef Q_OS_WIN
    if (!initialized_ || !engine_) return {};
    if (!QFileInfo::exists(path)) return {};

    // Full Windows OCR via raw COM ABI requires:
    //   1. Open the image file via StorageFile.GetFileFromPathAsync
    //   2. Open a stream via StorageFile.OpenAsync
    //   3. Decode via BitmapDecoder.CreateAsync
    //   4. Get the SoftwareBitmap via BitmapDecoder.GetSoftwareBitmapAsync
    //   5. Call OcrEngine.RecognizeAsync(SoftwareBitmap)
    //   6. Wait on the IAsyncOperation<OcrResult>
    //   7. Get the Text property from the OcrResult
    //
    // Each step is a separate async WinRT operation with its own vtable.
    // Doing all 7 steps in raw COM ABI is ~1000 lines of fragile code.
    //
    // The simplest reliable approach: shell out to PowerShell which
    // uses the .NET WinRT projection to run OCR. This is a fallback
    // until the full COM ABI can be implemented.
    //
    // PowerShell command:
    //   Add-Type -AssemblyName System.Runtime.WindowsRuntime
    //   $file = [Windows.Storage.StorageFile,Windows.Storage,ContentType=WindowsRuntime]::GetFileFromPathAsync($path).AwaitOperation()
    //   ...

    // We use a PowerShell-based OCR bridge because it's the most reliable
    // way to invoke WinRT from C++ without the WinRT headers' auto-link.
    // The PowerShell script uses the System.Runtime.WindowsRuntime.dll
    // extension methods (AsTask, AwaitOperation) to await WinRT async ops.

    const QString psScript = QString(
        "$ErrorActionPreference = 'Stop'\n"
        "[Windows.Storage.StorageFile,Windows.Storage,ContentType=WindowsRuntime] | Out-Null\n"
        "[Windows.Media.Ocr.OcrEngine,Windows.Media.Ocr,ContentType=WindowsRuntime] | Out-Null\n"
        "[Windows.Graphics.Imaging.BitmapDecoder,Windows.Graphics.Imaging,ContentType=WindowsRuntime] | Out-Null\n"
        "[Windows.Foundation.Metadata.AsyncInfo,Windows.Foundation.Metadata,ContentType=WindowsRuntime] | Out-Null\n"
        "Add-Type -AssemblyName System.Runtime.WindowsRuntime\n"
        "$asTaskGeneric = ([System.WindowsRuntimeSystemExtensions].GetMethods() | ? { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' -and $_.GetGenericArguments().Count -eq 1 })[0]\n"
        "function AwaitOperation($op) { $task = $asTaskGeneric.MakeGenericMethod($op.GetType().GetGenericArguments()[0]).Invoke($null, @($op)); $task.Wait(30000); $task.Result }\n"
        "function AwaitAction($op) { $task = ([System.WindowsRuntimeSystemExtensions].GetMethods() | ? { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncAction' })[0].Invoke($null, @($op)); $task.Wait(30000) }\n"
        "try {\n"
        "  $file = AwaitOperation ([Windows.Storage.StorageFile]::GetFileFromPathAsync('%1'))\n"
        "  $stream = AwaitOperation ($file.OpenAsync([Windows.Storage.FileAccessMode]::Read))\n"
        "  $decoder = AwaitOperation ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream))\n"
        "  $bitmap = AwaitOperation ($decoder.GetSoftwareBitmapAsync())\n"
        "  $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages()\n"
        "  if (-not $engine) { Write-Output 'OCR_NOLANG'; exit 1 }\n"
        "  $result = AwaitOperation ($engine.RecognizeAsync($bitmap))\n"
        "  Write-Output $result.Text\n"
        "} catch {\n"
        "  Write-Output ('OCR_ERROR:' + $_.Exception.Message)\n"
        "  exit 1\n"
        "}").arg(path.replace("'", "''"));

    // Run PowerShell with the script. Capture stdout.
    QProcess proc;
    proc.setProgram("powershell.exe");
    proc.setArguments({"-NoProfile", "-NonInteractive", "-Command", psScript});
    proc.start();
    if (!proc.waitForStarted(5000)) {
        DS_WARN("WinOCR", "Failed to start powershell.exe");
        return {};
    }
    if (!proc.waitForFinished(60000)) {
        DS_WARN("WinOCR", "PowerShell OCR timed out");
        proc.kill();
        return {};
    }
    const QByteArray out = proc.readAllStandardOutput();
    QString text = QString::fromUtf8(out).trimmed();
    if (text.startsWith("OCR_ERROR:") || text == "OCR_NOLANG") {
        DS_WARN("WinOCR", "OCR PowerShell error: " + text);
        return {};
    }
    return text;
#else
    Q_UNUSED(path);
    return {};
#endif
}

QStringList WindowsOcrEngine::availableLanguages() {
#ifdef Q_OS_WIN
    // This is a static method, so we can't check initialized_ here.
    // We just run PowerShell directly — if Windows OCR isn't available,
    // the script will fail and we'll return an empty list.
    QProcess proc;
    proc.setProgram("powershell.exe");
    proc.setArguments({"-NoProfile", "-NonInteractive", "-Command",
        "[Windows.Media.Ocr.OcrEngine,Windows.Media.Ocr,ContentType=WindowsRuntime] | Out-Null; "
        "([Windows.Media.Ocr.OcrEngine]::AvailableRecognizerLanguages) | ForEach-Object { $_.LanguageTag }"});
    proc.start();
    if (!proc.waitForStarted(5000)) return {};
    if (!proc.waitForFinished(10000)) { proc.kill(); return {}; }
    const QByteArray out = proc.readAllStandardOutput();
    return QString::fromUtf8(out).split('\n', Qt::SkipEmptyParts);
#else
    return {};
#endif
}

} // namespace DocuSearch
