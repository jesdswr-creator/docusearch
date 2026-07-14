// ============================================================
// WindowsOcrEngine.cpp - OCR via Windows.Media.Ocr (PowerShell)
// ============================================================
//
// Uses Windows' built-in OCR engine via PowerShell. This approach:
// - Does NOT crash the main app (runs in a separate process)
// - Does NOT require Python or external downloads
// - Works on Windows 10 v1903+ and Windows 11
// - Uses the OCR languages installed in Windows Settings
//
// For PDFs, each page is rendered to an image via Poppler, then
// OCR'd via PowerShell + Windows.Media.Ocr.
// ============================================================

#include "WindowsOcrEngine.h"
#include "../core/Logger.h"

#include <QImage>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QDateTime>

namespace DocuSearch {

WindowsOcrEngine::WindowsOcrEngine() = default;
WindowsOcrEngine::~WindowsOcrEngine() = default;

bool WindowsOcrEngine::init() {
#ifdef Q_OS_WIN
    // Check if Windows OCR is available by testing PowerShell.
    QProcess proc;
    proc.setProgram("powershell.exe");
    proc.setArguments({"-NoProfile", "-NonInteractive", "-Command",
        "[Windows.Media.Ocr.OcrEngine,Windows.Media.Ocr,ContentType=WindowsRuntime] | Out-Null; "
        "$e = [Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages(); "
        "if ($e) { Write-Output 'OK' } else { Write-Output 'NOLANG' }"});
    proc.start();
    if (!proc.waitForStarted(5000)) return false;
    if (!proc.waitForFinished(10000)) { proc.kill(); return false; }
    const QByteArray out = proc.readAllStandardOutput().trimmed();
    if (out == "OK") {
        initialized_ = true;
        return true;
    }
    DS_WARN("OCR", "Windows OCR not available: " + QString::fromUtf8(out));
    return false;
#else
    return false;
#endif
}

QString WindowsOcrEngine::ocrImage(const QImage& img) {
    if (!initialized_) return {};
    if (img.isNull()) return {};

    const QString tempPath = QDir::tempPath() + "/docusearch_ocr_" +
        QString::number(QDateTime::currentMSecsSinceEpoch()) + ".png";
    if (!img.save(tempPath, "PNG")) return {};
    const QString text = ocrFile(tempPath);
    QFile::remove(tempPath);
    return text;
}

QString WindowsOcrEngine::ocrFile(const QString& path) {
#ifdef Q_OS_WIN
    if (!initialized_) return {};
    if (!QFileInfo::exists(path)) return {};

    // PowerShell script that OCRs an image file using Windows.Media.Ocr.
    // Runs in a SEPARATE PROCESS — if it crashes, the main app is safe.
    const QString psScript = QString(
        "$ErrorActionPreference = 'Stop'\n"
        "[Windows.Storage.StorageFile,Windows.Storage,ContentType=WindowsRuntime] | Out-Null\n"
        "[Windows.Media.Ocr.OcrEngine,Windows.Media.Ocr,ContentType=WindowsRuntime] | Out-Null\n"
        "[Windows.Graphics.Imaging.BitmapDecoder,Windows.Graphics.Imaging,ContentType=WindowsRuntime] | Out-Null\n"
        "Add-Type -AssemblyName System.Runtime.WindowsRuntime\n"
        "$asTaskGeneric = ([System.WindowsRuntimeSystemExtensions].GetMethods() | ? { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' -and $_.GetGenericArguments().Count -eq 1 })[0]\n"
        "function Await($op) { $t = $asTaskGeneric.MakeGenericMethod($op.GetType().GetGenericArguments()[0]).Invoke($null, @($op)); $t.Wait(30000); $t.Result }\n"
        "try {\n"
        "  $f = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync('%1'))\n"
        "  $s = Await ($f.OpenAsync([Windows.Storage.FileAccessMode]::Read))\n"
        "  $d = Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($s))\n"
        "  $b = Await ($d.GetSoftwareBitmapAsync())\n"
        "  $e = [Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages()\n"
        "  if (-not $e) { exit 1 }\n"
        "  $r = Await ($e.RecognizeAsync($b))\n"
        "  Write-Output $r.Text\n"
        "} catch { exit 1 }\n"
    ).arg(QString(path).replace("'", "''"));

    QProcess proc;
    proc.setProgram("powershell.exe");
    proc.setArguments({"-NoProfile", "-NonInteractive", "-Command", psScript});
    proc.start();
    if (!proc.waitForStarted(5000)) return {};
    if (!proc.waitForFinished(60000)) { proc.kill(); return {}; }
    return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
#else
    Q_UNUSED(path);
    return {};
#endif
}

QStringList WindowsOcrEngine::availableLanguages() {
#ifdef Q_OS_WIN
    QProcess proc;
    proc.setProgram("powershell.exe");
    proc.setArguments({"-NoProfile", "-NonInteractive", "-Command",
        "[Windows.Media.Ocr.OcrEngine,Windows.Media.Ocr,ContentType=WindowsRuntime] | Out-Null; "
        "([Windows.Media.Ocr.OcrEngine]::AvailableRecognizerLanguages) | ForEach-Object { $_.LanguageTag }"});
    proc.start();
    if (!proc.waitForStarted(5000)) return {};
    if (!proc.waitForFinished(10000)) { proc.kill(); return {}; }
    return QString::fromUtf8(proc.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
#else
    return {};
#endif
}

} // namespace DocuSearch
