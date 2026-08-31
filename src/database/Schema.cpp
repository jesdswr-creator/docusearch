// ============================================================
// Schema.cpp - Full schema for DocuSearch v1
// ============================================================

#include "Schema.h"
#include "Database.h"
#include "../core/Logger.h"

#include <sqlite3.h>

namespace DocuSearch {

bool Schema::initialize(Database& db) {
    // Always create the base schema (idempotent — CREATE TABLE IF NOT EXISTS).
    // This handles fresh installs and ensures all base tables exist.
    if (!createSchemaV1(db)) {
        return false;
    }
    // Run any pending migrations. This handles upgrades from older versions
    // (e.g. v1.0 databases that don't have BgeEmbeddings/SemanticSettings).
    // See MISSED-6 in the review report — without this, upgrading from v1.0
    // would crash on first query to BgeEmbeddings.
    if (!migrate(db)) {
        return false;
    }

    // ── One-time repair of a legacy AI setting ────────────────────
    // Early builds seeded similarity_threshold at 0.65. INSERT OR IGNORE
    // never updates an existing row, so databases created by those builds
    // kept 0.65 forever — and BGE-small-en-v1.5 almost never scores that
    // high on real content, so semantic search silently returned zero hits
    // on every query (the "AI is on but found nothing" report). Databases
    // created later use 0.45 (v1.7.5 aligned every seed with the engine
    // default); anything still >= 0.55 is a stale legacy
    // value (or a user-tuned value so strict it blocks every match) and is
    // reset to the engine's current default of 0.45.
    {
        sqlite3* raw = db.raw();
        if (raw && db.exec(
                "UPDATE SemanticSettings "
                "SET value='0.45', updated_at=strftime('%s','now') "
                "WHERE key='similarity_threshold' "
                "  AND CAST(value AS REAL) >= 0.55;")) {
            const int changed = sqlite3_changes(raw);
            if (changed > 0) {
                DS_INFO("Database",
                    QString("Repaired %1 legacy AI similarity threshold(s) "
                            ">= 0.55 back to 0.45 (semantic search was "
                            "previously unusable with the old default).")
                        .arg(changed));
            }
        }
    }
    return true;
}

int Schema::currentVersion(Database& db) {
    sqlite3* raw = db.raw();
    if (!raw) return 0;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(raw, "PRAGMA user_version;", -1, &stmt, nullptr) != SQLITE_OK)
        return 0;
    int v = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        v = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return v;
}

