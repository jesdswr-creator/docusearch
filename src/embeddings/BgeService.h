#pragma once

// ============================================================
// BgeService.h - High-level orchestrator for BGE semantic search
// ============================================================
//
// Combines BgeEmbeddingEngine + BgeEmbeddingDb into a single
// service. Handles all error cases silently (never crash, never
// propagate exceptions to the caller).
// ============================================================

#include "BgeEmbeddingEngine.h"
#include "BgeEmbeddingDb.h"
#include "IEmbeddingService.h"

#include <QObject>
#include <QString>
#include <QVector>
#include <QStringList>
#include <QFuture>
#include <QHash>
#include <QMutex>
#include <memory>
#include <atomic>

class QThreadPool;   // v1.7.20: dedicated embedding worker pool

namespace DocuSearch {

// v1.7.21: implements IEmbeddingService so EmbeddingController (and its
// wiring tests) can drive the backfill through the interface — the two
// methods already existed with matching signatures.
class BgeService : public QObject, public IEmbeddingService {
    Q_OBJECT
public:
    explicit BgeService(QObject* parent = nullptr);
    ~BgeService();

    // Initialize engine + database. Returns true on success.
    // Never throws — all errors are caught and logged.
    bool initialize(const QString& dbPath, const QString& modelPath);

    bool   isReady() const override { return m_initialized; }   // IEmbeddingService
    QString getStatus() const { return m_statusMessage; }

    // Search for documents semantically similar to the query.
    // Returns up to topK results with similarity >= threshold.
    // Never throws — returns empty vector on any error.
    // v1.7.19: `cancel` aborts cooperatively (checks passed down to the
    // vector scans) so a superseded search stops early. The QUERY
    // EMBEDDING is LRU-cached — repeat/refined queries skip ONNX
    // inference entirely.
    std::vector<SemanticHit> search(
        const QString& query,
        int topK = 20,
        float threshold = 0.40f,
        const std::atomic<bool>* cancel = nullptr);

    // Search ONLY within the given file IDs — much faster than scanning
    // all embeddings. Use this in hybrid search: first get top BM25 results,
    // then only compute cosine similarity for those files.
    // Never throws — returns empty vector on any error.
    std::vector<SemanticHit> searchFiltered(
        const QString& query,
        const std::vector<int>& fileIds,
        int topK = 20,
        float threshold = 0.40f,
        const std::atomic<bool>* cancel = nullptr);

    // Phase 2: Search chunks — finds best matching chunk per file.
    // More precise than document-level search for long documents.
    std::vector<SemanticHit> searchChunksFiltered(
        const QString& query,
        const std::vector<int>& fileIds,
        int topK = 20,
        float threshold = 0.40f,
        const std::atomic<bool>* cancel = nullptr);

    // Phase 3: Search ALL chunks (not filtered by keyword results).
    // Used for RRF fusion — semantic search runs independently.
    // v1.7.26: `maxRows` bounds how many chunk rows the scan may visit
    // (see BgeEmbeddingDb::searchSimilarChunksAll); the document-level
    // pass is skipped when the chunk scan already delivered `topK`
    // precise hits, so a saturated chunk index no longer pays for two
    // full scans per query.
    std::vector<SemanticHit> searchChunksAll(
        const QString& query,
        int topK = 50,
        float threshold = 0.40f,
        int maxRows = 250000,
        const std::atomic<bool>* cancel = nullptr);

    // Embed a single document. Returns true on success.
    // If the document is already embedded, returns true immediately.
    bool embedDocument(int fileId, const QString& text);

    // Phase 2: Embed a document as multiple chunks (256 tokens, 64 overlap).
    // Generates one embedding per chunk and stores in EmbeddingChunks table.
    // Falls back to single embedding for short documents (< 256 tokens).
    bool embedDocumentChunked(int fileId, const QString& text);

    // Embed a batch of documents in the background. Emits
    // embeddingProgress and embeddingFinished signals.
    void embedDocumentsBatch(const QVector<int>& fileIds,
                             const QStringList& texts) override;   // IEmbeddingService

    // v1.7.20: run ALL background work (batch embedding worker) on a
    // DEDICATED thread pool owned by MainWindow instead of the global
    // QtConcurrent pool. The global pool is shared with the folder-scan
    // walk, so during a busy scan the embedding worker queued behind
    // walk/hash tasks and batches stalled for seconds. Pass nullptr
    // (the default) to keep the old global-pool behavior.
    void setWorkerPool(QThreadPool* pool) { m_pool = pool; }

    // Database stats (total/completed/failed embedding counts).
    BgeEmbeddingDb::Stats getStats() const;

    // Diagnostic: best cosine similarity observed in the most recent search
    // scan (document-level or chunk-level), even when it fell below the
    // threshold. -1 if no scan has run yet. Powers the "closest match
    // scored X%" hint so an empty semantic result is never a silent wall.
    float lastBestSimilarity() const {
        return m_database ? m_database->lastBestSimilarity() : -1.0f;
    }

signals:
    void ready();
    void embeddingProgress(int current, int total);
    void embeddingFinished(int successCount, int failCount);

private:
    // v1.7.19: QUERY-embedding LRU cache. The ONNX inference for a query
    // costs tens of milliseconds (or more under CPU contention); the
    // same query text always produces the same vector, so repeat and
    // refined searches (the user typing one more word) reuse the cached
    // embedding and skip inference entirely. Keyed by the PREFIXED query
    // text (the same string embed() would see). 32 entries × 384 floats
    // ≈ 50 KB — bounded and trivial.
    std::vector<float> cachedQueryEmbedding(const QString& prefixedKey, bool* hit);
    void rememberQueryEmbedding(const QString& prefixedKey,
                                const std::vector<float>& emb);
    static constexpr int kQueryCacheCap = 32;
    QMutex                             m_queryCacheMutex;
    QHash<QString, std::vector<float>> m_queryCache;
    QStringList                        m_queryCacheOrder;  // front = LRU

    std::unique_ptr<BgeEmbeddingEngine> m_engine;
    std::unique_ptr<BgeEmbeddingDb>     m_database;
    bool        m_initialized   = false;
    QString     m_statusMessage;

    // v1.7.11 lifetime safety: the batch-embedding worker captures `this`,
    // m_engine and m_database raw. If BgeService was destroyed mid-batch
    // (app exit during "Embed All"), the worker used freed objects.
    // The destructor now raises m_stopRequested (checked once per
    // document, so the wait is bounded by ONE inference) and joins
    // m_batchFuture before members are torn down.
    QFuture<void>     m_batchFuture;
    std::atomic<bool> m_stopRequested{false};

    // v1.7.20: dedicated worker pool (owned by MainWindow; may be null =
    // use the global pool). Not accessed by the worker itself — only used
    // when LAUNCHING a batch, so no synchronization is needed.
    QThreadPool*      m_pool = nullptr;
};

} // namespace DocuSearch
