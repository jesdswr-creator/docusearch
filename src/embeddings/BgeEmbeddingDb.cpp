// ============================================================
// BgeEmbeddingDb.cpp - SQLite blob storage for embeddings
// ============================================================

#include "BgeEmbeddingDb.h"
#include "../core/Logger.h"

#include <sqlite3.h>
#include <cstring>
#include <cmath>
#include <map>
#include <algorithm>

namespace DocuSearch {

namespace {

// v1.7.19: defensive guarantee of the dot-similarity invariant — every
// vector entering the DB is L2-normalized BEFORE the blob is written.
// embed() already normalizes, so this is a sub-microsecond no-op in
// practice; it exists so a future store-path change can never silently
// break cosine(a,b) == dot(a,b) for the scan kernel.
void normalizeInPlace(std::vector<float>& v) {
    float n2 = 0.0f;
    for (float x : v) n2 += x * x;
    n2 = std::sqrt(n2);
    if (n2 > 1e-9f) {
        const float inv = 1.0f / n2;
        for (float& x : v) x *= inv;
    }
}

} // namespace

BgeEmbeddingDb::BgeEmbeddingDb(const QString& dbPath)
    : m_dbPath(dbPath) {
}

BgeEmbeddingDb::~BgeEmbeddingDb() {
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

bool BgeEmbeddingDb::open() {
    if (m_db) return true;
    int rc = sqlite3_open(m_dbPath.toUtf8().constData(), &m_db);
    if (rc != SQLITE_OK) {
        DS_WARN("BGE", QString("Failed to open embedding DB: %1")
            .arg(sqlite3_errmsg(m_db)));
        if (m_db) {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
        return false;
    }
    // Performance pragmas.
    sqlite3_exec(m_db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
    return true;
}

bool BgeEmbeddingDb::storeEmbedding(int fileId, const std::vector<float>& embedding) {
    if (!m_db) return false;
    if (static_cast<int>(embedding.size()) != EMBEDDING_DIM) {
        DS_WARN("BGE", QString("storeEmbedding: invalid embedding size %1 (expected %2)")
            .arg(embedding.size()).arg(EMBEDDING_DIM));
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    // v1.7.15: stamp the algorithm version so stale vectors from older
    // builds are detected (and re-embedded) instead of searched against.
    const char* sql =
        "INSERT OR REPLACE INTO BgeEmbeddings "
        "(file_id, embedding, created_at, updated_at, status, algo_version) "
        "VALUES (?1, ?2, strftime('%s','now'), strftime('%s','now'), 'completed', ?3);";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        DS_WARN("BGE", "Failed to prepare storeEmbedding stmt.");
        return false;
    }

    sqlite3_bind_int64(stmt, 1, fileId);
    std::vector<float> normalized = embedding;
    normalizeInPlace(normalized);
    sqlite3_bind_blob(stmt, 2, normalized.data(), EMBEDDING_BYTES, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, kAlgoVersion);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool BgeEmbeddingDb::getEmbedding(int fileId, std::vector<float>& outEmbedding) {
    outEmbedding.clear();
    if (!m_db) return false;

    sqlite3_stmt* stmt = nullptr;
    // v1.7.15: only CURRENT-algorithm embeddings are returned — a stale
    // vector is no different from no vector.
    const char* sql =
        "SELECT embedding FROM BgeEmbeddings "
        "WHERE file_id = ?1 AND status = 'completed' AND algo_version >= ?2;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(stmt, 1, fileId);
    sqlite3_bind_int(stmt, 2, kAlgoVersion);

    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const void* blob = sqlite3_column_blob(stmt, 0);
        const int   size = sqlite3_column_bytes(stmt, 0);
        if (blob && size == EMBEDDING_BYTES) {
            outEmbedding.resize(EMBEDDING_DIM);
            std::memcpy(outEmbedding.data(), blob, EMBEDDING_BYTES);
            ok = true;
        }
    }
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<SemanticHit> BgeEmbeddingDb::searchSimilar(
    const std::vector<float>& queryEmbedding,
    int topK,
    float threshold,
    const std::atomic<bool>* cancel) {

    std::vector<SemanticHit> results;
    if (!m_db) return results;
    if (static_cast<int>(queryEmbedding.size()) != EMBEDDING_DIM) return results;

    sqlite3_stmt* stmt = nullptr;
    // v1.7.15: only CURRENT-algorithm embeddings are scanned — stale
    // vectors (older tokenizer/model, or pre-versioning rows) would
    // score meaningless similarities and pollute the results.
    const char* sql =
        "SELECT b.file_id, f.path, f.filename, b.embedding "
        "FROM BgeEmbeddings b "
        "LEFT JOIN Files f ON b.file_id = f.id "
        "WHERE b.status = 'completed' AND b.algo_version >= ?1 "
        "LIMIT 50000;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        DS_WARN("BGE", "Failed to prepare searchSimilar stmt.");
        return results;
    }
    sqlite3_bind_int(stmt, 1, kAlgoVersion);

    // v1.7.19: one scratch buffer reused across rows — the old loop
    // heap-allocated a fresh 384-float vector PER ROW (100k allocations
    // per search on a big library).
    std::vector<float> emb(EMBEDDING_DIM);
    const float* q = queryEmbedding.data();

    // Diagnostics: remember the best similarity even below threshold so the
    // UI can report how close the nearest document came.
    float bestSim = -1.0f;
    long long checkpoint = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        // v1.7.19: cooperative cancel — a superseded search must not pay
        // for a full scan of a result that will be discarded.
        if (++checkpoint % 4096 == 0 && cancel && cancel->load()) {
            sqlite3_finalize(stmt);
            m_lastBestSimilarity = bestSim;
            return results;
        }
        const int   fileId = sqlite3_column_int(stmt, 0);
        const void* blob   = sqlite3_column_blob(stmt, 3);
        const int   size   = sqlite3_column_bytes(stmt, 3);

        if (!blob || size != EMBEDDING_BYTES) continue;

        std::memcpy(emb.data(), blob, EMBEDDING_BYTES);

        const float sim = dotSimilarity(q, emb.data(), EMBEDDING_DIM);
        if (sim > bestSim) bestSim = sim;
        if (sim >= threshold) {
            const char* path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            SemanticHit hit;
            hit.fileId    = fileId;
            hit.filePath  = path  ? QString::fromUtf8(path) : QString();
            hit.filename  = name  ? QString::fromUtf8(name) : QString();
            hit.similarity = sim;
            results.push_back(hit);
        }
    }
    sqlite3_finalize(stmt);
    m_lastBestSimilarity = bestSim;

    // Sort by similarity descending, then trim to topK.
    std::sort(results.begin(), results.end(),
        [](const SemanticHit& a, const SemanticHit& b) {
            return a.similarity > b.similarity;
        });
    if (static_cast<int>(results.size()) > topK) {
        results.resize(topK);
    }
    return results;
}

std::vector<SemanticHit> BgeEmbeddingDb::searchSimilarFiltered(
    const std::vector<float>& queryEmbedding,
    const std::vector<int>& fileIds,
    int topK,
    float threshold,
    const std::atomic<bool>* cancel) {

    std::vector<SemanticHit> results;
    if (!m_db) return results;
    if (static_cast<int>(queryEmbedding.size()) != EMBEDDING_DIM) return results;

    // If fileIds is empty, fall back to scanning all (same as searchSimilar).
    if (fileIds.empty()) {
        return searchSimilar(queryEmbedding, topK, threshold, cancel);
    }

    // Build a parameterized IN clause: "WHERE file_id IN (?, ?, ?, ...)"
    // and bind each fileId. This avoids SQL injection and handles up to
    // ~999 parameters (SQLite default limit). If fileIds is larger, we
    // batch — but for hybrid search, fileIds is typically 200 (top BM25).
    const int SQLITE_MAX_PARAMS = 999;
    const int batchCount = std::min(static_cast<int>(fileIds.size()), SQLITE_MAX_PARAMS);

    // v1.7.15: version filter first (?1), then the file-ID list (?2..).
    QString sql = QString(
        "SELECT b.file_id, f.path, f.filename, b.embedding "
        "FROM BgeEmbeddings b "
        "LEFT JOIN Files f ON b.file_id = f.id "
        "WHERE b.status = 'completed' AND b.algo_version >= ?1 AND b.file_id IN (");
    for (int i = 0; i < batchCount; ++i) {
        if (i > 0) sql += ",";
        sql += "?";
    }
    sql += ");";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) {
        DS_WARN("BGE", "Failed to prepare searchSimilarFiltered stmt.");
        return results;
    }

    // Bind the version, then the file IDs.
    sqlite3_bind_int(stmt, 1, kAlgoVersion);
    for (int i = 0; i < batchCount; ++i) {
        sqlite3_bind_int64(stmt, i + 2, fileIds[i]);
    }

    // Compute cosine similarity for each row (v1.7.19: dot-only kernel,
    // scratch buffer reused across rows, cooperative cancel).
    std::vector<float> emb(EMBEDDING_DIM);
    const float* q = queryEmbedding.data();
    long long checkpoint = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (++checkpoint % 4096 == 0 && cancel && cancel->load()) {
            sqlite3_finalize(stmt);
            return results;
        }
        const int   fileId = sqlite3_column_int(stmt, 0);
        const void* blob   = sqlite3_column_blob(stmt, 3);
        const int   size   = sqlite3_column_bytes(stmt, 3);

        if (!blob || size != EMBEDDING_BYTES) continue;

        std::memcpy(emb.data(), blob, EMBEDDING_BYTES);

        const float sim = dotSimilarity(q, emb.data(), EMBEDDING_DIM);
        if (sim >= threshold) {
            const char* path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            SemanticHit hit;
            hit.fileId    = fileId;
            hit.filePath  = path  ? QString::fromUtf8(path) : QString();
            hit.filename  = name  ? QString::fromUtf8(name) : QString();
            hit.similarity = sim;
            results.push_back(hit);
        }
    }
    sqlite3_finalize(stmt);

    // Sort by similarity descending, trim to topK.
    std::sort(results.begin(), results.end(),
        [](const SemanticHit& a, const SemanticHit& b) {
            return a.similarity > b.similarity;
        });
    if (static_cast<int>(results.size()) > topK) {
        results.resize(topK);
    }
    return results;
}

bool BgeEmbeddingDb::hasEmbedding(int fileId) {
    if (!m_db) return false;
    // v1.7.15: version-aware. A row built by an OLDER embedding
    // algorithm (kAlgoVersion < current — including the garbage vectors
    // from the pre-fix hash-fallback tokenizer) counts as MISSING, so
    // every embedding path (single-file, batch, backfill) transparently
    // re-embeds stale documents with the current algorithm.
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db,
        "SELECT 1 FROM BgeEmbeddings "
        "WHERE file_id = ?1 AND algo_version >= ?2 LIMIT 1;",
        -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(stmt, 1, fileId);
    sqlite3_bind_int(stmt, 2, kAlgoVersion);
    const bool found = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return found;
}

bool BgeEmbeddingDb::deleteEmbedding(int fileId) {
    if (!m_db) return false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db,
        "DELETE FROM BgeEmbeddings WHERE file_id = ?1;",
        -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(stmt, 1, fileId);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

BgeEmbeddingDb::Stats BgeEmbeddingDb::getStats() {
    Stats s;
    if (!m_db) return s;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db,
        "SELECT COUNT(*), "
        "SUM(CASE WHEN status='completed' THEN 1 ELSE 0 END), "
        "SUM(CASE WHEN status='failed'    THEN 1 ELSE 0 END) "
        "FROM BgeEmbeddings;",
        -1, &stmt, nullptr) != SQLITE_OK) return s;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        s.total     = sqlite3_column_int(stmt, 0);
        s.completed = sqlite3_column_int(stmt, 1);
        s.failed    = sqlite3_column_int(stmt, 2);
    }
    sqlite3_finalize(stmt);
    return s;
}

