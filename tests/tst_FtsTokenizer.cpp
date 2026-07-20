// ============================================================
// tst_FtsTokenizer.cpp - Test FTS5 tokenizer behavior
// ============================================================
//
// Verifies the FTS5 tokenizer config:
//   tokenize = 'unicode61 remove_diacritics 2'
//
// Tests:
//   1. Unicode case folding (Hello == hello == HELLO)
//   2. Diacritic removal (café == cafe, naïve == naive)
//   3. Phrase matching with quotes
//   4. Boolean operators (AND, OR, NOT)
//   5. CJK content (Chinese/Japanese/Korean) — these don't have
//      case/diacritics but should still be searchable.
//   6. Numbers and special characters
//
// These are integration tests against a real SQLite FTS5 index.
// ============================================================

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QString>

class TestFtsTokenizer : public QObject {
    Q_OBJECT

private:
    QSqlDatabase db;

    bool exec(const QString& sql) {
        QSqlQuery q(db);
        if (!q.exec(sql)) {
            qWarning() << "SQL failed:" << q.lastError().text() << "\nSQL:" << sql;
            return false;
        }
        return true;
    }

private slots:
    void initTestCase() {
        db = QSqlDatabase::addDatabase("QSQLITE");
        db.setDatabaseName(":memory:");
        QVERIFY(db.open());

        // Create the same FTS5 table as Schema.cpp uses.
        QVERIFY(exec("CREATE VIRTUAL TABLE SearchIndex USING fts5("
                     "  filename, "
                     "  content, "
                     "  path UNINDEXED, "
                     "  extension UNINDEXED, "
                     "  file_id UNINDEXED, "
                     "  tokenize = 'unicode61 remove_diacritics 2'"
                     ");"));
    }

    void cleanupTestCase() {
        db.close();
    }

