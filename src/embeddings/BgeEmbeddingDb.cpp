// ============================================================
// BgeEmbeddingDb.cpp - SQLite blob storage for embeddings
// ============================================================

#include "BgeEmbeddingDb.h"
#include "../core/Logger.h"

#include <sqlite3.h>
#include <cstring>
#include <cmath>
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

} // namespace DocuSearch
