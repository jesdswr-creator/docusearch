#pragma once

// ============================================================
// Schema.h — Database schema creation & migrations
// ============================================================

#include <QString>
#include <QStringList>

namespace DocuSearch {

class Database;

class Schema {
public:
    // Create all tables & FTS5 indexes if missing. Idempotent.
    static bool initialize(Database& db);

    // Returns current schema version (stored in PRAGMA user_version).
    static int currentVersion(Database& db);

    // Apply migrations from currentVersion -> kLatestSchemaVersion.
    static bool migrate(Database& db);

    static constexpr int kLatestSchemaVersion = 1;

private:
    static bool createSchemaV1(Database& db);
};

} // namespace DocuSearch
