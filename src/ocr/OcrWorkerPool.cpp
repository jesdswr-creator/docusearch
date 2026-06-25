// ============================================================
// OcrWorkerPool.cpp
// ============================================================

#include "OcrWorkerPool.h"
#include "OcrEngine.h"
#include "../core/Logger.h"
#include "../core/FileUtils.h"
#include "../core/StringUtils.h"
#include "../core/Constants.h"

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
#include <algorithm>

namespace DocuSearch {

namespace {
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
    // Per-thread Tesseract instance.
    OcrEngine engine(settings_.tessdataPath, settings_.ocrLanguage);
    if (!engine.init()) {
        DS_ERROR("OCR", QString("Worker %1: Tesseract init failed; exiting").arg(workerId));
        return;
    }
    engine.setPsm(3);  // auto page segmentation

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
            // For PDFs, we'd render pages first (out of scope here without a
            // PDF rasterizer; users typically have OCR'd TIFFs already).
            if (FileUtils::hasExtension(task.path, Constants::kImageExtensions)) {
                text = engine.ocrFile(task.path);
                if (!text.isEmpty()) ok = true;
            } else {
                // Render first page of PDF to image - requires Poppler-Qt.
                // Skipped here; mark as not-needed.
                ok = false;
            }
        } catch (const std::exception& e) {
            DS_ERROR("OCR", QString("Exception on %1: %2").arg(task.path, e.what()));
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
