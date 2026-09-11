#pragma once

// ============================================================
// BgeEmbeddingDb.h - SQLite storage for BGE embeddings
// ============================================================

#include <QString>
#include <vector>
#include <cstdint>
#include <atomic>

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
    // v1.7.19: `cancel` (optional) aborts the scan cooperatively — each
    // checkpoint bails out so a superseded search stops paying for a
    // result that will be discarded. Cancelled scans return an empty
    // list (the caller drops the result by generation anyway).
    std::vector<SemanticHit> searchSimilar(
        const std::vector<float>& queryEmbedding,
        int topK,
        float threshold,
        const std::atomic<bool>* cancel = nullptr);

    // Search for similar embeddings ONLY within the given set of file IDs.
    // This is the recommended approach for hybrid search — first get the
    // top 200 BM25 results, then run cosine similarity only on those 200
    // embeddings (not all 100K). See HIGH-5 in the review report.
    // Empty fileIds = scan all (same as searchSimilar).
    std::vector<SemanticHit> searchSimilarFiltered(
        const std::vector<float>& queryEmbedding,
        const std::vector<int>& fileIds,
        int topK,
        float threshold,
        const std::atomic<bool>* cancel = nullptr);

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
        float threshold,
        const std::atomic<bool>* cancel = nullptr);

    // Phase 3: Search ALL chunks (for RRF — semantic runs independently).
    // v1.7.19 REWRITE: one rowid-range cursor (chunk_id is the table's
    // rowid) streamed in batches — the old code paginated with
    // "LIMIT 500 OFFSET k", making every batch re-walk and discard all
    // rows before it: O(N²) row visits per search (tens of millions of
    // wasted walks on a 100k-chunk library). Grouped by file_id, best
    // chunk wins. `cancel` aborts at each batch boundary.
    // v1.7.26: a HARD scan budget (maxRows) bounds how many chunk rows a
    // single query may visit — the old scan walked the ENTIRE table no
    // matter what, which is the "search takes 20 s" report on very large
    // libraries. Libraries smaller than the budget are still scanned in
    // full; bigger ones return the best matches from the first `maxRows`
    // chunks (plus the bounded document-level pass). maxRows <= 0 = the
    // default budget.
    std::vector<SemanticHit> searchSimilarChunksAll(
        const std::vector<float>& queryEmbedding,
        int topK,
        float threshold,
        int maxRows = kDefaultScanBudget,
        const std::atomic<bool>* cancel = nullptr);

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
    // v1.7.19: the ONLY similarity kernel used by the scans.
    // INVARIANT: every vector stored under algo_version >= 1 is
    // L2-normalized (BgeEmbeddingEngine::embed() normalizes; the store
    // paths re-normalize defensively). For normalized vectors
    // cosine(a,b) == dot(a,b), so the kernel is a single-accumulator,
    // 4-way unrolled dot product: 3x fewer FLOPs than the old
    // 3-accumulator cosine (which pointlessly recomputed the query norm
    // and each row's norm on EVERY row of EVERY search) and the unroll
    // auto-vectorizes under MSVC /O2. Returns the similarity directly —
    // no sqrt, no divisions.
    static float dotSimilarity(const float* a, const float* b, int n);

    QString  m_dbPath;
    sqlite3* m_db = nullptr;
    float    m_lastBestSimilarity = -1.0f;  // reset at each scan, max over rows

    static constexpr int EMBEDDING_DIM      = 384;
    static constexpr int EMBEDDING_BYTES    = EMBEDDING_DIM * 4;  // 1536
    // v1.7.26: default cap on chunk rows visited per semantic query.
    // ~1–2 s of scan work on mid hardware; HybridSearchEngine passes a
    // tier-aware override.
    static constexpr int kDefaultScanBudget = 250000;
};

} // namespace DocuSearch
