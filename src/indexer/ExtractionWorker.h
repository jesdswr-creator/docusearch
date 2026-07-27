#pragma once

// ============================================================
// ExtractionWorker.h - Off-main-thread file content extraction
// ============================================================
//
// WHY THIS EXISTS:
//   The old extraction loop ran `registry.extractByExtension()` inside
//   a QTimer::timeout callback on the MAIN thread. Each call could
//   take 100-1000ms for large PDFs/DOCX — freezing the entire UI
//   between timer ticks. Users saw a "not responding" window during
//   extraction.
//
// FIX:
//   Move extraction to a worker QThread. The worker:
//     • Opens its OWN SQLite connection (multi-connection safe — we
//       use WAL mode + busy_timeout, so concurrent reads/writes work).
//     • Runs extractByExtension() on the worker thread.
//     • Writes results back to the DB on the worker thread.
//     • Emits queued signals to the main thread for progress + UI
//       updates (status bar, progress bar, preview refresh).
//
// The main thread NEVER blocks on extraction — only on the
// (microsecond) signal/slot dispatch.
//
// NOTE: This struct is named `ExtractionProgress` (not `ExtractionResult`)
// to avoid a name collision with the existing `DocuSearch::ExtractionResult`
// defined in src/documents/IDocumentExtractor.h (which is the value
// returned by extractByExtension).
// ============================================================

#include <QObject>
#include <QString>
#include <QList>
#include <QAtomicInt>
#include <memory>

namespace DocuSearch {

struct ExtractionTodoItem {
    qint64  fileId = 0;
    QString path;
    QString ext;
};

// Progress update for a single file — emitted via fileExtracted().
// Distinct from ExtractionResult (the return type of extractByExtension)
// — this struct carries file metadata + status flags for the UI.
struct ExtractionProgress {
    qint64   fileId        = 0;
    QString  path;
    QString  filename;
    QString  extractedText;
    QString  source;          // "native", "ocr", etc.
    bool     ok              = false;
    bool     needsOcr        = false;
    bool     skippedTooLarge = false;
    bool     missingFile     = false;
    QString  errorMessage;
};

class ExtractionWorker : public QObject {
    Q_OBJECT
public:
    explicit ExtractionWorker(QObject* parent = nullptr);
    ~ExtractionWorker() override;

    // Configure the batch — call before moving the worker to its thread.
    void setTodo(const QList<ExtractionTodoItem>& todo,
                 const QString& dbPath,
                 bool   generateEmbeddings);

    // Cooperative cancel flag — checked between files. Main thread
    // calls cancelExtraction() (thread-safe).
    void cancelExtraction();
    bool isCancelled() const { return cancelFlag_.loadRelaxed() != 0; }

public slots:
    // Entry point — runs on the worker thread. Emits signals as it goes.
    void run();

signals:
    // Emitted after each file is processed (queued connection → main thread).
    void fileExtracted(const DocuSearch::ExtractionProgress& result);

    // Emitted periodically with (done, total).
    void progress(int done, int total);

    // Emitted once at the end with final counts.
    void finished(int succeeded, int failed, int total);

private:
    QList<ExtractionTodoItem> todo_;
    QString  dbPath_;
    bool     generateEmbeddings_ = false;
    QAtomicInt cancelFlag_{0};
};

} // namespace DocuSearch

// Required for Qt's queued signal/slot connections across threads.
// Without this, the fileExtracted() signal will fail to deliver
// ExtractionProgress from the worker thread to the main thread at runtime.
Q_DECLARE_METATYPE(DocuSearch::ExtractionProgress)
