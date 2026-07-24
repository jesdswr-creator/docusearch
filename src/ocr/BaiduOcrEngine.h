#pragma once

// ============================================================
// BaiduOcrEngine.h - Baidu Cloud OCR (unlimited, cloud-based)
// ============================================================
//
// Uses Baidu Cloud's "通用文字识别" (general text recognition) API
// for OCR. This is the "unlimited OCR by Baidu" engine.
//
// WHY THIS EXISTS:
//   oneocr.dll (the local engine) requires manual DLL extraction
//   from the Windows 11 Snipping Tool, supports only ~5 languages,
//   and has a license gray area. Baidu Cloud OCR has:
//     • 50+ languages including mixed Chinese/English
//     • Free 1000 calls/day quota (generous for desktop use)
//     • Higher quotas via Baidu Cloud paid plans (effectively unlimited)
//     • No DLL to bundle — pure HTTP/JSON
//
// Setup (one-time, in Settings → OCR):
//   1. User signs up at https://cloud.baidu.com
//   2. Creates an OCR application in the Baidu Cloud console
//   3. Copies the API Key + Secret Key into DocuSearch Settings
//
// Authentication flow (per Baidu docs):
//   • POST https://aip.baidubce.com/oauth/2.0/token
//     ?grant_type=client_credentials&client_id=KEY&client_secret=SECRET
//   • Returns access_token (valid for 30 days)
//   • Cache the token + expiry in QSettings
//   • On 401/403 from OCR endpoint: refresh the token + retry once
//
// OCR call:
//   • POST https://aip.baidubce.com/rest/2.0/ocr/v1/general_basic
//     ?access_token=TOKEN
//   • Body: image=<base64-encoded image bytes> & language_type=CHN_ENG
//   • Returns JSON: { words_result: [{words: "..."}, ...], words_result_num, log_id }
//
// If no API key configured → isConfigured() returns false → the caller
// (OcrWorkerPool) falls back to WindowsOcrEngine (oneocr).
// ============================================================

#include <QObject>
#include <QString>
#include <QImage>
#include <QDateTime>

class QNetworkAccessManager;
class QNetworkReply;
class QEventLoop;

namespace DocuSearch {

class BaiduOcrEngine {
public:
    BaiduOcrEngine();
    ~BaiduOcrEngine();

    BaiduOcrEngine(const BaiduOcrEngine&)            = delete;
    BaiduOcrEngine& operator=(const BaiduOcrEngine&) = delete;

    // Singleton accessor — used by status bar / Settings UI.
    static BaiduOcrEngine& instance();

    // Initialize. Always returns true (this engine doesn't need any
    // bundled files). The actual readiness check is isConfigured().
    bool init();

    // Shutdown hook (no-op — QNetworkAccessManager is parented to this).
    void shutdown() {}

    // True if the user has entered both API Key and Secret Key in Settings.
    bool isConfigured() const;

    // True if init() has been called.
    bool isInitialized() const { return initialized_; }

    // True if the most recent OCR call succeeded (proves credentials work).
    bool isFunctional() const { return functional_; }

    // Read/write credentials (stored in QSettings, NOT in AppSettings struct
    // — keeps the Baidu dependency fully isolated to this class).
    QString apiKey() const;
    QString secretKey() const;
    void setCredentials(const QString& apiKey, const QString& secretKey);
    void clearCredentials();

    // OCR an image file. Returns recognized text (UTF-8) or empty on failure.
    // Thread-safety: safe to call from worker threads — each call uses its
    // own QNetworkAccessManager + QEventLoop (no shared state mutations).
    QString ocrFile(const QString& path);

    // OCR a QImage. Saves to a temp PNG, then calls ocrFile().
    QString ocrImage(const QImage& img);

    // Test the configured credentials by fetching an access token.
    // Returns true on success, false on failure (bad key, network error).
    // Synchronous — call from a worker thread.
    bool testConnection(QString* errorMessage = nullptr);

private:
    // Fetch (or refresh) the OAuth access token. Cached in QSettings.
    // Returns true on success.
    bool ensureAccessToken(QString* errorMessage = nullptr);

    // Refresh the token unconditionally (used when OCR returns 401/403).
    bool refreshAccessToken(QString* errorMessage = nullptr);

    // Read cached token + expiry from QSettings. Returns false if expired.
    bool loadCachedToken(QString* token, qint64* expiryEpoch) const;
    void saveCachedToken(const QString& token, qint64 expiryEpoch) const;
    void clearCachedToken() const;

    bool   initialized_ = false;
    bool   functional_  = false;
    QString apiKey_;
    QString secretKey_;
};

} // namespace DocuSearch