bool Schema::createSchemaV1(Database& db) {
    // Single transaction for the whole schema.
    const QStringList stmts = {
        // --- Files ---------------------------------------------------------
        R"SQL(CREATE TABLE IF NOT EXISTS Files (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            path            TEXT    NOT NULL UNIQUE,
            filename        TEXT    NOT NULL,
            extension       TEXT    NOT NULL DEFAULT '',
            size            INTEGER NOT NULL DEFAULT 0,
            created_date    INTEGER NOT NULL DEFAULT 0,
            modified_date   INTEGER NOT NULL DEFAULT 0,
            hash            TEXT    DEFAULT '',
            indexing_status TEXT    NOT NULL DEFAULT 'pending',
            ocr_status      TEXT    NOT NULL DEFAULT 'pending',
            is_favorite     INTEGER NOT NULL DEFAULT 0,
            open_count      INTEGER NOT NULL DEFAULT 0,
            last_opened     INTEGER NOT NULL DEFAULT 0,
            indexed_at      INTEGER NOT NULL DEFAULT 0
        );)SQL",

        "CREATE INDEX IF NOT EXISTS idx_files_filename        ON Files(filename);",
        "CREATE INDEX IF NOT EXISTS idx_files_extension       ON Files(extension);",
        "CREATE INDEX IF NOT EXISTS idx_files_modified        ON Files(modified_date);",
        "CREATE INDEX IF NOT EXISTS idx_files_status          ON Files(indexing_status);",
        "CREATE INDEX IF NOT EXISTS idx_files_ocr             ON Files(ocr_status);",
        "CREATE INDEX IF NOT EXISTS idx_files_favorite        ON Files(is_favorite);",
        "CREATE INDEX IF NOT EXISTS idx_files_hash            ON Files(hash);",

        // --- Document text (per-file extracted text) -----------------------
        R"SQL(CREATE TABLE IF NOT EXISTS DocumentText (
            file_id         INTEGER PRIMARY KEY REFERENCES Files(id) ON DELETE CASCADE,
            extracted_text  TEXT    NOT NULL DEFAULT '',
            text_source     TEXT    NOT NULL DEFAULT '',   -- 'native' | 'ocr' | 'both'
            char_count      INTEGER NOT NULL DEFAULT 0,
            updated_at      INTEGER NOT NULL DEFAULT 0
        );)SQL",

        // --- Tags ----------------------------------------------------------
        R"SQL(CREATE TABLE IF NOT EXISTS Tags (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            file_id         INTEGER NOT NULL REFERENCES Files(id) ON DELETE CASCADE,
            tag             TEXT    NOT NULL,
            UNIQUE(file_id, tag)
        );)SQL",
        "CREATE INDEX IF NOT EXISTS idx_tags_file ON Tags(file_id);",
        "CREATE INDEX IF NOT EXISTS idx_tags_tag  ON Tags(tag);",

        // --- Notes (one per file) -----------------------------------------
        R"SQL(CREATE TABLE IF NOT EXISTS Notes (
            file_id         INTEGER PRIMARY KEY REFERENCES Files(id) ON DELETE CASCADE,
            note            TEXT    NOT NULL DEFAULT '',
            updated_at      INTEGER NOT NULL DEFAULT 0
        );)SQL",

        // --- Favorites / recent (denormalized convenience) ----------------
        // We use Files.is_favorite + Files.last_opened already.

        // --- Saved searches -----------------------------------------------
        R"SQL(CREATE TABLE IF NOT EXISTS SavedSearches (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            search_name     TEXT    NOT NULL UNIQUE,
            search_query    TEXT    NOT NULL,
            created_at      INTEGER NOT NULL DEFAULT 0
        );)SQL",

        // --- Settings (key/value) -----------------------------------------
        R"SQL(CREATE TABLE IF NOT EXISTS Settings (
            key             TEXT PRIMARY KEY,
            value           TEXT NOT NULL DEFAULT ''
        );)SQL",

        // --- Indexing log (recent errors / events) ------------------------
        R"SQL(CREATE TABLE IF NOT EXISTS IndexingLog (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            file_id         INTEGER,
            event           TEXT    NOT NULL,
            message         TEXT    NOT NULL DEFAULT '',
            created_at      INTEGER NOT NULL DEFAULT 0
        );)SQL",
        "CREATE INDEX IF NOT EXISTS idx_log_file ON IndexingLog(file_id);",
        "CREATE INDEX IF NOT EXISTS idx_log_time ON IndexingLog(created_at);",

        // --- FTS5 full-text index over filename + body --------------------
        // Phase 8: Use trigram tokenizer for CJK (Chinese/Japanese/Korean)
        // support. Trigram splits text into 3-character sliding windows,
        // which handles CJK languages that don't have word boundaries.
        // Falls back to unicode61 if trigram not available (SQLite < 3.34).
        R"SQL(CREATE VIRTUAL TABLE IF NOT EXISTS SearchIndex USING fts5(
            filename,
            content,
            path UNINDEXED,
            extension UNINDEXED,
            file_id UNINDEXED,
            tokenize = 'trigram'
        );)SQL",

        // --- BGE embedding storage (semantic search) ----------------------
        // Each file gets at most one 384-float embedding blob (1536 bytes).
        R"SQL(CREATE TABLE IF NOT EXISTS BgeEmbeddings (
            file_id     INTEGER PRIMARY KEY,
            embedding   BLOB    NOT NULL,
            created_at  INTEGER NOT NULL DEFAULT 0,
            updated_at  INTEGER NOT NULL DEFAULT 0,
            status      TEXT    NOT NULL DEFAULT 'completed',
            FOREIGN KEY(file_id) REFERENCES Files(id) ON DELETE CASCADE
        );)SQL",
        "CREATE INDEX IF NOT EXISTS idx_bge_status ON BgeEmbeddings(status);",

        // --- Semantic search settings (key/value) -------------------------
        R"SQL(CREATE TABLE IF NOT EXISTS SemanticSettings (
            key         TEXT PRIMARY KEY,
            value       TEXT,
            updated_at  INTEGER NOT NULL DEFAULT 0
        );)SQL",

        // Default semantic-search settings (inserted once, never overwritten).
        "INSERT OR IGNORE INTO SemanticSettings (key, value) VALUES "
        "('semantic_enabled',     'false');",
        "INSERT OR IGNORE INTO SemanticSettings (key, value) VALUES "
        "('model_path',           './models/bge-small-en-v1.5/model.onnx');",
        "INSERT OR IGNORE INTO SemanticSettings (key, value) VALUES "
        "('similarity_threshold', '0.45');",
        "INSERT OR IGNORE INTO SemanticSettings (key, value) VALUES "
        "('semantic_weight',      '0.40');",
        "INSERT OR IGNORE INTO SemanticSettings (key, value) VALUES "
        "('top_k',                '20');",
    };

    bool ok = true;
    db.begin();
    for (const auto& s : stmts) {
        if (!db.exec(s)) {
            ok = false;
            break;
        }
    }
    if (ok) {
        // Set schema version
        db.exec(QString("PRAGMA user_version = %1;").arg(kLatestSchemaVersion));
        db.commit();
        DS_INFO("Database", "Schema v1 initialized.");
    } else {
        db.rollback();
        DS_ERROR("Database", "Schema initialization failed.");
    }
    return ok;
}

