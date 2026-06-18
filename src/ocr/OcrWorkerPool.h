#pragma once

// ============================================================
// OcrWorkerPool.h — Thread-pool for background OCR with CPU throttling
// ============================================================

#include <QObject>
#include <QString>
#include <QQueue>
#include <QMutex>
#include <QWaitCondition>
#include <QThread>
#include <QQueue>
#include <atomic>
#include <memory>
#include <vector>

#include "../core/Types.h"
#include "../core/Config.h"

namespace DocuSearch {

class OcrEngine;

struct OcrTask {
    qint64    fileId   = 0;
    QString   path;
    QString   extension;
};

class OcrWorkerPool : public QObject {
    Q_OBJECT
public:
    explicit OcrWorkerPool(int numThreads, QObject* parent = nullptr);
    ~OcrWorkerPool() override;

    void setAppSettings(const AppSettings& s);
    void enqueue(const OcrTask& t);
    void enqueueBatch(const QList<OcrTask>& ts);
    void clearQueue();

    int  queueSize() const;

    // Pause/resume processing (does NOT abort running tasks).
    void pause();
    void resume();
    bool isPaused() const { return paused_.load(); }

    // Graceful shutdown. Blocks until workers stop.
    void shutdown();

signals:
    void taskCompleted(qint64 fileId, const QString& ocrText, bool success);
    void progress(int completed, int total);
    void queueSizeChanged(int size);
    void logMessage(const QString& msg);

private slots:
    void onWorkerFinished();

private:
    void spawnWorkers();
    void workerLoop(int workerId);

    friend class OcrWorkerThread;   // declared in the .cpp file

    mutable QMutex        queueMutex_;
    QWaitCondition        queueCond_;
    QQueue<OcrTask>       queue_;
    std::atomic<bool>     stopping_{false};
    std::atomic<bool>     paused_{false};

    int                   numThreads_;
    std::vector<std::unique_ptr<QThread>> threads_;
    std::atomic<int>      activeWorkers_{0};
    std::atomic<int>      completedCount_{0};
    std::atomic<int>      failedCount_{0};
    std::atomic<int>      enqueuedCount_{0};

    AppSettings           settings_;
};

} // namespace DocuSearch
