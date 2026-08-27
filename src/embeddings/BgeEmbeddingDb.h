#pragma once

// ============================================================
// BgeEmbeddingDb.h - SQLite storage for BGE embeddings
// ============================================================

#include <QString>
#include <vector>
#include <cstdint>

struct sqlite3;

namespace DocuSearch {

struct SemanticHit {
    int     fileId    = 0;
    QString filePath;
    QString filename;
    float   similarity = 0.0f;
};

class BgeEmbeddingDb {
public:
    explicit BgeEmbeddingDb(const QString& dbPath);
    ~BgeEmbeddingDb();

    BgeEmbeddingDb(const BgeEmbeddingDb&)            = delete;
    BgeEmbeddingDb& operator=(const BgeEmbeddingDb&) = delete;

    bool open();

    // Store a 384-float embedding for the given file. Returns true on success.
    bool storeEmbedding(int fileId, const std::vector<float>& embedding);

    // Retrieve the embedding for a file. Returns true on success.
    bool getEmbedding(int fileId, std::vector<float>& outEmbedding);

    // Search for similar embeddings. Returns up to topK results with
    // similarity >= threshold. Scans ALL embeddings (O(N)) — for large
    // datasets, prefer searchSimilarFiltered() which only scans the
    // specified file IDs.
    std::vector<SemanticHit> searchSimilar(
        const std::vector<float>& queryEmbedding,
        int topK,
        float threshold);

    // Search for similar embeddings ONLY within the given set of file IDs.
    // This is the recommended approach for hybrid search — first get the
    // top 200 BM25 results, then run cosine similarity only on those 200
    // embeddings (not all 100K). See HIGH-5 in the review report.
    // Empty fileIds = scan all (same as searchSimilar).
    std::vector<SemanticHit> searchSimilarFiltered(
        const std::vector<float>& queryEmbedding,
        const std::vector<int>& fileIds,
        int topK,
        float threshold);

    bool hasEmbedding(int fileId);
    bool deleteEmbedding(int fileId);

    // ── Phase 2: Chunked embeddings ──────────────────────────
    // Store multiple chunk embeddings for a single file.
    struct ChunkData {
        int chunkIndex;
        int startOffset;
        int endOffset;
        std::vector<float> embedding;
    };
    bool storeChunks(int fileId, const std::vector<ChunkData>& chunks);
    bool hasChunks(int fileId);
    bool deleteChunks(int fileId);

    // Search chunks — returns best matching chunk per file.
    std::vector<SemanticHit> searchSimilarChunks(
        const std::vector<float>& queryEmbedding,
        const std::vector<int>& fileIds,
        int topK,
        float threshold);

    // Phase 3: Search ALL chunks (for RRF — semantic runs independently).
    // Scans all chunks in batches of 500, groups by file_id, keeps best.
    std::vector<SemanticHit> searchSimilarChunksAll(
        const std::vector<float>& queryEmbedding,
        int topK,
        float threshold);

    struct Stats {
        int total     = 0;
        int completed = 0;
        int failed    = 0;
    };
    Stats getStats();

    // Diagnostic: the highest cosine similarity seen during the most recent
    // search scan, REGARDLESS of whether it cleared the caller's threshold.
    // Lets the UI explain WHY a query produced zero semantic hits ("closest
    // match scored 38% but the bar is 45%") instead of a silent nothing.
    float lastBestSimilarity() const { return m_lastBestSimilarity; }

private:
    static float cosineSimilarity(const std::vector<float>& a,
                                   const std::vector<float>& b);

    QString  m_dbPath;
    sqlite3* m_db = nullptr;
    float    m_lastBestSimilarity = -1.0f;  // reset at each scan, max over rows

    static constexpr int EMBEDDING_DIM      = 384;
    static constexpr int EMBEDDING_BYTES    = EMBEDDING_DIM * 4;  // 1536
};

} // namespace DocuSearch