bool Schema::migrate(Database& db) {
    const int cur = currentVersion(db);
    if (cur >= kLatestSchemaVersion) return true;
    DS_INFO("Database", QString("Migrating schema from v%1 to v%2")
                .arg(cur).arg(kLatestSchemaVersion));

    // v0 (fresh install or pre-versioning) → v1: create base schema.
    if (cur < 1) {
        if (!createSchemaV1(db)) return false;
    }

    // v1 → v2: add BGE embedding tables + semantic settings.
    // Uses CREATE TABLE IF NOT EXISTS so it's safe to run on a database
    // that already has these tables (e.g. if v1 schema creation already
    // added them in a newer build).
    if (cur < 2) {
        if (!migrateV1ToV2(db)) return false;
    }

    // v2 → v3: add EmbeddingChunks table for chunked embeddings.
    // This is the biggest AI quality improvement — instead of one embedding
    // per entire document, we store multiple embeddings per chunk (256 tokens,
    // 64 overlap). This lets the AI find the RELEVANT PART of a long document.
    if (cur < 3) {
        if (!migrateV2ToV3(db)) return false;
    }

    db.exec(QString("PRAGMA user_version = %1;").arg(kLatestSchemaVersion));
    DS_INFO("Database", QString("Schema migrated to v%1").arg(kLatestSchemaVersion));
    return true;
}

