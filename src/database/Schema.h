#pragma once

// ============================================================
// Schema.h - Database schema creation & migrations
// ============================================================

#include <QString>
#include <QStringList>

namespace DocuSearch {

class Database;

class Schema {
public:
    // Create all tables & FTS5 indexes if missing. Idempotent.
    // Also runs migrations if the on-disk schema version is older
    // than kLatestSchemaVersion. Safe to call on every startup.
    static bool initialize(Database& db);

    // Returns current schema version (stored in PRAGMA user_version).
    static int currentVersion(Database& db);

    // Apply migrations from currentVersion -> kLatestSchemaVersion.
    // Each migration step is idempotent (uses CREATE TABLE IF NOT EXISTS,
    // ALTER TABLE ADD COLUMN with try/catch for "duplicate column" errors).
    static bool migrate(Database& db);

    // Bumped to 2 when BgeEmbeddings + SemanticSettings tables were added.
    // v1 → v2 migration adds those tables (via CREATE TABLE IF NOT EXISTS,
    // so it's safe to run on a v2 database too).
    static constexpr int kLatestSchemaVersion = 3;

private:
    static bool createSchemaV1(Database& db);
    static bool migrateV1ToV2(Database& db);
    static bool migrateV2ToV3(Database& db);
};

} // namespace DocuSearch
