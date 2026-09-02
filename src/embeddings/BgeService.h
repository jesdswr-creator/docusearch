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

#include <QObject>
#include <QString>
#include <QVector>
#include <QStringList>
#include <QFuture>
#include <memory>
#include <atomic>

namespace DocuSearch {

class BgeService : public QObject {
    Q_OBJECT
public:
    explicit BgeService(QObject* parent = nullptr);
    ~BgeService();

    // Initialize engine + database. Returns true on success.
    // Never throws — all errors are caught and logged.
    bool initialize(const QString& dbPath, const QString& modelPath);

    bool   isReady() const { return m_initialized; }
    QString getStatus() const { return m_statusMessage; }

    // Search for documents semantically similar to the query.
    // Returns up to topK results with similarity >= threshold.
    // Never throws — returns empty vector on any error.
    std::vector<SemanticHit> search(
        const QString& query,
        int topK = 20,
        float threshold = 0.40f);

    // Search ONLY within the given file IDs — much faster than scanning
    // all embeddings. Use this in hybrid search: first get top BM25 results,
    // then only compute cosine similarity for those files.
    // Never throws — returns empty vector on any error.
    std::vector<SemanticHit> searchFiltered(
        const QString& query,
        const std::vector<int>& fileIds,
        int topK = 20,
        float threshold = 0.40f);

    // Phase 2: Search chunks — finds best matching chunk per file.
    // More precise than document-level search for long documents.
    std::vector<SemanticHit> searchChunksFiltered(
        const QString& query,
        const std::vector<int>& fileIds,
        int topK = 20,
        float threshold = 0.40f);

    // Phase 3: Search ALL chunks (not filtered by keyword results).
    // Used for RRF fusion — semantic search runs independently.
    std::vector<SemanticHit> searchChunksAll(
        const QString& query,
        int topK = 50,
        float threshold = 0.40f);

    // Embed a single document. Returns true on success.
    // If the document is already embedded, returns true immediately.
    bool embedDocument(int fileId, const QString& text);

    // Phase 2: Embed a document as multiple chunks (256 tokens, 64 overlap).
    // Generates one embedding per chunk and stores in EmbeddingChunks table.
    // Falls back to single embedding for short documents (< 256 tokens).
    bool embedDocumentChunked(int fileId, const QString& text);

    // Embed a batch of documents in the background. Emits
    // embeddingProgress and embeddingFinished signals.
    void embedDocumentsBatch(const QVector<int>& fileIds, const QStringList& texts);

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
};

} // namespace DocuSearch