bool Schema::migrateV1ToV2(Database& db) {
    // Add BgeEmbeddings table (file_id PK, 384-float embedding as BLOB).
    // CREATE TABLE IF NOT EXISTS is safe even if the table already exists.
    if (!db.exec("CREATE TABLE IF NOT EXISTS BgeEmbeddings ("
                 "  file_id     INTEGER PRIMARY KEY,"
                 "  embedding   BLOB    NOT NULL,"
                 "  created_at  INTEGER NOT NULL DEFAULT 0,"
                 "  updated_at  INTEGER NOT NULL DEFAULT 0,"
                 "  status      TEXT    NOT NULL DEFAULT 'completed',"
                 "  FOREIGN KEY(file_id) REFERENCES Files(id) ON DELETE CASCADE"
                 ");")) {
        DS_ERROR("Database", "Failed to create BgeEmbeddings table during v1→v2 migration.");
        return false;
    }
    db.exec("CREATE INDEX IF NOT EXISTS idx_bge_status ON BgeEmbeddings(status);");

    // Add SemanticSettings table (key/value).
    if (!db.exec("CREATE TABLE IF NOT EXISTS SemanticSettings ("
                 "  key         TEXT PRIMARY KEY,"
                 "  value       TEXT,"
                 "  updated_at  INTEGER NOT NULL DEFAULT 0"
                 ");")) {
        DS_ERROR("Database", "Failed to create SemanticSettings table during v1→v2 migration.");
        return false;
    }

    // Insert default semantic settings (INSERT OR IGNORE = don't overwrite existing).
    db.exec("INSERT OR IGNORE INTO SemanticSettings (key, value) VALUES "
            "('semantic_enabled',     'false');");
    db.exec("INSERT OR IGNORE INTO SemanticSettings (key, value) VALUES "
            "('model_path',           './models/bge-small-en-v1.5/model.onnx');");
    db.exec("INSERT OR IGNORE INTO SemanticSettings (key, value) VALUES "
            "('similarity_threshold', '0.45');");
    db.exec("INSERT OR IGNORE INTO SemanticSettings (key, value) VALUES "
            "('semantic_weight',      '0.40');");
    db.exec("INSERT OR IGNORE INTO SemanticSettings (key, value) VALUES "
            "('top_k',                '20');");

    DS_INFO("Database", "v1→v2 migration complete: added BgeEmbeddings + SemanticSettings tables.");
    return true;
}

bool Schema::migrateV2ToV3(Database& db) {
    // Phase 2: EmbeddingChunks table — one document can have multiple
    // chunk embeddings (256 tokens each, 64 overlap). This dramatically
    // improves search quality for long documents.
    if (!db.exec("CREATE TABLE IF NOT EXISTS EmbeddingChunks ("
                 "  chunk_id    INTEGER PRIMARY KEY AUTOINCREMENT,"
                 "  file_id     INTEGER NOT NULL REFERENCES Files(id) ON DELETE CASCADE,"
                 "  chunk_index INTEGER NOT NULL,"
                 "  start_offset INTEGER DEFAULT 0,"
                 "  end_offset   INTEGER DEFAULT 0,"
                 "  embedding   BLOB    NOT NULL,"
                 "  created_at  INTEGER NOT NULL DEFAULT 0,"
                 "  status      TEXT    NOT NULL DEFAULT 'ready',"
                 "  UNIQUE(file_id, chunk_index)"
                 ");")) {
        DS_ERROR("Database", "Failed to create EmbeddingChunks table during v2→v3 migration.");
        return false;
    }
    db.exec("CREATE INDEX IF NOT EXISTS idx_chunks_file ON EmbeddingChunks(file_id);");
    db.exec("CREATE INDEX IF NOT EXISTS idx_chunks_status ON EmbeddingChunks(status);");

    // Add default chunk settings to SemanticSettings.
    db.exec("INSERT OR IGNORE INTO SemanticSettings (key, value) VALUES "
            "('chunk_size', '256');");
    db.exec("INSERT OR IGNORE INTO SemanticSettings (key, value) VALUES "
            "('chunk_overlap', '64');");
    db.exec("INSERT OR IGNORE INTO SemanticSettings (key, value) VALUES "
            "('embedding_mode', 'chunk');");

    DS_INFO("Database", "v2→v3 migration complete: added EmbeddingChunks table.");
    return true;
}

} // namespace DocuSearch
