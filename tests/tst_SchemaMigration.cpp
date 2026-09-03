// ============================================================
// tst_SchemaMigration.cpp — Unit tests for database/Schema v3→v4
// ============================================================
//
// Covers the v1.7.15 v3→v4 migration + the version-handling fixes:
//   • fresh installs get the COMPLETE v4 schema — including
//     EmbeddingChunks, which used to only exist via the v2→v3
//     migration that never ran (createSchemaV1 clobbered the schema
//     version, so migrate() was a permanent no-op on every database)
//   • similarity_threshold is NEVER touched by schema code (the
//     AI-noise gate moved to the engine in v1.7.14 —
//     HybridSearchEngine::m_additionsThreshold) — legacy defaults AND
//     user-tuned values survive every migration and every launch
//   • legacy databases' version is preserved through
//     createSchemaV1 so migrate() actually runs for them
//   • the algo_version stamp: hasEmbedding() treats older-version
//     rows as MISSING so garbage vectors from the broken
//     hash-fallback tokenizer builds get re-embedded automatically
//
// Uses the Qt Test framework.
// ============================================================

#include "../src/database/Database.h"
#include "../src/database/Schema.h"
#include "../src/embeddings/BgeEmbeddingDb.h"

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <sqlite3.h>
#include <memory>
#include <vector>

using DocuSearch::Database;
using DocuSearch::Schema;
using DocuSearch::BgeEmbeddingDb;

