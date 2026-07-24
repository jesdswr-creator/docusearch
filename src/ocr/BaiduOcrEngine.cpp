// ============================================================
// BaiduOcrEngine.cpp - Baidu Cloud OCR implementation
// ============================================================
//
// All HTTP calls are SYNCHRONOUS (blocking) via QEventLoop. This is
// intentional: BaiduOcrEngine is called from worker threads in
// OcrWorkerPool, which are allowed to block. The main thread never
// calls these methods.
// ============================================================

#include "BaiduOcrEngine.h"
#include "../core/Logger.h"
#include "../core/Config.h"

#include <QCoreApplication>
#include <QSettings>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QEventLoop>
#include <QTimer>
#include <QImage>
#include <QFileInfo>
#include <QFile>
#include <QByteArray>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>
#include <QDir>

namespace DocuSearch {

namespace {
// Baidu Cloud OCR endpoints.
constexpr const char* kTokenUrl   = "https://aip.baidubce.com/oauth/2.0/token";
constexpr const char* kOcrUrl     = "https://aip.baidubce.com/rest/2.0/ocr/v1/general_basic";
constexpr int         kTokenRefreshLeadSeconds = 300;   // refresh 5min before expiry
constexpr int         kHttpTimeoutMs           = 30000; // 30s per request

// QSettings keys (under "BaiduOcr/" prefix).
const QString kKeyApiKey       = "BaiduOcr/apiKey";
const QString kKeySecretKey    = "BaiduOcr/secretKey";
const QString kKeyCachedToken  = "BaiduOcr/cachedToken";
const QString kKeyTokenExpiry  = "BaiduOcr/tokenExpiryEpoch";

// Simple synchronous HTTP POST. Returns the response body.
// On timeout or network error, returns empty QByteArray + sets *ok=false.
QByteArray httpPostSync(const QUrl& url,
                        const QByteArray& bodyData,
                        const char* contentType = "application/x-www-form-urlencoded",
                        bool* ok = nullptr,
                        int* httpStatus = nullptr) {
    if (ok) *ok = false;
    if (httpStatus) *httpStatus = 0;

    QNetworkAccessManager nam;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    req.setRawHeader("User-Agent", "DocuSearch/1.0");

    QNetworkReply* reply = nam.post(req, bodyData);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    // Timeout safety.
    QTimer::singleShot(kHttpTimeoutMs, &loop, &QEventLoop::quit);
    loop.exec();

    if (!reply->isFinished()) {
        // Timed out.
        reply->abort();
        reply->deleteLater();
        return QByteArray();
    }

    QVariant statusVar = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (httpStatus && statusVar.isValid()) {
        *httpStatus = statusVar.toInt();
    }

    QByteArray resp = reply->readAll();
    QNetworkReply::NetworkError err = reply->error();
    reply->deleteLater();

    if (err == QNetworkReply::NoError) {
        if (ok) *ok = true;
    }
    // Note: we return the body even on HTTP error — caller may want to
    // inspect the error message in the JSON response.
    return resp;
}

// Simple synchronous HTTP GET (used for token endpoint via POST actually).
QByteArray httpPostFormSync(const QUrl& url,
                            const QUrlQuery& params,
                            bool* ok = nullptr,
                            int* httpStatus = nullptr) {
    QByteArray body = params.toString(QUrl::FullyEncoded).toUtf8();
    return httpPostSync(url, body, "application/x-www-form-urlencoded",
                        ok, httpStatus);
}
} // namespace

// ============================================================
// Construction / initialization
// ============================================================

BaiduOcrEngine::BaiduOcrEngine() = default;
BaiduOcrEngine::~BaiduOcrEngine() = default;

BaiduOcrEngine& BaiduOcrEngine::instance() {
    static BaiduOcrEngine inst;
    static bool initialized = false;
    if (!initialized) {
        inst.init();
        initialized = true;
    }
    return inst;
}

bool BaiduOcrEngine::init() {
    // Read credentials from QSettings (kept here so the Baidu dependency
    // doesn't leak into AppSettings struct).
    QSettings s;
    s.beginGroup("BaiduOcr");
    apiKey_  = s.value("apiKey").toString();
    secretKey_ = s.value("secretKey").toString();
    s.endGroup();

    initialized_ = true;
    DS_INFO("OCR", QString("BaiduOcrEngine init — apiKey present: %1, secretKey present: %2")
             .arg(!apiKey_.isEmpty()).arg(!secretKey_.isEmpty()));
    return true;
}

// ============================================================
// Credential management
// ============================================================

bool BaiduOcrEngine::isConfigured() const {
    return !apiKey_.isEmpty() && !secretKey_.isEmpty();
}

QString BaiduOcrEngine::apiKey() const {
    if (!apiKey_.isEmpty()) return apiKey_;
    QSettings s;
    return s.value(kKeyApiKey).toString();
}

QString BaiduOcrEngine::secretKey() const {
    if (!secretKey_.isEmpty()) return secretKey_;
    QSettings s;
    return s.value(kKeySecretKey).toString();
}

void BaiduOcrEngine::setCredentials(const QString& apiKey, const QString& secretKey) {
    apiKey_   = apiKey.trimmed();
    secretKey_ = secretKey.trimmed();

    QSettings s;
    s.setValue(kKeyApiKey,    apiKey_);
    s.setValue(kKeySecretKey, secretKey_);

    // Invalidate cached token — new credentials may belong to a different account.
    clearCachedToken();
    functional_ = false;

    DS_INFO("OCR", "Baidu OCR credentials updated (cached token cleared).");
}

void BaiduOcrEngine::clearCredentials() {
    apiKey_.clear();
    secretKey_.clear();

    QSettings s;
    s.remove(kKeyApiKey);
    s.remove(kKeySecretKey);
    clearCachedToken();
    functional_ = false;

    DS_INFO("OCR", "Baidu OCR credentials cleared.");
}

// ============================================================
// OAuth access token management
// ============================================================

bool BaiduOcrEngine::loadCachedToken(QString* token, qint64* expiryEpoch) const {
    QSettings s;
    QString t = s.value(kKeyCachedToken).toString();
    qint64 exp = s.value(kKeyTokenExpiry).toLongLong();
    if (t.isEmpty() || exp == 0) return false;

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (now >= (exp - kTokenRefreshLeadSeconds)) {
        // Token expired or about to expire.
        return false;
    }
    *token = t;
    if (expiryEpoch) *expiryEpoch = exp;
    return true;
}

void BaiduOcrEngine::saveCachedToken(const QString& token, qint64 expiryEpoch) const {
    QSettings s;
    s.setValue(kKeyCachedToken, token);
    s.setValue(kKeyTokenExpiry, (qlonglong)expiryEpoch);
}

void BaiduOcrEngine::clearCachedToken() const {
    QSettings s;
    s.remove(kKeyCachedToken);
    s.remove(kKeyTokenExpiry);
}

bool BaiduOcrEngine::refreshAccessToken(QString* errorMessage) {
    if (!isConfigured()) {
        if (errorMessage) *errorMessage = "API key and secret key are not set";
        return false;
    }

    QUrl url(QString::fromUtf8(kTokenUrl));
    QUrlQuery q;
    q.addQueryItem("grant_type", "client_credentials");
    q.addQueryItem("client_id", apiKey_);
    q.addQueryItem("client_secret", secretKey_);

    bool ok = false;
    int httpStatus = 0;
    QByteArray resp = httpPostFormSync(url, q, &ok, &httpStatus);
    if (resp.isEmpty()) {
        if (errorMessage) *errorMessage = "Network error (empty response)";
        DS_WARN("OCR", "Baidu token request: empty response");
        return false;
    }

    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(resp, &parseErr);
    if (parseErr.error != QJsonParseError::NoError) {
        if (errorMessage) *errorMessage = "Invalid JSON in token response";
        DS_WARN("OCR", QString("Baidu token JSON parse error: %1").arg(parseErr.errorString()));
        return false;
    }
    QJsonObject obj = doc.object();

    if (obj.contains("error")) {
        QString err = obj.value("error").toString();
        QString desc = obj.value("error_description").toString();
        if (errorMessage) *errorMessage = QString("Baidu: %1 — %2").arg(err, desc);
        DS_WARN("OCR", QString("Baidu token error: %1 — %2").arg(err, desc));
        return false;
    }

    QString token = obj.value("access_token").toString();
    qint64 expiresIn = (qint64)obj.value("expires_in").toDouble();
    if (token.isEmpty() || expiresIn == 0) {
        if (errorMessage) *errorMessage = "Missing access_token or expires_in";
        return false;
    }

    qint64 expiry = QDateTime::currentSecsSinceEpoch() + expiresIn;
    saveCachedToken(token, expiry);

    DS_INFO("OCR", QString("Baidu access token refreshed — valid for %1 seconds")
             .arg(expiresIn));
    return true;
}

bool BaiduOcrEngine::ensureAccessToken(QString* errorMessage) {
    QString cached;
    qint64 expiry = 0;
    if (loadCachedToken(&cached, &expiry)) {
        // Still valid.
        if (errorMessage) *errorMessage = QString();
        return true;
    }
    return refreshAccessToken(errorMessage);
}

// ============================================================
// OCR
// ============================================================

QString BaiduOcrEngine::ocrImage(const QImage& img) {
    if (!isConfigured()) return QString();
    if (img.isNull()) return QString();

    const QString tempPath = QDir::tempPath() + "/docusearch_baidu_ocr_" +
        QString::number(QDateTime::currentMSecsSinceEpoch()) + ".png";
    if (!img.save(tempPath, "PNG")) return QString();
    const QString text = ocrFile(tempPath);
    QFile::remove(tempPath);
    return text;
}

QString BaiduOcrEngine::ocrFile(const QString& path) {
    if (!isConfigured()) return QString();
    if (!QFileInfo::exists(path)) return QString();

    // 1. Read file + base64-encode.
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        DS_WARN("OCR", QString("Baidu: cannot open %1").arg(path));
        return QString();
    }
    QByteArray imageBytes = f.readAll();
    f.close();

