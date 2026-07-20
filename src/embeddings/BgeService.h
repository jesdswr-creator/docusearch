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

    // Embed a single document. Returns true on success.
    // If the document is already embedded, returns true immediately.
    bool embedDocument(int fileId, const QString& text);

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
