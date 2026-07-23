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
#include <memory>

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

signals:
    void ready();
    void embeddingProgress(int current, int total);
    void embeddingFinished(int successCount, int failCount);

private:
    std::unique_ptr<BgeEmbeddingEngine> m_engine;
    std::unique_ptr<BgeEmbeddingDb>     m_database;
    bool        m_initialized   = false;
    QString     m_statusMessage;
};

} // namespace DocuSearch