    // Cap at 10MB (Baidu limit is 6MB for general_basic; we conservatively
    // reject anything > 6MB before sending).
    if (imageBytes.size() > 6 * 1024 * 1024) {
        DS_WARN("OCR", QString("Baidu: image too large (%1 bytes) — max 6MB")
                 .arg(imageBytes.size()));
        return QString();
    }

    QByteArray imageB64 = imageBytes.toBase64();

    // 2. Ensure we have a valid access token.
    QString errMsg;
    if (!ensureAccessToken(&errMsg)) {
        DS_WARN("OCR", QString("Baidu: cannot get access token — %1").arg(errMsg));
        return QString();
    }

    // 3. POST to OCR endpoint.
    // Try up to 2 times: if the first attempt returns 401/110 (access token
    // invalid), refresh the token + retry once.
    for (int attempt = 0; attempt < 2; ++attempt) {
        QSettings s;
        QString token = s.value(kKeyCachedToken).toString();
        if (token.isEmpty()) {
            DS_WARN("OCR", "Baidu: cached token vanished mid-request");
            return QString();
        }

        QUrl url(QString::fromUtf8(kOcrUrl));
        QUrlQuery q;
        q.addQueryItem("access_token", token);
        url.setQuery(q);

        // Body: image=<base64> & language_type=CHN_ENG & detect_direction=true
        QByteArray body;
        body.append("image=");
        body.append(QUrl::toPercentEncoding(QString::fromUtf8(imageB64)));
        body.append("&language_type=CHN_ENG");
        body.append("&detect_direction=true");
        body.append("&detect_language=true");
        body.append("&paragraph=false");
        body.append("&probability=false");

        bool ok = false;
        int httpStatus = 0;
        QByteArray resp = httpPostSync(url, body,
                                        "application/x-www-form-urlencoded",
                                        &ok, &httpStatus);

        if (resp.isEmpty()) {
            DS_WARN("OCR", "Baidu OCR: empty response (network error or timeout)");
            return QString();
        }

        QJsonParseError parseErr;
        QJsonDocument doc = QJsonDocument::fromJson(resp, &parseErr);
        if (parseErr.error != QJsonParseError::NoError) {
            DS_WARN("OCR", QString("Baidu OCR: JSON parse error — %1")
                     .arg(parseErr.errorString()));
            return QString();
        }
        QJsonObject obj = doc.object();

        // Check for auth errors → refresh token + retry.
        if (obj.contains("error_code")) {
            int code = (int)obj.value("error_code").toDouble();
            QString msg = obj.value("error_msg").toString();

            // 110 = access token invalid, 111 = expired.
            if ((code == 110 || code == 111) && attempt == 0) {
                DS_INFO("OCR", QString("Baidu OCR: token invalid (%1) — refreshing")
                         .arg(code));
                if (refreshAccessToken(&errMsg)) {
                    continue;  // retry with new token
                } else {
                    DS_WARN("OCR", QString("Baidu OCR: token refresh failed — %1").arg(errMsg));
                    return QString();
                }
            }
            DS_WARN("OCR", QString("Baidu OCR error %1: %2").arg(code).arg(msg));
            return QString();
        }

        // 4. Parse words_result array.
        QJsonArray words = obj.value("words_result").toArray();
        if (words.isEmpty()) {
            // Could be a successful but empty result, or an unrecognized
            // error shape. Either way, return empty text.
            DS_INFO("OCR", "Baidu OCR: no words in response");
            return QString();
        }

        QString text;
        for (const QJsonValue& v : words) {
            QJsonObject line = v.toObject();
            QString words_str = line.value("words").toString();
            if (!text.isEmpty()) text += "\n";
            text += words_str;
        }

        if (!text.isEmpty()) {
            if (!functional_) {
                DS_INFO("OCR", "Baidu OCR succeeded — marking engine functional.");
            }
            functional_ = true;
        }
        return text;
    }

    return QString();
}

// ============================================================
// Test connection (used by Settings → OCR → Test button)
// ============================================================

bool BaiduOcrEngine::testConnection(QString* errorMessage) {
    if (!isConfigured()) {
        if (errorMessage) *errorMessage = "API key and secret key are not set";
        return false;
    }
    // Force-refresh the token. If it works, the credentials are valid.
    clearCachedToken();
    return refreshAccessToken(errorMessage);
}

} // namespace DocuSearch
