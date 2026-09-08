#pragma once

// ============================================================
// EmbeddingController.h - Headless owner of the AI-embedding
// backfill state machine (v1.7.21).
// ============================================================
//
// Moved out of MainWindow (god-object decomposition): the two-phase
// backfill queue (document-level, then chunk-level), the stale-
// embedding invalidation pass, the "Rebuild All AI Embeddings" purge
// chain, and the deadlock guard that stops an all-failing batch from
// being re-queued forever.
//
// Headless-testable: it depends on Database (sqlite) and the
// IEmbeddingService interface — never on BgeService or ONNX — so
// tst_Wiring drives the full state machine with a fake service and a
// temp database. All resources are constructed eagerly in the ctor
// and audited by verifyWiring().
//
// THREADING (unchanged from v1.7.20): all ONNX inference runs on the
// dedicated single-thread embedding pool — search (searchPool_),
// extraction (ExtractionController's pool), embedding (this pool) and
// OCR each own their thread, so background work can never starve or
// freeze another path.
// ============================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QThreadPool>

#include "embeddings/IEmbeddingService.h"

namespace DocuSearch {

class Database;
class BgeEmbeddingDb;   // for kAlgoVersion in the .cpp include

class EmbeddingController : public QObject {
    Q_OBJECT
public:
    explicit EmbeddingController(QObject* parent = nullptr);
    ~EmbeddingController() override;

    // ---- wiring ---------------------------------------------------
    void setDatabase(Database* db)              { m_db = db; }
    // The service is optional (AI can be unavailable): with no service
    // attached the backfill is a no-op and the chip stays OFF.
    void attachService(IEmbeddingService* svc)  { m_service = svc; }
    IEmbeddingService* service() const          { return m_service; }
    // The dedicated embedding worker pool — MainWindow plumbs it into
    // BgeService::setWorkerPool so ALL BGE work (model init + batch
    // embedding) runs here, never on the global QtConcurrent pool.
    QThreadPool* embeddingPool() const          { return m_pool; }
    // Whether the AI toggle is currently ON (the end-of-drain chip
    // reads it: "ON" vs "OFF").
    void setAiEnabled(bool on)                  { m_aiEnabled = on; }

    // Runtime audit: pool eagerly constructed and configured.
    bool verifyWiring() const;

    // ---- backfill -------------------------------------------------
    // Scan SearchIndex/DocumentText for files with no current
    // embedding (stale algo_version counts as missing) and queue one
    // batch on the background worker. Follow-up batches are chained
    // from noteEmbeddingFinished until drained. Safe to call from any
    // wake site: BGE-ready, AI toggle on, extraction finished,
    // Settings button.
    void ensureBackfill();

    // One-click "Rebuild All AI Embeddings": batch-deletes every
    // BgeEmbeddings/EmbeddingChunks row, then hands over to
    // ensureBackfill() so the whole library is re-embedded from FULL
    // document text. Emits rebuildBlocked(msg) when AI is not ready
    // (the UI shows the dialog — the controller is headless).
    void startRebuild();

    // BgeService signal deliveries (the window forwards them; tests
    // call directly).
    void noteEmbeddingProgress(int current, int total);
    void noteEmbeddingFinished(int success, int fail);

    // Backlog counters (power the status line and the Settings AI
    // section). Pure SQL against the temp database — unit-tested.
    qint64 countMissingEmbeddings();
    qint64 countMissingChunkDocs();

    // State flags the status bar / close confirmation disclose.
    bool isBackfillRunning() const      { return m_backfillRunning; }
    bool isRebuildPurging() const       { return m_rebuildPurging; }

    // Teardown helpers: database reset / destruction must stop the
    // purge chain and clear the in-flight batch flag so late
    // completions cannot write into a new database.
    void stopAll();
    void shutdownForTeardown();

signals:
    // Persistent AI chip (state + counts) next to the AI switch.
    void chipChanged(const QString& text, bool active);
    // statusBar()->showMessage(msg, timeoutMs)
    void statusMessage(const QString& msg, int timeoutMs = 0);
    // "Rebuild" cannot start (AI not ready / nothing to rebuild).
    void rebuildBlocked(const QString& reason);
    // A batch fully finished (success/fail counts) — diagnostics.
    void backfillBatchFinished(int success, int fail);

private slots:
    void purgeEmbeddingsTick();

private:
    Database*          m_db      = nullptr;
    IEmbeddingService* m_service = nullptr;

    // Eagerly constructed (verifyWiring audits this).
    QThreadPool*       m_pool    = nullptr;

    bool m_backfillRunning      = false;  // batch embed in flight
    bool m_backfillChunkMode    = false;  // current batch is chunk mode
    int  m_backfillDeadlock     = 0;      // consecutive zero-success batches
    bool m_rebuildPurging       = false;  // rebuild purge chain in flight
    int  m_rebuildRetries       = 0;      // consecutive purge SQL failures
    bool m_aiEnabled            = false;  // AI switch state (chip only)
};

} // namespace DocuSearch