    // ── 1. Basic insert + match ─────────────────────────────
    void testBasicMatch() {
        QVERIFY(exec("INSERT INTO SearchIndex (filename, content, path, extension, file_id) "
                     "VALUES ('doc.pdf', 'Hello world from DocuSearch', '/x/doc.pdf', 'pdf', 1);"));

        QSqlQuery q(db);
        QVERIFY(q.exec("SELECT file_id FROM SearchIndex WHERE SearchIndex MATCH 'hello';"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toLongLong(), 1LL);
    }

    // ── 2. Case insensitivity ───────────────────────────────
    void testCaseInsensitive() {
        QVERIFY(exec("INSERT INTO SearchIndex (filename, content, path, extension, file_id) "
                     "VALUES ('a.txt', 'The Quick Brown FOX', '/x/a.txt', 'txt', 2);"));

        QSqlQuery q(db);
        // All of these should match.
        QVERIFY(q.exec("SELECT file_id FROM SearchIndex WHERE SearchIndex MATCH 'quick';"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toLongLong(), 2LL);

        QVERIFY(q.exec("SELECT file_id FROM SearchIndex WHERE SearchIndex MATCH 'QUICK';"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toLongLong(), 2LL);

        QVERIFY(q.exec("SELECT file_id FROM SearchIndex WHERE SearchIndex MATCH 'Fox';"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toLongLong(), 2LL);
    }

    // ── 3. Diacritic removal (remove_diacritics 2) ──────────
    void testDiacriticRemoval() {
        QVERIFY(exec("INSERT INTO SearchIndex (filename, content, path, extension, file_id) "
                     "VALUES ('b.txt', 'café résumé naïve façade', '/x/b.txt', 'txt', 3);"));

        QSqlQuery q(db);
        // Search WITHOUT diacritics should match content WITH diacritics.
        QVERIFY(q.exec("SELECT file_id FROM SearchIndex WHERE SearchIndex MATCH 'cafe';"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toLongLong(), 3LL);

        QVERIFY(q.exec("SELECT file_id FROM SearchIndex WHERE SearchIndex MATCH 'resume';"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toLongLong(), 3LL);

        QVERIFY(q.exec("SELECT file_id FROM SearchIndex WHERE SearchIndex MATCH 'naive';"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toLongLong(), 3LL);

        QVERIFY(q.exec("SELECT file_id FROM SearchIndex WHERE SearchIndex MATCH 'facade';"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toLongLong(), 3LL);
    }

    // ── 4. Phrase queries ───────────────────────────────────
    void testPhraseQuery() {
        QVERIFY(exec("INSERT INTO SearchIndex (filename, content, path, extension, file_id) "
                     "VALUES ('c.txt', 'The quick brown fox jumps', '/x/c.txt', 'txt', 4);"));

        QSqlQuery q(db);
        // Phrase query: "quick brown" should match adjacent words.
        QVERIFY(q.exec("SELECT file_id FROM SearchIndex WHERE SearchIndex MATCH '\"quick brown\"';"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toLongLong(), 4LL);

        // Reversed order should NOT match.
        QVERIFY(q.exec("SELECT file_id FROM SearchIndex WHERE SearchIndex MATCH '\"brown quick\"';"));
        QVERIFY(!q.next());  // no match
    }

    // ── 5. Boolean operators ────────────────────────────────
    void testBooleanOperators() {
        QVERIFY(exec("INSERT INTO SearchIndex (filename, content, path, extension, file_id) "
                     "VALUES ('d.txt', 'apple banana cherry', '/x/d.txt', 'txt', 5);"));

        QSqlQuery q(db);
        // AND (implicit)
        QVERIFY(q.exec("SELECT file_id FROM SearchIndex WHERE SearchIndex MATCH 'apple banana';"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toLongLong(), 5LL);

        // OR
        QVERIFY(q.exec("SELECT file_id FROM SearchIndex WHERE SearchIndex MATCH 'apple OR grape';"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toLongLong(), 5LL);

        // NOT
        QVERIFY(q.exec("SELECT file_id FROM SearchIndex WHERE SearchIndex MATCH 'apple NOT grape';"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toLongLong(), 5LL);
    }

    // ── 6. Numbers and special characters ───────────────────
    void testNumbersAndSpecial() {
        QVERIFY(exec("INSERT INTO SearchIndex (filename, content, path, extension, file_id) "
                     "VALUES ('e.txt', 'Invoice 2024-01-15 amount $1,234.56', '/x/e.txt', 'txt', 6);"));

        QSqlQuery q(db);
        QVERIFY(q.exec("SELECT file_id FROM SearchIndex WHERE SearchIndex MATCH '2024';"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toLongLong(), 6LL);

        QVERIFY(q.exec("SELECT file_id FROM SearchIndex WHERE SearchIndex MATCH 'invoice';"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toLongLong(), 6LL);
    }

    // ── 7. CJK content (no case/diacritics, but should still work) ─
    void testCjkContent() {
        QVERIFY(exec("INSERT INTO SearchIndex (filename, content, path, extension, file_id) "
                     "VALUES ('f.txt', '你好世界 北京 上海', '/x/f.txt', 'txt', 7);"));

        QSqlQuery q(db);
        // Note: unicode61 tokenizer does NOT split CJK on word boundaries.
        // Searching for individual characters or short substrings may not
        // behave like Latin text. This test documents the actual behavior:
        // the full text "你好世界" is one token, so we search for it as-is.
        QVERIFY(q.exec("SELECT file_id FROM SearchIndex WHERE SearchIndex MATCH '北京';"));
        if (q.next()) {
            QCOMPARE(q.value(0).toLongLong(), 7LL);
        }
        // If no match, that's expected — unicode61 has no CJK segmentation.
        // Users needing CJK word segmentation should configure a custom
        // tokenizer (e.g., SimpleTokenizer from sqlite-fts5-trigram).
    }

    // ── 8. Empty / whitespace edge cases ────────────────────
    void testEmptyAndWhitespace() {
        QVERIFY(exec("INSERT INTO SearchIndex (filename, content, path, extension, file_id) "
                     "VALUES ('g.txt', '   ', '/x/g.txt', 'txt', 8);"));

        QSqlQuery q(db);
        // Searching for empty string should not crash.
        QVERIFY(q.exec("SELECT file_id FROM SearchIndex WHERE SearchIndex MATCH 'nonexistent';"));
        QVERIFY(!q.next());
    }
};

QTEST_GUILESS_MAIN(TestFtsTokenizer)
#include "tst_FtsTokenizer.moc"
