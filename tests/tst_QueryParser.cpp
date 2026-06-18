// ============================================================
// tst_QueryParser.cpp — Unit tests for search/QueryParser
// ============================================================
//
// Covers: phrase parsing, boolean operators, field filters
//         (type:, folder:, date:, favorite:, ocr:, tag:),
//         exclusion (-term), wildcard (prefix*).
//
// Uses the Qt Test framework.
// ============================================================

#include "../src/search/QueryParser.h"

#include <QtTest/QtTest>
#include <QString>

using DocuSearch::QueryParser;
using DocuSearch::ParsedQuery;

class TestQueryParser : public QObject {
    Q_OBJECT
private slots:

    // ---- basics -----------------------------------------------------------

    void emptyQuery() {
        const auto q = QueryParser::parse("");
        QVERIFY(q.valid);
        QVERIFY(q.ftsQuery.isEmpty());
    }
    void whitespaceOnly() {
        const auto q = QueryParser::parse("   ");
        QVERIFY(q.valid);
        QVERIFY(q.ftsQuery.isEmpty());
    }

    // ---- bare words -------------------------------------------------------

    void singleWord() {
        const auto q = QueryParser::parse("NOC");
        QVERIFY(q.valid);
        QVERIFY(q.ftsQuery.contains("\"NOC\""));
    }
    void multipleWords() {
        const auto q = QueryParser::parse("NOC examination");
        QVERIFY(q.ftsQuery.contains("\"NOC\""));
        QVERIFY(q.ftsQuery.contains("\"examination\""));
    }

    // ---- phrases ----------------------------------------------------------

    void quotedPhrase() {
        const auto q = QueryParser::parse("\"Station Development\"");
        // Should produce an FTS5 phrase token: "Station Development"
        QVERIFY(q.ftsQuery.contains("\"Station Development\""));
    }
    void phraseWithInternalQuotes() {
        // FTS5 spec: a literal " inside a phrase is represented by doubling it.
        // Input `"a""b"` (6 chars: " a " " b ") — the current parser treats
        // this as TWO separate phrases: "a" and "b". A more sophisticated
        // parser would merge them into the single phrase `a"b`. For now we
        // verify that both quoted tokens are emitted.
        const auto q = QueryParser::parse("\"a\"\"b\"");
        QVERIFY(q.ftsQuery.contains("\"a\""));
        QVERIFY(q.ftsQuery.contains("\"b\""));
    }

    // ---- boolean ----------------------------------------------------------

    void booleanAnd() {
        const auto q = QueryParser::parse("NOC AND examination");
        QVERIFY(q.ftsQuery.contains("AND"));
    }
    void booleanOr() {
        const auto q = QueryParser::parse("NOC OR examination");
        QVERIFY(q.ftsQuery.contains("OR"));
    }
    void booleanNot() {
        const auto q = QueryParser::parse("NOC NOT examination");
        QVERIFY(q.ftsQuery.contains("NOT"));
    }

    // ---- exclusion --------------------------------------------------------

    void exclusionPrefix() {
        const auto q = QueryParser::parse("NOC -draft");
        QVERIFY(q.ftsQuery.contains("NOT"));
        QVERIFY(q.ftsQuery.contains("\"draft\""));
    }

    // ---- wildcards --------------------------------------------------------

    void prefixWildcard() {
        const auto q = QueryParser::parse("rail*");
        // FTS5 supports prefix queries via the * suffix on a bare token
        QVERIFY(q.ftsQuery.contains("rail*"));
    }

    // ---- field filters ----------------------------------------------------

    void typeFilter() {
        const auto q = QueryParser::parse("type:pdf");
        QCOMPARE(q.typeFilter, QString("pdf"));
    }
    void typeFilter_caseInsensitive() {
        const auto q = QueryParser::parse("type:PDF");
        QCOMPARE(q.typeFilter, QString("pdf"));
    }
    void extAliasForType() {
        const auto q = QueryParser::parse("ext:docx");
        QCOMPARE(q.typeFilter, QString("docx"));
    }
    void folderFilter() {
        const auto q = QueryParser::parse("folder:Railway");
        QCOMPARE(q.folderFilter, QString("Railway"));
    }
    void folderFilter_quoted() {
        const auto q = QueryParser::parse("folder:\"My Documents\"");
        QCOMPARE(q.folderFilter, QString("My Documents"));
    }
    void pathAliasForFolder() {
        const auto q = QueryParser::parse("path:project");
        QCOMPARE(q.folderFilter, QString("project"));
    }
    void dateFilter_year() {
        const auto q = QueryParser::parse("date:2026");
        QCOMPARE(q.dateFilter, QString("2026"));
    }
    void yearAliasForDate() {
        const auto q = QueryParser::parse("year:2024");
        QCOMPARE(q.dateFilter, QString("2024"));
    }
    void dateFilter_fullDate() {
        const auto q = QueryParser::parse("date:2026-06-15");
        QCOMPARE(q.dateFilter, QString("2026-06-15"));
    }

    // ---- favorites / ocr --------------------------------------------------

    void favoritesFlag() {
        const auto q = QueryParser::parse("favorite:1");
        QVERIFY(q.favoritesOnly);
    }
    void favoritesFlag_alias() {
        const auto q = QueryParser::parse("fav:1");
        QVERIFY(q.favoritesOnly);
    }
    void favoritesFlag_bare() {
        // "favorite:" with no value should also enable
        const auto q = QueryParser::parse("favorite:");
        QVERIFY(q.favoritesOnly);
    }
    void ocrFlag() {
        const auto q = QueryParser::parse("ocr:1");
        QVERIFY(q.ocrOnly);
    }

    // ---- tag filter (the bit we just implemented) -------------------------

    void tagFilter_basic() {
        const auto q = QueryParser::parse("tag:Urgent");
        QCOMPARE(q.tagFilter, QString("Urgent"));
    }
    void tagFilter_quoted() {
        const auto q = QueryParser::parse("tag:\"For Review\"");
        QCOMPARE(q.tagFilter, QString("For Review"));
    }
    void tagFilter_empty() {
        // tag: with empty value should NOT set the filter
        const auto q = QueryParser::parse("tag:");
        QVERIFY(q.tagFilter.isEmpty());
    }

    // ---- combined ---------------------------------------------------------

    void combinedFiltersAndText() {
        const auto q = QueryParser::parse("type:pdf folder:Railway NOC examination");
        QCOMPARE(q.typeFilter,   QString("pdf"));
        QCOMPARE(q.folderFilter, QString("Railway"));
        QVERIFY(q.ftsQuery.contains("\"NOC\""));
        QVERIFY(q.ftsQuery.contains("\"examination\""));
    }
    void everythingQuery() {
        // The headline example from the spec
        const auto q = QueryParser::parse("\"Executive Lounge\" type:pdf date:2026");
        QVERIFY(q.ftsQuery.contains("\"Executive Lounge\""));
        QCOMPARE(q.typeFilter, QString("pdf"));
        QCOMPARE(q.dateFilter, QString("2026"));
    }
};

QTEST_GUILESS_MAIN(TestQueryParser)
#include "tst_QueryParser.moc"
