// ============================================================
// OcrWorkerPool.cpp
// ============================================================

#include "OcrWorkerPool.h"
#include "WindowsOcrEngine.h"
#include "../core/Logger.h"
#include "../core/FileUtils.h"
#include "../core/StringUtils.h"
#include "../core/Constants.h"
#include "../core/SehTranslator.h"
#include "../core/TextQuality.h"

#ifdef DOCUSEARCH_HAS_PDFIUM
#  include "../pdf/PdfiumDocument.h"
#endif

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <psapi.h>
#endif

#include <QImage>
#include <QFileInfo>
#include <QTimer>
#include <QDateTime>
#include <QThread>
#include <QElapsedTimer>
#include <QTransform>
#include <algorithm>

namespace DocuSearch {

namespace {

// v1.7.10: quality metric used to judge OCR during auto-orientation.
// Sum of lengths of runs of >= 3 letter/number characters. Real text
// at the correct orientation produces many 3+ character words, so the
// score climbs fast; a SIDEWAYS page read by Windows OCR yields
// scattered 1-2 character fragments that score near zero. The old
// word-count metric (< 3 two-char runs) treated that sideways junk as
// "we already read words" and never tried the rotated passes, which is
// exactly why rotated PDFs came back empty or garbled.
//
// v1.7.16: the metric alone is NOT enough. Measured on the reference
// scan ("Minhaj Pay", dense sideways memorandum): the sideways junk
// scored 1234 vs 1399 upright - a 13% gap no fixed threshold can
// separate, because tables/numbers/stamps OCR into long-ish junk runs
// at ANY orientation. TextQuality::assessOcrText now also reports
// wordRate (share of tokens that are common words, 0.39 upright vs
// ~0.05 sideways on that scan) and the two metrics below combine them.
// The old local run-score helper lives on as OcrTextStats::runScore.

// Below this run score the upright OCR is treated as "the page is
// probably stored sideways" and the rotated passes are tried.
// 48 chars in 3+ letter runs is roughly a dozen real words - a nearly
// blank page scores 0-10, sideways junk scores < 20, any real text
// page scores hundreds.
constexpr int kMinUprightOcrScore = 48;

// v1.7.16: a Latin-dominant OCR text with at least this many tokens
// and a word rate below this is fragment junk even when its run score
// is high. Real upright text measures 0.15-0.40; sideways fragments
// land near 0. Generous on purpose: a false trigger only costs up to
// three extra OCR passes (the best candidate still wins), while a
// missed trigger leaves rotated scans permanently unreadable.
constexpr double kMinOcrWordRate = 0.10;

// v1.7.16: rank an orientation candidate. Higher = keep.
//   - Latin-dominant text with enough tokens: word rate dominates
//     (1% of real words is worth 120 run-score points, capped so a
//     wall of junk runs can never out-rank word-bearing text). This
//     is what makes a correctly-oriented short page (few words)
//     still beat a dense sideways fragment page.
//   - Non-Latin text (CJK, Devanagari, ...) or too little signal:
//     fall back to the v1.7.10 run score alone.
int ocrCandidateRank(const TextQuality::OcrTextStats& q) {
    if (!q.latinDominant || q.tokens < 12) return q.runScore;
    return int(q.wordRate * 12000.0) + std::min(q.runScore, 2000);
}

// v1.7.16: should the rotated passes run for this upright result?
bool ocrLooksMisoriented(const TextQuality::OcrTextStats& q) {
    if (q.runScore < kMinUprightOcrScore) return true;   // near-empty
    return q.latinDominant && q.tokens >= 12 &&
           q.wordRate < kMinOcrWordRate;                 // fragment junk
}

// v1.7.16: early-exit once a rotation candidate reads as real text -
// no later rotation (all wrong by construction once one is right)
// can do better, so skip the remaining passes.
bool ocrLooksSolid(const TextQuality::OcrTextStats& q) {
    return q.runScore >= kMinUprightOcrScore &&
           (!q.latinDominant || q.tokens < 12 ||
            q.wordRate >= kMinOcrWordRate);
}

// v1.7.10: Windows.Media.Ocr accuracy degrades past ~2600 px per side
// (OcrEngine::MaxImageDimension). Scans rendered at 150 DPI A4 are
// 1240x1754 — fine — but phone photos can be 4000+ px. Bound the long
// side before OCR so quality (and the auto-orientation verdicts built
// on it) stay consistent.
constexpr int kMaxOcrImageSide = 2600;

QImage prepareForOcr(QImage img) {
    if (img.isNull()) return img;
    const int longSide = qMax(img.width(), img.height());
    if (longSide > kMaxOcrImageSide) {
        img = img.scaled(kMaxOcrImageSide, kMaxOcrImageSide,
                         Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return img;
}

// Read system-wide CPU usage as a percentage (0-100). Returns -1 if unavailable.
int readSystemCpuUsage() {
#ifdef Q_OS_WIN
    // We sample total system CPU via GetSystemTimes (idle vs total).
    static FILETIME prevIdle{}, prevKernel{}, prevUser{};
    FILETIME idle, kernel, user;
    if (!GetSystemTimes(&idle, &kernel, &user)) return -1;
    auto toU64 = [](const FILETIME& ft){
        return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    };
    const ULONGLONG id = toU64(idle),     ker = toU64(kernel),    usr = toU64(user);
    const ULONGLONG pid = toU64(prevIdle), pk = toU64(prevKernel), pu = toU64(prevUser);
    const ULONGLONG sys = (ker - pk) + (usr - pu);
    const ULONGLONG total = sys + (id - pid);
    prevIdle = idle; prevKernel = kernel; prevUser = user;
    if (total == 0) return -1;
    return static_cast<int>(100.0 * (1.0 - double(id - pid) / double(total)));
#else
    return 30; // mock on Linux dev
#endif
}

// Sleep a worker thread for a small slice based on CPU target.
void throttledSleep(int targetPct, int actualPct) {
    // If actual CPU > target, sleep proportionally to throttle down.
    if (actualPct <= 0 || actualPct <= targetPct) return;
    // e.g., target 30%, actual 70% -> sleep (40/70)*100 ms per 100 ms work
    int over = actualPct - targetPct;
    int sleepMs = std::min(150, over * 2);
    QThread::msleep(sleepMs);
}
} // namespace

class OcrWorkerThread : public QThread {
public:
    OcrWorkerThread(OcrWorkerPool* pool, int id)
        : QThread(pool), pool_(pool), id_(id) {
        setObjectName(QString("OcrWorker-%1").arg(id));
    }
    void run() override { pool_->workerLoop(id_); }
private:
    OcrWorkerPool* pool_;
    int id_;
};

OcrWorkerPool::OcrWorkerPool(int numThreads, QObject* parent)
    : QObject(parent), numThreads_(std::clamp(numThreads, 1, 16)) {
    spawnWorkers();
}

OcrWorkerPool::~OcrWorkerPool() {
    shutdown();
}

void OcrWorkerPool::setAppSettings(const AppSettings& s) {
    settings_ = s;
}

void OcrWorkerPool::spawnWorkers() {
    for (int i = 0; i < numThreads_; ++i) {
        auto t = std::make_unique<OcrWorkerThread>(this, i);
        t->start();
        threads_.push_back(std::move(t));
    }
}

void OcrWorkerPool::enqueue(const OcrTask& t) {
    {
        QMutexLocker lk(&queueMutex_);
        queue_.enqueue(t);
    }
    enqueuedCount_.fetch_add(1);
    queueCond_.wakeOne();
    emit queueSizeChanged(queueSize());
}

void OcrWorkerPool::enqueueBatch(const QList<OcrTask>& ts) {
    {
        QMutexLocker lk(&queueMutex_);
        for (const auto& t : ts) queue_.enqueue(t);
    }
    enqueuedCount_.fetch_add(ts.size());
    queueCond_.wakeAll();
    emit queueSizeChanged(queueSize());
}

void OcrWorkerPool::clearQueue() {
    QMutexLocker lk(&queueMutex_);
    queue_.clear();
    emit queueSizeChanged(0);
}

int OcrWorkerPool::queueSize() const {
    QMutexLocker lk(&queueMutex_);
    return queue_.size();
}

void OcrWorkerPool::pause()    { paused_.store(true);  emit logMessage("OCR paused"); }
void OcrWorkerPool::resume()   { paused_.store(false); queueCond_.wakeAll(); emit logMessage("OCR resumed"); }

void OcrWorkerPool::shutdown() {
    if (stopping_.exchange(true)) return;
    queueCond_.wakeAll();
    for (auto& t : threads_) {
        if (t->isRunning()) {
            t->wait(2000);
            if (t->isRunning()) t->terminate();
        }
    }
    threads_.clear();
}

void OcrWorkerPool::onWorkerFinished() {}

void OcrWorkerPool::workerLoop(int workerId) {
    // CRITICAL: install the SEH translator ON THIS THREAD.
    // _set_se_translator() is per-thread — main() only installs it on the
    // main thread. Without this, an access violation while parsing OCR
    // output (or anywhere else on this thread) would crash the process.
    installSehTranslator();

    // Windows.Media.Ocr engine — local, free, ships with Windows 10/11.
    WindowsOcrEngine engine;
    if (!engine.init()) {
        DS_ERROR("OCR", QString("Worker %1: Windows.Media.Ocr init failed; exiting").arg(workerId));
        return;
    }

    QElapsedTimer cpuTimer;
    cpuTimer.start();
    int lastCpuSample = 0;

    for (;;) {
        if (stopping_.load()) return;

        // Pause handling
        if (paused_.load()) {
            QThread::msleep(250);
            continue;
        }

        // Periodically sample CPU usage; if above threshold, sleep.
        if (cpuTimer.elapsed() > 1000) {
            lastCpuSample = readSystemCpuUsage();
            cpuTimer.restart();
            if (settings_.pauseOnHeavyLoad && lastCpuSample > settings_.cpuPauseThresholdPct) {
                DS_DEBUG("OCR", QString("CPU %1%% > threshold %2%% - pausing worker %3")
                         .arg(lastCpuSample).arg(settings_.cpuPauseThresholdPct).arg(workerId));
                QThread::msleep(2000);
                continue;
            }
            throttledSleep(settings_.cpuTargetPct, lastCpuSample);
        }

        OcrTask task;
        {
            QMutexLocker lk(&queueMutex_);
            while (queue_.isEmpty() && !stopping_.load() && !paused_.load())
                queueCond_.wait(&queueMutex_);
            if (stopping_.load()) return;
            if (queue_.isEmpty()) continue;
            task = queue_.dequeue();
        }
        emit queueSizeChanged(queueSize());

        // Run OCR
        bool ok = false;
        QString text;
        try {
            if (FileUtils::hasExtension(task.path, Constants::kImageExtensions)) {
                // v1.7.10 AUTO-ORIENTATION for images too. Rotated scans
                // arrive just as often as .jpg/.png/.tif as inside PDFs,
                // and the old path OCR'd the bytes as-is — a sideways
                // photo of a document came back EMPTY ("OCR returning
                // nothing"). OCR upright; if the quality score says
                // "this reads like junk", retry at 90/270/180 and keep
                // the orientation that reads best.
                QImage img(task.path);
                if (!img.isNull()) {
                    img = prepareForOcr(std::move(img));
                    QString best = engine.ocrImage(img);
                    const TextQuality::OcrTextStats uprightQ =
                        TextQuality::assessOcrText(best);
                    TextQuality::OcrTextStats bestQ = uprightQ;
                    int bestRank = ocrCandidateRank(bestQ);
                    if (ocrLooksMisoriented(bestQ)) {
                        for (const int deg : {90, 270, 180}) {
                            if (stopping_.load()) break;
                            QTransform tf;
                            tf.rotate(deg);
                            const QImage rotImg =
                                img.transformed(tf, Qt::SmoothTransformation);
                            if (rotImg.isNull()) continue;
                            const QString candidate = engine.ocrImage(rotImg);
                            const TextQuality::OcrTextStats cq =
                                TextQuality::assessOcrText(candidate);
                            const int cr = ocrCandidateRank(cq);
                            if (cr > bestRank) {
                                bestRank = cr;
                                best = candidate;
                                bestQ = cq;
                                DS_INFO("OCR", QString("Image OCR auto-orient: "
                                                       "%1 rotated %2 deg "
                                                       "(upright score %3/rank %4, "
                                                       "rotated score %5/rank %6)")
                                                       .arg(task.path,
                                                       QString::number(deg),
                                                       QString::number(uprightQ.runScore),
                                                       QString::number(ocrCandidateRank(uprightQ)),
                                                       QString::number(cq.runScore),
                                                       QString::number(cr)));
                            }
                            if (ocrLooksSolid(bestQ)) break;
                        }
                    }
                    text = best;
                    if (!text.trimmed().isEmpty()) ok = true;
                } else {
                    DS_WARN("OCR", QString("Cannot read image %1")
                                       .arg(task.path));
                }
            } else if (FileUtils::hasExtension(task.path,
                                               {QStringLiteral("pdf")})) {
                // v1.7.2: OCR a PDF by rendering each page with PDFium and
                // running the helper on the raster. Until now the pool
                // silently skipped PDF tasks (ok=false), so files flagged
                // needs_ocr - scanned pages, or a garbled text layer
                // discarded by PdfExtractor - never got automatic OCR and
                // stayed unreadable in preview/search. kMaxPdfOcrPages and
                // kPdfOcrDpi (defined for exactly this) bound the work.
#ifdef DOCUSEARCH_HAS_PDFIUM
                PdfiumDocument doc;
                if (doc.loadFromFile(task.path) && doc.pageCount() > 0) {
                    const int pages = qMin(doc.pageCount(),
                                           Constants::kMaxPdfOcrPages);
                    for (int i = 0; i < pages; ++i) {
                        if (stopping_.load()) break;
                        const double dpi = double(Constants::kPdfOcrDpi);

                        // v1.7.6 AUTO-ORIENTATION: scans are frequently
                        // stored sideways inside the page (the scanner wrote
                        // a rotated bitmap and no /Rotate entry, so PDFium —
                        // correctly — renders them sideways). Windows OCR
                        // then reads vertical text as garbage. OCR the
                        // upright raster first; if it yields almost no
                        // words, retry at 90°/270°/180° (PDFium's native
                        // rotation — no pixel juggling) and keep the
                        // orientation that reads the most words.
                        const QImage upright = prepareForOcr(doc.renderPage(i, dpi));
                        if (upright.isNull()) continue;
                        QString pageText = engine.ocrImage(upright);
                        // v1.7.10: judge by QUALITY SCORE, not raw word
                        // count. The old < 3 words trigger never fired on
                        // full-text sideways pages (sideways OCR still
                        // yields 3+ junk fragments), so the rotated passes
                        // — the entire point of auto-orientation — almost
                        // never ran and rotated PDFs kept coming back
                        // empty/garbled.
                        // v1.7.16: run score alone still misses DENSE
                        // sideways pages (measured: junk 1234 vs upright
                        // 1399 on the reference scan). The verdict now
                        // also demands real words; candidates are ranked
                        // by word rate first so the orientation that
                        // reads LANGUAGE always beats the one that reads
                        // the most junk characters.
                        const TextQuality::OcrTextStats uprightQ =
                            TextQuality::assessOcrText(pageText);
                        TextQuality::OcrTextStats bestQ = uprightQ;
                        int bestRank = ocrCandidateRank(bestQ);
                        int bestRot = 0;
                        if (ocrLooksMisoriented(bestQ)) {
                            for (const int rot : {1, 3, 2}) {
                                const QImage rotImg = doc.renderPage(i, dpi, rot);
                                if (rotImg.isNull()) continue;
                                const QString candidate = engine.ocrImage(rotImg);
                                const TextQuality::OcrTextStats cq =
                                    TextQuality::assessOcrText(candidate);
                                const int cr = ocrCandidateRank(cq);
                                if (cr > bestRank) {
                                    bestRank = cr;
                                    bestRot = rot;
                                    pageText = candidate;
                                    bestQ = cq;
                                }
                                if (ocrLooksSolid(bestQ)) break;
                            }
                            if (bestRot != 0) {
                                DS_INFO("OCR", QString("PDF OCR auto-orient: %1 page %2/%3 "
                                                       "rotated %4 deg (upright score %5/rank %6, "
                                                       "rotated score %7/rank %8)")
                                                       .arg(task.path,
                                                       QString::number(i + 1),
                                                       QString::number(pages),
                                                       QString::number(bestRot * 90),
                                                       QString::number(uprightQ.runScore),
                                                       QString::number(ocrCandidateRank(uprightQ)),
                                                       QString::number(bestQ.runScore),
                                                       QString::number(bestRank)));
                            }
                        }
                        if (!pageText.isEmpty()) {
                            text += pageText;
                            text += QLatin1Char('\n');
                        }
                    }
                    ok = !text.trimmed().isEmpty();
                    if (ok) {
                        DS_INFO("OCR", QString("PDF OCR %1: %2 pages -> %3 "
                                               "chars").arg(task.path,
                                               QString::number(pages),
                                               QString::number(text.size())));
                    }
                } else {
                    DS_WARN("OCR", QString("PDF OCR: cannot open %1 (%2)")
                                       .arg(task.path, doc.lastError()));
                }
#else
                DS_DEBUG("OCR", "PDF task skipped - PDF engine not linked");
#endif
            } else {
                ok = false;
            }
        } catch (const std::exception& e) {
            DS_ERROR("OCR", QString("Exception on %1: %2").arg(task.path, e.what()));
            ok = false;
        } catch (...) {
            // SEH-translated or unknown — log + continue.
            DS_ERROR("OCR", QString("Unknown exception on %1").arg(task.path));
            ok = false;
        }

        if (ok) {
            completedCount_.fetch_add(1);
        } else {
            failedCount_.fetch_add(1);
        }
        emit taskCompleted(task.fileId, text, ok);
        emit progress(completedCount_.load(), enqueuedCount_.load());
    }
}

} // namespace DocuSearch
