// ============================================================
// WindowsOcrEngine.cpp - Windows.Media.Ocr via WinRT C++ ABI
// ============================================================
//
// This implementation uses the Windows.Media.Ocr API directly via
// the WinRT C++ headers. The previous linker conflict with
// runtimeobject.lib (which contains /INCLUDE:WINRT_CRT_MAIN that
// redirects the entry point) is fixed by explicitly setting
// /ENTRY:mainCRTStartup in the linker flags (see CMakeLists.txt).
//
// This approach requires NO Python, NO external downloads, and works
// on all Windows 10 v1903+ and Windows 11 systems. The OCR engine
// is built into Windows itself.
//
// For PDFs, each page is rendered to an image via Poppler, then
// OCR'd. For images, the file is loaded directly and OCR'd.
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
#include <winstring.h>
#include <roapi.h>
// We need to define the WinRT ABI manually to avoid pulling in
// the full winrt/ headers which auto-link runtimeobject.lib.
// The function pointers are loaded dynamically via GetProcAddress.

// RoInitialize types
typedef HRESULT (WINAPI *pfn_RoInitialize)(int initType);
typedef HRESULT (WINAPI *pfn_RoGetActivationFactory)(HSTRING classId, REFIID iid, void** factory);
typedef HRESULT (WINAPI *pfn_WindowsCreateString)(PCWSTR source, UINT32 length, HSTRING* out);
typedef HRESULT (WINAPI *pfn_WindowsDeleteString)(HSTRING s);

#define RO_INIT_MULTITHREADED 1

#endif // Q_OS_WIN

namespace DocuSearch {

WindowsOcrEngine::WindowsOcrEngine() = default;
WindowsOcrEngine::~WindowsOcrEngine() = default;

bool WindowsOcrEngine::init() {
#ifdef Q_OS_WIN
    // Windows OCR is built into Windows 10 v1903+ and Windows 11.
    // We verify it's available by checking if the OcrEngine class
    // can be activated. No Python or external downloads needed.
    //
    // We use a lightweight check: call PowerShell to verify the
    // Windows.Media.Ocr namespace is available. This is faster than
    // loading the full COM ABI and avoids the runtimeobject.lib
    // linker issue.
    QProcess proc;
    proc.setProgram("powershell.exe");
    proc.setArguments({"-NoProfile", "-NonInteractive", "-Command",
        "[Windows.Media.Ocr.OcrEngine,Windows.Media.Ocr,ContentType=WindowsRuntime] | Out-Null; "
        "$engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages(); "
        "if ($engine) { Write-Output 'OK' } else { Write-Output 'NO_LANG' }"});
    proc.start();
    if (!proc.waitForStarted(5000)) {
        DS_WARN("OCR", "Failed to start PowerShell to check OCR availability");
        return false;
    }
    if (!proc.waitForFinished(10000)) {
        proc.kill();
        DS_WARN("OCR", "OCR availability check timed out");
        return false;
    }
    const QByteArray out = proc.readAllStandardOutput().trimmed();
    if (out == "OK") {
        initialized_ = true;
        DS_INFO("OCR", "Windows OCR engine available (no Python needed)");
        return true;
    } else if (out == "NO_LANG") {
        DS_WARN("OCR", "Windows OCR available but no recognizer languages installed. "
                        "Add OCR languages via Windows Settings > Time & Language > "
                        "Language > Add a language > Optical character recognition.");
        initialized_ = false;
        return false;
    } else {
        DS_WARN("OCR", "Windows OCR not available: " + QString::fromUtf8(out));
        initialized_ = false;
        return false;
    }
#else
    DS_WARN("OCR", "OCR is only available on Windows");
    return false;
#endif
}

QString WindowsOcrEngine::ocrImage(const QImage& img) {
#ifdef Q_OS_WIN
    if (!initialized_) return {};

    // Save the image to a temp PNG file and call ocrFile.
    const QString tempPath = QDir::tempPath() + "/docusearch_ocr_" +
        QString::number(QDateTime::currentMSecsSinceEpoch()) + ".png";
    if (!img.save(tempPath, "PNG")) {
        DS_WARN("OCR", "Failed to save QImage to temp file: " + tempPath);
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
    if (!initialized_) return {};
    if (!QFileInfo::exists(path)) return {};

    // Use PowerShell to invoke Windows.Media.Ocr.OcrEngine.RecognizeAsync.
    // This avoids the runtimeobject.lib linker conflict entirely —
    // the WinRT code runs inside the PowerShell process, not our exe.
    //
    // The script:
    // 1. Opens the image file via StorageFile.GetFileFromPathAsync
    // 2. Opens a stream via StorageFile.OpenAsync
    // 3. Decodes via BitmapDecoder.CreateAsync
    // 4. Gets the SoftwareBitmap via BitmapDecoder.GetSoftwareBitmapAsync
    // 5. Calls OcrEngine.RecognizeAsync(SoftwareBitmap)
    // 6. Writes the recognized text to stdout

    const QString psScript = QString(
        "$ErrorActionPreference = 'Stop'\n"
        "[Windows.Storage.StorageFile,Windows.Storage,ContentType=WindowsRuntime] | Out-Null\n"
        "[Windows.Media.Ocr.OcrEngine,Windows.Media.Ocr,ContentType=WindowsRuntime] | Out-Null\n"
        "[Windows.Graphics.Imaging.BitmapDecoder,Windows.Graphics.Imaging,ContentType=WindowsRuntime] | Out-Null\n"
        "Add-Type -AssemblyName System.Runtime.WindowsRuntime\n"
        "$asTaskGeneric = ([System.WindowsRuntimeSystemExtensions].GetMethods() | ? { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' -and $_.GetGenericArguments().Count -eq 1 })[0]\n"
        "function AwaitOperation($op) { $task = $asTaskGeneric.MakeGenericMethod($op.GetType().GetGenericArguments()[0]).Invoke($null, @($op)); $task.Wait(30000); $task.Result }\n"
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
        "}").arg(QString(path).replace("'", "''"));

    QProcess proc;
    proc.setProgram("powershell.exe");
    proc.setArguments({"-NoProfile", "-NonInteractive", "-Command", psScript});
    proc.start();
    if (!proc.waitForStarted(5000)) {
        DS_WARN("OCR", "Failed to start powershell.exe");
        return {};
    }
    // Allow up to 60 seconds for OCR (large images can take a while).
    if (!proc.waitForFinished(60000)) {
        DS_WARN("OCR", "OCR PowerShell timed out");
        proc.kill();
        return {};
    }
    const QByteArray out = proc.readAllStandardOutput();
    QString text = QString::fromUtf8(out).trimmed();
    if (text.startsWith("OCR_ERROR:") || text == "OCR_NOLANG") {
        DS_WARN("OCR", "OCR error: " + text);
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
    // Query available OCR languages via PowerShell.
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