class TestSchemaMigration : public QObject {
    Q_OBJECT

private:
    static QString settingValue(Database& db, const QString& key) {
        sqlite3* raw = db.raw();
        sqlite3_stmt* s = nullptr;
        QString v;
        if (sqlite3_prepare_v2(raw,
                "SELECT value FROM SemanticSettings WHERE key = ?1;",
                -1, &s, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(s, 1, key.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(s) == SQLITE_ROW) {
                const unsigned char* t = sqlite3_column_text(s, 0);
                if (t) v = QString::fromUtf8(reinterpret_cast<const char*>(t));
            }
            sqlite3_finalize(s);
        }
        return v;
    }

    static bool tableExists(Database& db, const QString& table) {
        sqlite3* raw = db.raw();
        sqlite3_stmt* s = nullptr;
        bool exists = false;
        if (sqlite3_prepare_v2(raw,
                "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?1;",
                -1, &s, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(s, 1, table.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            exists = (sqlite3_step(s) == SQLITE_ROW);
            sqlite3_finalize(s);
        }
        return exists;
    }

    static bool tableHasColumn(Database& db, const QString& table, const QString& column) {
        sqlite3* raw = db.raw();
        sqlite3_stmt* s = nullptr;
        bool has = false;
        const QString sql = QStringLiteral("PRAGMA table_info(%1);").arg(table);
        if (sqlite3_prepare_v2(raw, sql.toUtf8().constData(), -1, &s, nullptr) == SQLITE_OK) {
            while (sqlite3_step(s) == SQLITE_ROW) {
                // column 1 of table_info = column name
                const unsigned char* c = sqlite3_column_text(s, 1);
                if (c && QString::fromUtf8(reinterpret_cast<const char*>(c)) == column) {
                    has = true;
                    break;
                }
            }
            sqlite3_finalize(s);
        }
        return has;
    }

private slots:

    // ---- fresh install -----------------------------------------------------

    void freshInstallGetsCompleteV4Schema() {
        Database db;
        QString err;
        QVERIFY(db.open(":memory:", &err));
        QVERIFY(Schema::initialize(db));

        QCOMPARE(Schema::currentVersion(db), 4);
        // EmbeddingChunks used to be created ONLY by the v2→v3 migration,
        // which never ran (version was clobbered first) — every chunk
        // query failed to prepare and chunked embeddings were silently
        // dead on every install.
        QVERIFY(tableExists(db, "BgeEmbeddings"));
        QVERIFY(tableExists(db, "EmbeddingChunks"));
        QVERIFY(tableHasColumn(db, "BgeEmbeddings", "algo_version"));
        QVERIFY(tableHasColumn(db, "EmbeddingChunks", "algo_version"));
        // 0.45 = the engine's m_threshold default; the noise gate for
        // AI-only additions lives in the engine (0.60), not here.
        QCOMPARE(settingValue(db, "similarity_threshold"), QString("0.45"));
    }

    // ---- threshold is never schema-managed ----------------------------------

    // Contract since v1.7.14: schema code never touches
    // similarity_threshold (the AI-noise gate lives in the engine).
    // Regression guard: an old build force-reset thresholds on EVERY
    // startup, silently undoing the user's tuning — and no migration
    // may ever "repair" the stored value either. Legacy default
    // values and user-tuned values must survive the v3→v4 migration
    // AND repeated launches exactly as stored.
    void thresholdIsNeverTouchedBySchemaCode() {
        Database db;
        QString err;
        QVERIFY(db.open(":memory:", &err));
        QVERIFY(Schema::initialize(db));

        // Legacy v3 DB on the old 0.65 default + a user-tuned 0.60
        // value on a second key is impossible (single key), so test
        // both cases in sequence.
        db.exec("PRAGMA user_version = 3;");
        db.exec("UPDATE SemanticSettings SET value='0.65' WHERE key='similarity_threshold';");

        QVERIFY(Schema::initialize(db));
        QCOMPARE(Schema::currentVersion(db), 4);
        // Legacy default survives the v3→v4 migration untouched.
        QCOMPARE(settingValue(db, "similarity_threshold"), QString("0.65"));

        // User tunes it afterwards; repeated launches keep it.
        db.exec("UPDATE SemanticSettings SET value='0.60' WHERE key='similarity_threshold';");
        QVERIFY(Schema::initialize(db));
        QCOMPARE(settingValue(db, "similarity_threshold"), QString("0.60"));
        // And again — idempotence.
        QVERIFY(Schema::initialize(db));
        QCOMPARE(settingValue(db, "similarity_threshold"), QString("0.60"));
    }

    // ---- legacy version must survive createSchemaV1 -------------------------

    // Regression: an old build stamped user_version = latest at the END
    // of createSchemaV1() — BEFORE migrate() ran — so migrate() was a
    // permanent no-op on every existing database. We simulate a real
    // v3 database: BgeEmbeddings WITHOUT the algo_version column (the
    // pre-v4 shape). If the version survived createSchemaV1, the
    // v3→v4 migration adds the column on re-initialize; if it was
    // clobbered, the migration never runs and the column is missing.
    void legacyVersionSurvivesSchemaCreation() {
        Database db;
        QString err;
        QVERIFY(db.open(":memory:", &err));
        QVERIFY(db.exec(
            "CREATE TABLE BgeEmbeddings ("
            "  file_id     INTEGER PRIMARY KEY,"
            "  embedding   BLOB    NOT NULL,"
            "  created_at  INTEGER NOT NULL DEFAULT 0,"
            "  updated_at  INTEGER NOT NULL DEFAULT 0,"
            "  status      TEXT    NOT NULL DEFAULT 'completed');"
            "CREATE TABLE Files (id INTEGER PRIMARY KEY, path TEXT);"));
        db.exec("PRAGMA user_version = 3;");

        QVERIFY(Schema::initialize(db));

        QCOMPARE(Schema::currentVersion(db), 4);
        // The migration ran (version was still 3) → it added the
        // column to the pre-existing legacy table.
        QVERIFY(tableHasColumn(db, "BgeEmbeddings", "algo_version"));
        // Freshly created by createSchemaV1 (was missing on the v3 DB).
        QVERIFY(tableExists(db, "EmbeddingChunks"));
        QVERIFY(tableHasColumn(db, "EmbeddingChunks", "algo_version"));
    }

    // ---- algo_version stamping ----------------------------------------------

    void freshEmbeddingReadsAsPresent() {
        QTemporaryDir dir;
        const QString dbPath = dir.filePath("emb.db");
        {
            sqlite3* raw = nullptr;
            QCOMPARE(sqlite3_open(dbPath.toUtf8().constData(), &raw), SQLITE_OK);
            QCOMPARE(sqlite3_exec(raw,
                "CREATE TABLE BgeEmbeddings ("
                "  file_id INTEGER PRIMARY KEY,"
                "  embedding BLOB NOT NULL,"
                "  created_at INTEGER NOT NULL DEFAULT 0,"
                "  updated_at INTEGER NOT NULL DEFAULT 0,"
                "  status TEXT NOT NULL DEFAULT 'completed',"
                "  algo_version INTEGER NOT NULL DEFAULT 0);",
                nullptr, nullptr, nullptr), SQLITE_OK);
            sqlite3_close(raw);
        }

        BgeEmbeddingDb emb(dbPath);
        QVERIFY(emb.open());
        const std::vector<float> v(384, 0.01f);  // BGE-small-en-v1.5 dim
        QVERIFY(emb.storeEmbedding(7, v));
        QVERIFY(emb.hasEmbedding(7));
    }

    // Regression: vectors written by the broken hash-fallback tokenizer
    // builds (or any older algorithm) must read as MISSING so the
    // background backfill re-embeds them — instead of scoring
    // meaningless similarities against real queries forever.
    void staleAlgoVersionEmbeddingReadsAsMissing() {
        QTemporaryDir dir;
        const QString dbPath = dir.filePath("emb.db");
        {
            sqlite3* raw = nullptr;
            QCOMPARE(sqlite3_open(dbPath.toUtf8().constData(), &raw), SQLITE_OK);
            QCOMPARE(sqlite3_exec(raw,
                "CREATE TABLE BgeEmbeddings ("
                "  file_id INTEGER PRIMARY KEY,"
                "  embedding BLOB NOT NULL,"
                "  created_at INTEGER NOT NULL DEFAULT 0,"
                "  updated_at INTEGER NOT NULL DEFAULT 0,"
                "  status TEXT NOT NULL DEFAULT 'completed',"
                "  algo_version INTEGER NOT NULL DEFAULT 0);",
                nullptr, nullptr, nullptr), SQLITE_OK);
            sqlite3_close(raw);
        }

        BgeEmbeddingDb emb(dbPath);
        QVERIFY(emb.open());
        const std::vector<float> v(384, 0.01f);  // BGE-small-en-v1.5 dim
        QVERIFY(emb.storeEmbedding(7, v));
        QVERIFY(emb.hasEmbedding(7));

        // Simulate a row written by an OLDER algorithm.
        sqlite3* raw = nullptr;
        QCOMPARE(sqlite3_open(dbPath.toUtf8().constData(), &raw), SQLITE_OK);
        QCOMPARE(sqlite3_exec(raw,
            "UPDATE BgeEmbeddings SET algo_version = 0 WHERE file_id = 7;",
            nullptr, nullptr, nullptr), SQLITE_OK);
        sqlite3_close(raw);

        // Stale → missing (the backfill will re-embed it).
        QVERIFY(!emb.hasEmbedding(7));

        // Re-storing heals the row.
        QVERIFY(emb.storeEmbedding(7, v));
        QVERIFY(emb.hasEmbedding(7));
    }
};

QTEST_GUILESS_MAIN(TestSchemaMigration)
#include "tst_SchemaMigration.moc"
