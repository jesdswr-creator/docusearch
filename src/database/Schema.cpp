// ============================================================
// Schema.cpp - Full schema for DocuSearch v1
// ============================================================

#include "Schema.h"
#include "Database.h"
#include "../core/Logger.h"

#include <sqlite3.h>

namespace DocuSearch {

bool Schema::initialize(Database& db) {
    // CRITICAL (v1.7.15): capture the on-disk version BEFORE creating
    // anything. An earlier build stamped user_version = latest at the
    // END of createSchemaV1(), clobbering a legacy database's version
    // BEFORE migrate() ever read it — so migrate() was a permanent
    // no-op on every EXISTING database (all migration steps silently
    // skipped, and e.g. the EmbeddingChunks table was never created for
    // anyone). Fresh databases (version 0) are stamped here; legacy
    // ones keep their original version so the right migrations run.
    const bool fresh = (currentVersion(db) == 0);

    // Always create the base schema (idempotent — CREATE TABLE IF NOT EXISTS).
    // This handles fresh installs and ensures all base tables exist.
    if (!createSchemaV1(db)) {
        return false;
    }
    if (fresh) {
        // Nothing to migrate — the complete current schema was just
        // created. Stamp it.
        db.exec(QString("PRAGMA user_version = %1;").arg(kLatestSchemaVersion));
    }
    // Run any pending migrations. This handles upgrades from older versions
    // (e.g. v1.0 databases that don't have BgeEmbeddings/SemanticSettings).
    // See MISSED-6 in the review report — without this, upgrading from v1.0
    // would crash on first query to BgeEmbeddings.
    //
    // NOTE (v1.7.15): an earlier build had a "repair" block here that ran on
    // EVERY startup and force-reset similarity_threshold to 0.45 whenever it
    // was >= 0.55 — silently destroying any threshold the user had tuned in
    // Settings (drag the slider up to cut noise → next launch reverts it).
    // It is GONE and nothing here ever touches similarity_threshold —
    // v1.7.14 moved the noise gate to a dedicated additions threshold in
    // HybridSearchEngine (m_additionsThreshold), so the stored value and
    // any user-tuned setting are left alone.
    if (!migrate(db)) {
        return false;
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
        // algo_version stamps WHICH embedding algorithm produced the vector
        // (see BgeEmbeddingDb::kAlgoVersion). Rows with an older version
        // were built by a broken/older tokenizer and are re-embedded
        // automatically instead of serving garbage "AI matches".
        R"SQL(CREATE TABLE IF NOT EXISTS BgeEmbeddings (
            file_id     INTEGER PRIMARY KEY,
            embedding   BLOB    NOT NULL,
            created_at  INTEGER NOT NULL DEFAULT 0,
            updated_at  INTEGER NOT NULL DEFAULT 0,
            status      TEXT    NOT NULL DEFAULT 'completed',
            algo_version INTEGER NOT NULL DEFAULT 0,
            FOREIGN KEY(file_id) REFERENCES Files(id) ON DELETE CASCADE
        );)SQL",
        "CREATE INDEX IF NOT EXISTS idx_bge_status ON BgeEmbeddings(status);",

        // v1.7.15: chunked embeddings. Previously this table was ONLY
        // created by the v2→v3 migration — but a FRESH install gets
        // user_version = latest straight from createSchemaV1, so the
        // migration never ran and brand-new installs silently had NO
        // EmbeddingChunks table (every chunk query failed to prepare,
        // semantic search ran document-level only, and the chunk
        // backfill found nothing to do). Fresh installs must get the
        // complete schema here.
        R"SQL(CREATE TABLE IF NOT EXISTS EmbeddingChunks (
            chunk_id    INTEGER PRIMARY KEY AUTOINCREMENT,
            file_id     INTEGER NOT NULL REFERENCES Files(id) ON DELETE CASCADE,
            chunk_index INTEGER NOT NULL,
            start_offset INTEGER DEFAULT 0,
            end_offset   INTEGER DEFAULT 0,
            embedding   BLOB    NOT NULL,
            created_at  INTEGER NOT NULL DEFAULT 0,
            status      TEXT    NOT NULL DEFAULT 'ready',
            algo_version INTEGER NOT NULL DEFAULT 0,
            UNIQUE(file_id, chunk_index)
        );)SQL",
        "CREATE INDEX IF NOT EXISTS idx_chunks_file ON EmbeddingChunks(file_id);",
        "CREATE INDEX IF NOT EXISTS idx_chunks_status ON EmbeddingChunks(status);",

        // --- Semantic search settings (key/value) -------------------------
        R"SQL(CREATE TABLE IF NOT EXISTS SemanticSettings (
            key         TEXT PRIMARY KEY,
            value       TEXT,
            updated_at  INTEGER NOT NULL DEFAULT 0
        );)SQL",

        // Default semantic-search settings (inserted once, never overwritten).
        // similarity_threshold stays 0.45 = HybridSearchEngine's m_threshold
        // default (ranking/annotation + retrieval budget). The AI-NOISE gate
        // is separate since v1.7.14: AI-only additions must clear
        // m_additionsThreshold (0.60) in the engine, so the stored value
        // here is what the Settings slider shows and must not be "fixed"
        // by schema code.
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
        "INSERT OR IGNORE INTO SemanticSettings (key, value) VALUES "
        "('chunk_size', '256');",
        "INSERT OR IGNORE INTO SemanticSettings (key, value) VALUES "
        "('chunk_overlap', '64');",
        "INSERT OR IGNORE INTO SemanticSettings (key, value) VALUES "
        "('embedding_mode', 'chunk');",
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
        // NOTE (v1.7.15): the schema version is NO LONGER stamped here.
        // Stamping user_version = latest before migrate() ran clobbered
        // legacy databases' versions, so every migration step was
        // silently skipped on existing databases (see initialize()).
        // Fresh databases are stamped by initialize(); migrated ones by
        // migrate() at the end of their upgrade.
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

    // v3 → v4: stamp embeddings with the algorithm version that built
    // them so vectors from the old broken hash-fallback tokenizer are
    // detected and re-embedded (they scored meaningless against real
    // queries). Does NOT touch similarity_threshold (see migrateV3ToV4).
    if (cur < 4) {
        if (!migrateV3ToV4(db)) return false;
    }

    db.exec(QString("PRAGMA user_version = %1;").arg(kLatestSchemaVersion));
    DS_INFO("Database", QString("Schema migrated to v%1").arg(kLatestSchemaVersion));
    return true;
}