float BgeEmbeddingDb::dotSimilarity(const float* a, const float* b, int n) {
    // v1.7.19: single-accumulator, 4-way unrolled dot product on
    // L2-normalized vectors (cosine == dot for them — see the header
    // invariant). Four partial sums break the dependency chain so MSVC
    // /O2 can pipeline (and typically auto-vectorize) the loop; one
    // horizontal add at the end. 384 dims = 96 unrolled steps, sub-µs
    // per row — versus the old 3-accumulator cosine that recomputed
    // normA (the QUERY norm — identical for every row) and normB (each
    // row's norm — recomputed on every search) plus two sqrt per row.
    float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
    int i = 0;
    for (; i + 4 <= n; i += 4) {
        s0 += a[i]     * b[i];
        s1 += a[i + 1] * b[i + 1];
        s2 += a[i + 2] * b[i + 2];
        s3 += a[i + 3] * b[i + 3];
    }
    float s = (s0 + s1) + (s2 + s3);
    for (; i < n; ++i) s += a[i] * b[i];
    return s;
}

// ── Phase 2: Chunked embeddings ────────────────────────────

bool BgeEmbeddingDb::storeChunks(int fileId, const std::vector<ChunkData>& chunks) {
    if (!m_db) return false;
    if (chunks.empty()) return false;

    // Delete existing chunks for this file first.
    deleteChunks(fileId);

    sqlite3_exec(m_db, "BEGIN;", nullptr, nullptr, nullptr);
    for (const auto& chunk : chunks) {
        if (static_cast<int>(chunk.embedding.size()) != EMBEDDING_DIM) continue;

        sqlite3_stmt* stmt = nullptr;
        // v1.7.15: stamp the algorithm version (see storeEmbedding).
        // v1.7.19: normalized copy — dot-similarity invariant (see the
        // normalizeInPlace note at the top of this file).
        const char* sql =
            "INSERT INTO EmbeddingChunks (file_id, chunk_index, start_offset, end_offset, embedding, created_at, status, algo_version) "
            "VALUES (?1, ?2, ?3, ?4, ?5, strftime('%s','now'), 'ready', ?6);";
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            std::vector<float> normalized = chunk.embedding;
            normalizeInPlace(normalized);
            sqlite3_bind_int64(stmt, 1, fileId);
            sqlite3_bind_int(stmt, 2, chunk.chunkIndex);
            sqlite3_bind_int(stmt, 3, chunk.startOffset);
            sqlite3_bind_int(stmt, 4, chunk.endOffset);
            sqlite3_bind_blob(stmt, 5, normalized.data(), EMBEDDING_BYTES, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 6, kAlgoVersion);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
    sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
    return true;
}

bool BgeEmbeddingDb::hasChunks(int fileId) {
    if (!m_db) return false;
    // v1.7.15: version-aware (same contract as hasEmbedding) — stale
    // chunk vectors count as missing and get regenerated.
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db,
        "SELECT 1 FROM EmbeddingChunks "
        "WHERE file_id = ?1 AND algo_version >= ?2 LIMIT 1;",
        -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(stmt, 1, fileId);
    sqlite3_bind_int(stmt, 2, kAlgoVersion);
    const bool found = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return found;
}

bool BgeEmbeddingDb::deleteChunks(int fileId) {
    if (!m_db) return false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db,
        "DELETE FROM EmbeddingChunks WHERE file_id = ?1;",
        -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(stmt, 1, fileId);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<SemanticHit> BgeEmbeddingDb::searchSimilarChunks(
    const std::vector<float>& queryEmbedding,
    const std::vector<int>& fileIds,
    int topK,
    float threshold,
    const std::atomic<bool>* cancel) {

    std::vector<SemanticHit> results;
    if (!m_db) return results;
    if (static_cast<int>(queryEmbedding.size()) != EMBEDDING_DIM) return results;
    if (fileIds.empty()) return results;

    // Build IN clause for file IDs (max 999 params).
    const int batchCount = std::min(static_cast<int>(fileIds.size()), 999);
    // v1.7.4: INNER JOIN Files — (a) chunks whose Files row is gone
    // (deleted/moved file that was not yet purged) can no longer surface
    // as ghost AI hits, (b) hits carry path/filename so the fusion layer
    // and the results pane can show and open them properly.
    // v1.7.15: version filter first (?1), then the file-ID list (?2..).
    QString sql = QString(
        "SELECT c.file_id, c.embedding, f.path, f.filename "
        "FROM EmbeddingChunks c INNER JOIN Files f ON f.id = c.file_id "
        "WHERE c.status = 'ready' AND c.algo_version >= ?1 AND c.file_id IN (");
    for (int i = 0; i < batchCount; ++i) {
        if (i > 0) sql += ",";
        sql += "?";
    }
    sql += ");";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }
    sqlite3_bind_int(stmt, 1, kAlgoVersion);
    for (int i = 0; i < batchCount; ++i) {
        sqlite3_bind_int64(stmt, i + 2, fileIds[i]);
    }

    // Compute cosine similarity for each chunk. Group by file_id,
    // keep the MAX similarity per file (best chunk wins).
    // v1.7.19: dot-only kernel + reused scratch buffer + cooperative
    // cancel + path strings converted once per file.
    std::map<int, float> bestPerFile;
    std::map<int, std::pair<QString, QString>> pathPerFile;  // v1.7.4
    std::vector<float> emb(EMBEDDING_DIM);
    const float* q = queryEmbedding.data();
    long long checkpoint = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (++checkpoint % 4096 == 0 && cancel && cancel->load()) {
            sqlite3_finalize(stmt);
            return results;
        }
        const int fileId = sqlite3_column_int(stmt, 0);
        const void* blob = sqlite3_column_blob(stmt, 1);
        const int size = sqlite3_column_bytes(stmt, 1);
        if (!blob || size != EMBEDDING_BYTES) continue;

        if (pathPerFile.find(fileId) == pathPerFile.end()) {
            const unsigned char* pth = sqlite3_column_text(stmt, 2);
            const unsigned char* fn  = sqlite3_column_text(stmt, 3);
            pathPerFile.emplace(fileId, std::make_pair(
                pth ? QString::fromUtf8(reinterpret_cast<const char*>(pth))
                    : QString(),
                fn  ? QString::fromUtf8(reinterpret_cast<const char*>(fn))
                    : QString()));
        }

        std::memcpy(emb.data(), blob, EMBEDDING_BYTES);

        const float sim = dotSimilarity(q, emb.data(), EMBEDDING_DIM);
        auto it = bestPerFile.find(fileId);
        if (it == bestPerFile.end() || sim > it->second) {
            bestPerFile[fileId] = sim;
        }
    }
    sqlite3_finalize(stmt);

    // Filter by threshold and sort.
    for (const auto& [fileId, sim] : bestPerFile) {
        if (sim >= threshold) {
            SemanticHit hit;
            hit.fileId = fileId;
            hit.similarity = sim;
            const auto pit = pathPerFile.find(fileId);
            if (pit != pathPerFile.end()) {
                hit.filePath = pit->second.first;
                hit.filename = pit->second.second;
            }
            results.push_back(hit);
        }
    }
    std::sort(results.begin(), results.end(),
        [](const SemanticHit& a, const SemanticHit& b) {
            return a.similarity > b.similarity;
        });
    if (static_cast<int>(results.size()) > topK) {
        results.resize(topK);
    }
    return results;
}

