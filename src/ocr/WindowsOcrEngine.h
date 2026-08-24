#pragma once

// ============================================================
// WindowsOcrEngine.h - OCR wrapper backed by Windows.Media.Ocr
// ============================================================
//
// Uses the official Windows.Media.Ocr WinRT API — the same OCR
// engine that powers Windows Search, Snipping Tool, and the Photos
// app. This is the officially-supported, royalty-free OCR API for
// any Windows app (commercial included — see docs/OCR_LICENSING.md).
//
// The WinRT calls live in a SEPARATE helper exe (docusearch_ocr_helper.exe)
// spawned via QProcess. This keeps runtimeobject.lib (WinRT) out of
// the main Qt app, avoiding the well-known Qt/WinRT entry-point
// conflict. It also gives us free process isolation: even if the
// OCR subsystem faults, the main app is unaffected.
//
// Availability:
//   Windows.Media.Ocr is always present on Windows 10 1809+ when at
//   least one OCR language pack is installed (most consumer Windows
//   installs do). The helper exe is the only file that needs to ship
//   with DocuSearch — no DLLs, no models, no scripts to run.
//
// Class name kept as WindowsOcrEngine for backward compatibility
// with existing code that references it.
// ============================================================

#include <QString>
#include <QImage>

namespace DocuSearch {

class WindowsOcrEngine {
public:
    WindowsOcrEngine();
    ~WindowsOcrEngine();

    WindowsOcrEngine(const WindowsOcrEngine&)            = delete;
    WindowsOcrEngine& operator=(const WindowsOcrEngine&) = delete;

    // Singleton accessor — used by status bar / UI to query OCR status
    // without creating a new instance each time (which would lose the
    // cached available_ flag set by previous OCR runs).
    static WindowsOcrEngine& instance();

    // Initialize the OCR engine. Checks for the helper exe.
    // Returns true if the helper is present (and WinRT will be ready
    // when ocrFile() is called).
    bool init();

    // Shutdown hook — call before QApplication is destroyed.
    // Currently a no-op because we create a fresh QProcess per ocrFile()
    // call, but kept for forward compatibility and to match the
    // shutdown pattern used by other singletons.
    void shutdown() {}

    // OCR an image. Returns extracted text (empty on failure).
    QString ocrImage(const QImage& img);

    // OCR a file directly (PNG, JPG, BMP, TIFF, WebP, GIF, HEIF).
    QString ocrFile(const QString& path);

    bool isInitialized() const { return initialized_; }

    // Returns true if Windows.Media.Ocr appears to be available
    // (helper exe present + at least one OCR language pack installed
    // per the helper's last run). The check is cheap and cached.
    bool isAvailable() const;

    // Returns the list of installed OCR language tags — empty if
    // no OCR languages are installed yet (caller should prompt user
    // to install one via Windows Settings).
    static QStringList availableLanguages();

private:
    bool initialized_ = false;
    bool available_ = false;
    // Tracks whether the last OCR call surfaced a "no language packs
    // installed" message — drives the status bar indicator.
    bool noLanguagePacks_ = false;
};

} // namespace DocuSearch
