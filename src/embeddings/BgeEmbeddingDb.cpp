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
    const char* sql =
        "INSERT OR REPLACE INTO BgeEmbeddings "
        "(file_id, embedding, created_at, updated_at, status) "
        "VALUES (?1, ?2, strftime('%s','now'), strftime('%s','now'), 'completed');";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        DS_WARN("BGE", "Failed to prepare storeEmbedding stmt.");
        return false;
    }

    sqlite3_bind_int64(stmt, 1, fileId);
    sqlite3_bind_blob(stmt, 2, embedding.data(), EMBEDDING_BYTES, SQLITE_TRANSIENT);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool BgeEmbeddingDb::getEmbedding(int fileId, std::vector<float>& outEmbedding) {
    outEmbedding.clear();
    if (!m_db) return false;

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT embedding FROM BgeEmbeddings WHERE file_id = ?1 AND status = 'completed';";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(stmt, 1, fileId);

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
    float threshold) {

    std::vector<SemanticHit> results;
    if (!m_db) return results;
    if (static_cast<int>(queryEmbedding.size()) != EMBEDDING_DIM) return results;

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT b.file_id, f.path, f.filename, b.embedding "
        "FROM BgeEmbeddings b "
        "LEFT JOIN Files f ON b.file_id = f.id "
        "WHERE b.status = 'completed' "
        "LIMIT 50000;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        DS_WARN("BGE", "Failed to prepare searchSimilar stmt.");
        return results;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const int   fileId = sqlite3_column_int(stmt, 0);
        const char* path   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* name   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const void* blob   = sqlite3_column_blob(stmt, 3);
        const int   size   = sqlite3_column_bytes(stmt, 3);

        if (!blob || size != EMBEDDING_BYTES) continue;

        std::vector<float> emb(EMBEDDING_DIM);
        std::memcpy(emb.data(), blob, EMBEDDING_BYTES);

        const float sim = cosineSimilarity(queryEmbedding, emb);
        if (sim >= threshold) {
            SemanticHit hit;
            hit.fileId    = fileId;
            hit.filePath  = path  ? QString::fromUtf8(path) : QString();
            hit.filename  = name  ? QString::fromUtf8(name) : QString();
            hit.similarity = sim;
            results.push_back(hit);
        }
    }
    sqlite3_finalize(stmt);

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
    float threshold) {

    std::vector<SemanticHit> results;
    if (!m_db) return results;
    if (static_cast<int>(queryEmbedding.size()) != EMBEDDING_DIM) return results;

    // If fileIds is empty, fall back to scanning all (same as searchSimilar).
    if (fileIds.empty()) {
        return searchSimilar(queryEmbedding, topK, threshold);
    }

    // Build a parameterized IN clause: "WHERE file_id IN (?, ?, ?, ...)"
    // and bind each fileId. This avoids SQL injection and handles up to
    // ~999 parameters (SQLite default limit). If fileIds is larger, we
    // batch — but for hybrid search, fileIds is typically 200 (top BM25).
    const int SQLITE_MAX_PARAMS = 999;
    const int batchCount = std::min(static_cast<int>(fileIds.size()), SQLITE_MAX_PARAMS);

    QString sql = QString(
        "SELECT b.file_id, f.path, f.filename, b.embedding "
        "FROM BgeEmbeddings b "
        "LEFT JOIN Files f ON b.file_id = f.id "
        "WHERE b.status = 'completed' AND b.file_id IN (");
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

    // Bind the file IDs.
    for (int i = 0; i < batchCount; ++i) {
        sqlite3_bind_int64(stmt, i + 1, fileIds[i]);
    }

    // Compute cosine similarity for each row.
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const int   fileId = sqlite3_column_int(stmt, 0);
        const char* path   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* name   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const void* blob   = sqlite3_column_blob(stmt, 3);
        const int   size   = sqlite3_column_bytes(stmt, 3);

        if (!blob || size != EMBEDDING_BYTES) continue;

        std::vector<float> emb(EMBEDDING_DIM);
        std::memcpy(emb.data(), blob, EMBEDDING_BYTES);

        const float sim = cosineSimilarity(queryEmbedding, emb);
        if (sim >= threshold) {
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
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db,
        "SELECT 1 FROM BgeEmbeddings WHERE file_id = ?1 LIMIT 1;",
        -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(stmt, 1, fileId);
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

float BgeEmbeddingDb::cosineSimilarity(const std::vector<float>& a,
                                        const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) return 0.0f;
    float dot = 0.0f, normA = 0.0f, normB = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        dot   += a[i] * b[i];
        normA += a[i] * a[i];
        normB += b[i] * b[i];
    }
    normA = std::sqrt(normA);
    normB = std::sqrt(normB);
    if (normA < 1e-9f || normB < 1e-9f) return 0.0f;
    return dot / (normA * normB);
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
        const char* sql =
            "INSERT INTO EmbeddingChunks (file_id, chunk_index, start_offset, end_offset, embedding, created_at, status) "
            "VALUES (?1, ?2, ?3, ?4, ?5, strftime('%s','now'), 'ready');";
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, fileId);
            sqlite3_bind_int(stmt, 2, chunk.chunkIndex);
            sqlite3_bind_int(stmt, 3, chunk.startOffset);
            sqlite3_bind_int(stmt, 4, chunk.endOffset);
            sqlite3_bind_blob(stmt, 5, chunk.embedding.data(), EMBEDDING_BYTES, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
    sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
    return true;
}

bool BgeEmbeddingDb::hasChunks(int fileId) {
    if (!m_db) return false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db,
        "SELECT 1 FROM EmbeddingChunks WHERE file_id = ?1 LIMIT 1;",
        -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(stmt, 1, fileId);
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
    float threshold) {

    std::vector<SemanticHit> results;
    if (!m_db) return results;
    if (static_cast<int>(queryEmbedding.size()) != EMBEDDING_DIM) return results;
    if (fileIds.empty()) return results;

    // Build IN clause for file IDs (max 999 params).
    const int batchCount = std::min(static_cast<int>(fileIds.size()), 999);
    QString sql = QString(
        "SELECT file_id, embedding FROM EmbeddingChunks "
        "WHERE status = 'ready' AND file_id IN (");
    for (int i = 0; i < batchCount; ++i) {
        if (i > 0) sql += ",";
        sql += "?";
    }
    sql += ");";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }
    for (int i = 0; i < batchCount; ++i) {
        sqlite3_bind_int64(stmt, i + 1, fileIds[i]);
    }

    // Compute cosine similarity for each chunk. Group by file_id,
    // keep the MAX similarity per file (best chunk wins).
    std::map<int, float> bestPerFile;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const int fileId = sqlite3_column_int(stmt, 0);
        const void* blob = sqlite3_column_blob(stmt, 1);
        const int size = sqlite3_column_bytes(stmt, 1);
        if (!blob || size != EMBEDDING_BYTES) continue;

        std::vector<float> emb(EMBEDDING_DIM);
        std::memcpy(emb.data(), blob, EMBEDDING_BYTES);

        const float sim = cosineSimilarity(queryEmbedding, emb);
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