// Phase 3: Search ALL chunks (for RRF fusion — semantic runs independently).
std::vector<SemanticHit> BgeEmbeddingDb::searchSimilarChunksAll(
    const std::vector<float>& queryEmbedding,
    int topK,
    float threshold,
    const std::atomic<bool>* cancel) {

    std::vector<SemanticHit> results;
    if (!m_db) return results;
    if (static_cast<int>(queryEmbedding.size()) != EMBEDDING_DIM) return results;

    // ── v1.7.19 REWRITE — this scan WAS the AI search bottleneck ──
    // The old version paginated with "LIMIT 500 OFFSET k": SQLite must
    // walk and DISCARD the k rows before every batch, so a library with
    // N chunks cost ~N²/(2·500) row visits per search (a 100k-chunk
    // library = tens of millions of wasted walks) — plus a fresh
    // sqlite3_prepare + QString::arg SQL build per batch, a
    // heap-allocated 384-float vector PER ROW, and two QString UTF-8
    // conversions PER ROW even when the file had already been seen.
    //
    // Now: ONE prepared statement, ONE rowid-range cursor. chunk_id is
    // the table's INTEGER PRIMARY KEY (= rowid), so
    //   WHERE chunk_id > ?last ORDER BY chunk_id LIMIT ?batch
    // is a B-tree seek + sequential read: the whole table is walked
    // exactly ONCE regardless of batch count (O(N), not O(N²)). Rows
    // stream through sqlite3_step — memory stays bounded (only the
    // per-file best similarity + one path string per FILE are kept).
    // Similarity is the dot-only kernel, the scratch buffer is reused,
    // path strings convert once per file, and `cancel` aborts at each
    // batch boundary so a superseded search stops scanning early.
    std::map<int, float> bestPerFile;
    std::map<int, std::pair<QString, QString>> pathPerFile;  // v1.7.4
    m_lastBestSimilarity = -1.0f;

    const int BATCH = 4096;
    long long lastId = 0;  // chunk_id is AUTOINCREMENT — always > 0

    sqlite3_stmt* stmt = nullptr;
    // v1.7.4: INNER JOIN Files — ghost chunks of deleted/moved files are
    // skipped at the source. v1.7.15: only CURRENT-algorithm chunks.
    const char* sql =
        "SELECT c.chunk_id, c.file_id, c.embedding, f.path, f.filename "
        "FROM EmbeddingChunks c INNER JOIN Files f ON f.id = c.file_id "
        "WHERE c.status = 'ready' AND c.algo_version >= ?1 "
        "AND c.chunk_id > ?2 ORDER BY c.chunk_id LIMIT ?3;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }

    std::vector<float> emb(EMBEDDING_DIM);
    const float* q = queryEmbedding.data();
    bool cancelled = false;

    while (true) {
        sqlite3_reset(stmt);
        sqlite3_bind_int(stmt, 1, kAlgoVersion);
        sqlite3_bind_int64(stmt, 2, lastId);
        sqlite3_bind_int(stmt, 3, BATCH);

        int rowsRead = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ++rowsRead;
            lastId = sqlite3_column_int64(stmt, 0);
            const int fileId = sqlite3_column_int(stmt, 1);
            const void* blob = sqlite3_column_blob(stmt, 2);
            const int size = sqlite3_column_bytes(stmt, 2);
            if (!blob || size != EMBEDDING_BYTES) continue;

            // Path strings convert ONCE per file (first chunk wins) —
            // not once per chunk row.
            if (pathPerFile.find(fileId) == pathPerFile.end()) {
                const unsigned char* pth = sqlite3_column_text(stmt, 3);
                const unsigned char* fn  = sqlite3_column_text(stmt, 4);
                pathPerFile.emplace(fileId, std::make_pair(
                    pth ? QString::fromUtf8(reinterpret_cast<const char*>(pth))
                        : QString(),
                    fn  ? QString::fromUtf8(reinterpret_cast<const char*>(fn))
                        : QString()));
            }

            std::memcpy(emb.data(), blob, EMBEDDING_BYTES);

            const float sim = dotSimilarity(q, emb.data(), EMBEDDING_DIM);
            auto it = bestPerFile.find(fileId);
            if (it == bestPerFile.end() || sim > it->second) {
                bestPerFile[fileId] = sim;
            }
        }

        // Batch boundary: drain reached (rowsRead < BATCH) or cancelled.
        if (cancel && cancel->load()) { cancelled = true; break; }
        if (rowsRead < BATCH) break;
    }
    sqlite3_finalize(stmt);

    // Superseded mid-scan: drop the partial work — the caller discards
    // stale results by generation anyway.
    if (cancelled) return results;

    // Filter by threshold + sort + trim to topK.
    for (const auto& [fileId, sim] : bestPerFile) {
        if (sim > m_lastBestSimilarity) m_lastBestSimilarity = sim;
        if (sim >= threshold) {
            SemanticHit hit;
            hit.fileId = fileId;
            hit.similarity = sim;
            const auto pit = pathPerFile.find(fileId);
            if (pit != pathPerFile.end()) {
                hit.filePath = pit->second.first;
                hit.filename = pit->second.second;
            }
            results.push_back(hit);
        }
    }
    std::sort(results.begin(), results.end(),
        [](const SemanticHit& a, const SemanticHit& b) {
            return a.similarity > b.similarity;
        });
    if (static_cast<int>(results.size()) > topK) {
        results.resize(topK);
    }
    return results;
}

} // namespace DocuSearch
