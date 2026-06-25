#pragma once

// ============================================================
// Indexer.h - Top-level orchestrator combining metadata + content
// ============================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <memory>

#include "../core/Types.h"
#include "MetadataIndexer.h"   // needed for MetadataIndexer::CancelFlag

namespace DocuSearch {

class FileRepository;
class OcrWorkerPool;
class ContentIndexer;

class Indexer : public QObject {
    Q_OBJECT
public:
    explicit Indexer(FileRepository& repo, OcrWorkerPool& ocrPool,
                     QObject* parent = nullptr);
    ~Indexer() override;

    // Start a full Phase-1 scan + Phase-2 content extraction.
    // This kicks off a background thread.
    void startIndexing(const AppSettings& s);

    // Stop indexing after the current file.
    void stopIndexing();

    // Pause/resume background content indexing (OCR workers pause independently).
    void pause();
    void resume();
    bool isPaused() const { return paused_.load(); }
    bool isRunning() const { return running_.load(); }
    bool isStopping() const { return stopping_.load(); }

    // Lazy-index a specific file on demand. Returns extracted text (may be empty).
    QString lazyIndex(qint64 fileId);

    // Force-reindex a single file (e.g., after watcher detected modification).
    void reindexFile(const QString& path);

    // Drop DB rows for paths that no longer exist on disk.
    qint64 pruneDeleted();

signals:
    void phaseChanged(const QString& phase);
    void progress(const IndexingProgress& p);
    void indexingStarted();
    void indexingFinished();
    void logMessage(const QString& msg);

private slots:
    void onMetadataProgress(qint64 n);
    void onMetadataFinished(qint64 total, qint64 errors);
    void onContentProcessed(qint64 fileId, const QString& path, bool success);
    void onOcrCompleted(qint64 fileId, const QString& ocrText, bool ok);
    void onCpuSampleTick();

private:
    void startPhase2();
    void emitProgress();

    FileRepository&  repo_;
    OcrWorkerPool&   ocrPool_;
    AppSettings      settings_;

    std::unique_ptr<MetadataIndexer> metadataIndexer_;
    std::unique_ptr<ContentIndexer>  contentIndexer_;

    std::unique_ptr<QThread> workerThread_;
    QTimer                   cpuTimer_;
    std::atomic<bool>        running_{false};
    std::atomic<bool>        paused_{false};
    std::atomic<bool>        stopping_{false};
    MetadataIndexer::CancelFlag cancelFlag_;

    IndexingProgress progress_;
};

} // namespace DocuSearch
