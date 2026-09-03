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
    // v1.7.15: which embedding algorithm produced the stored vectors.
    // Bump this whenever the model, tokenizer, pooling or query prefix
    // changes in a way that makes older stored vectors INCOMPARABLE with
    // fresh ones. Consequences of the version stamp:
    //   • storeEmbedding()/storeChunks() stamp new rows with kAlgoVersion
    //   • hasEmbedding()/hasChunks() treat rows with an OLDER version as
    //     MISSING — so the background backfill re-embeds them
    //   • every search scan ignores older-version rows, so garbage
    //     vectors from the pre-fix hash-fallback tokenizer builds can
    //     never surface as "AI matches" again
    // Rows written by builds without the column default to 0 (= "unknown
    // / pre-versioning") and are therefore treated as stale too.
    static constexpr int kAlgoVersion = 1;

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
    // Lets callers combine diagnostics from multiple scans (chunk-level
    // and document-level searches each reset/overwrite the value).
    void setLastBestSimilarity(float v) { m_lastBestSimilarity = v; }

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