bool Schema::migrateV1ToV2(Database& db) {
    // Add BgeEmbeddings table (file_id PK, 384-float embedding as BLOB).
    // CREATE TABLE IF NOT EXISTS is safe even if the table already exists
    // (v3 databases already have the table WITHOUT algo_version — the
    // v3→v4 migration adds that column to pre-existing tables).
    if (!db.exec("CREATE TABLE IF NOT EXISTS BgeEmbeddings ("
                 "  file_id     INTEGER PRIMARY KEY,"
                 "  embedding   BLOB    NOT NULL,"
                 "  created_at  INTEGER NOT NULL DEFAULT 0,"
                 "  updated_at  INTEGER NOT NULL DEFAULT 0,"
                 "  status      TEXT    NOT NULL DEFAULT 'completed',"
                 "  algo_version INTEGER NOT NULL DEFAULT 0,"
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
    // (similarity_threshold stays 0.45 = the engine's m_threshold default;
    // see the createSchemaV1 comment.)
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
    // (v1.7.15: fresh installs create this table in createSchemaV1, so
    // this is a no-op for them via IF NOT EXISTS.)
    if (!db.exec("CREATE TABLE IF NOT EXISTS EmbeddingChunks ("
                 "  chunk_id    INTEGER PRIMARY KEY AUTOINCREMENT,"
                 "  file_id     INTEGER NOT NULL REFERENCES Files(id) ON DELETE CASCADE,"
                 "  chunk_index INTEGER NOT NULL,"
                 "  start_offset INTEGER DEFAULT 0,"
                 "  end_offset   INTEGER DEFAULT 0,"
                 "  embedding   BLOB    NOT NULL,"
                 "  created_at  INTEGER NOT NULL DEFAULT 0,"
                 "  status      TEXT    NOT NULL DEFAULT 'ready',"
                 "  algo_version INTEGER NOT NULL DEFAULT 0,"
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

bool Schema::migrateV3ToV4(Database& db) {
    // ── (a) algo_version column on both embedding tables ─────────
    // The column tells the backfill which rows hold vectors from an
    // OLD (possibly broken) embedding algorithm. Pre-existing tables
    // from v1/v2/v3 builds do not have it; freshly created ones do,
    // so a "duplicate column" error here means the column already
    // exists — success, not failure.
    const struct { const char* table; } tables[] = {
        { "BgeEmbeddings" }, { "EmbeddingChunks" },
    };
    for (const auto& t : tables) {
        const QString sql = QString(
            "ALTER TABLE %1 ADD COLUMN algo_version INTEGER NOT NULL DEFAULT 0;")
            .arg(t.table);
        QString err;
        if (!db.exec(sql, &err)) {
            if (!err.contains(QStringLiteral("duplicate column"))) {
                DS_ERROR("Database",
                    QString("v3→v4: failed to add algo_version to %1: %2")
                        .arg(t.table, err));
                return false;
            }
            // Already present (table was created by a build that had
            // the column) — nothing to do.
        }
    }

    // NOTE: this migration deliberately does NOT touch
    // similarity_threshold. v1.7.14 moved the AI-noise gate out of the
    // stored setting and into the engine (HybridSearchEngine::
    // m_additionsThreshold = 0.60 for AI-only additions), so rewriting
    // the stored value would only fight user tuning.

    DS_INFO("Database", "v3→v4 migration complete: algo_version stamping on both embedding tables.");
    return true;
}

} // namespace DocuSearch
