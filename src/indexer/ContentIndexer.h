#pragma once

// ============================================================
// ContentIndexer.h — Phase 2: extract text + queue OCR
// ============================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include <atomic>
#include <memory>

#include "../core/Types.h"

namespace DocuSearch {

class FileRepository;
class OcrWorkerPool;

class ContentIndexer : public QObject {
    Q_OBJECT
public:
    ContentIndexer(FileRepository& repo, OcrWorkerPool& ocrPool,
                   QObject* parent = nullptr);

    // Pull next pending file from DB, extract text via the right extractor,
    // and enqueue OCR if the file is an image or has needsOcr=true.
    // Returns true if there was a file to process (caller can loop).
    bool processOne();

    // Process up to `max` files in current thread. Returns count actually processed.
    int processBatch(int max);

    // Process a specific file immediately (lazy OCR). Returns the extracted text.
    // Blocks until text is available (max ~30s).
    QString processNow(qint64 fileId);

    qint64 documentsIndexed() const { return docsIndexed_.load(); }
    qint64 ocrQueued()        const { return ocrQueued_.load(); }
    qint64 errors()           const { return errors_.load(); }

    void setAppSettings(const AppSettings& s) { settings_ = s; }

signals:
    void fileProcessed(qint64 fileId, const QString& path, bool success);
    void ocrEnqueued(qint64 fileId);
    void progress(qint64 docsIndexed, qint64 queueRemaining);

private:
    FileRepository&  repo_;
    OcrWorkerPool&   ocrPool_;
    AppSettings      settings_;
    std::atomic<qint64> docsIndexed_{0};
    std::atomic<qint64> ocrQueued_{0};
    std::atomic<qint64> errors_{0};
};

} // namespace DocuSearch
