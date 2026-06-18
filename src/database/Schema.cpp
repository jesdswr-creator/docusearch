// ============================================================
// Schema.cpp — Full schema for DocuSearch v1
// ============================================================

#include "Schema.h"
#include "Database.h"
#include "../core/Logger.h"

#include <sqlite3.h>

namespace DocuSearch {

bool Schema::initialize(Database& db) {
    return createSchemaV1(db);
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
        // External content table pointing at DocumentText + Files.
        R"SQL(CREATE VIRTUAL TABLE IF NOT EXISTS SearchIndex USING fts5(
            filename,
            content,
            path UNINDEXED,
            extension UNINDEXED,
            file_id UNINDEXED,
            tokenize = 'unicode61 remove_diacritics 2'
        );)SQL",
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
    DS_INFO("Database", QString("Migrating schema from v%1 to v%2").arg(cur).arg(kLatestSchemaVersion));
    // Future: apply deltas per version.
    return createSchemaV1(db);
}

} // namespace DocuSearch
