// ============================================================
// tst_StringUtils.cpp — Unit tests for core/StringUtils
// ============================================================
//
// Covers: normalize, toLowerAscii, fts5Quote, snippetAround,
//         stripControlChars, formatFileSize, slugify,
//         levenshtein, jaccardSimilarity, splitFieldTerms.
//
// Uses the Qt Test framework.
// ============================================================

#include "../src/core/StringUtils.h"

#include <QtTest/QtTest>
#include <QString>

using DocuSearch::Utils::normalize;
using DocuSearch::Utils::toLowerAscii;
using DocuSearch::Utils::fts5Quote;
using DocuSearch::Utils::snippetAround;
using DocuSearch::Utils::stripControlChars;
using DocuSearch::Utils::formatFileSize;
using DocuSearch::Utils::slugify;
using DocuSearch::Utils::levenshtein;
using DocuSearch::Utils::jaccardSimilarity;
using DocuSearch::Utils::splitFieldTerms;

class TestStringUtils : public QObject {
    Q_OBJECT
private slots:

    // ---- normalize --------------------------------------------------------

    void normalize_lowercases() {
        QCOMPARE(normalize("Hello WORLD"), QString("hello world"));
    }
    void normalize_stripsAccents() {
        // NFD decomposition + combining-mark removal
        QCOMPARE(normalize(QString::fromUtf8("café résumé")), QString("cafe resume"));
    }
    void normalize_collapsesWhitespace() {
        QCOMPARE(normalize("  foo   bar  \n\t baz "), QString("foo bar baz"));
    }
    void normalize_empty() {
        QCOMPARE(normalize(""), QString(""));
    }

    // ---- toLowerAscii -----------------------------------------------------

    void toLowerAscii_asciiOnly() {
        QCOMPARE(toLowerAscii("ABC123xyz"), QString("abc123xyz"));
    }
    void toLowerAscii_preservesCJK() {
        // CJK characters must NOT be corrupted by ASCII lowercasing
        const QString in  = QString::fromUtf8("文件NAME.PDF");
        const QString out = toLowerAscii(in);
        QVERIFY(out.contains(QString::fromUtf8("文件")));
        QVERIFY(out.contains("name.pdf"));
    }

    // ---- fts5Quote --------------------------------------------------------

    void fts5Quote_plain() {
        QCOMPARE(fts5Quote("hello"), QString("\"hello\""));
    }
    void fts5Quote_doublesInternalQuotes() {
        // Per FTS5 spec: embedded " becomes ""
        QCOMPARE(fts5Quote("a\"b"), QString("\"a\"\"b\""));
    }
    void fts5Quote_empty() {
        QCOMPARE(fts5Quote(""), QString("\"\""));
    }

    // ---- snippetAround ----------------------------------------------------

    void snippetAround_findsMatch() {
        const QString text = "The quick brown fox jumps over the lazy dog.";
        const QString snip = snippetAround(text, "brown", 5, 5);
        QVERIFY(snip.contains("brown"));
        QVERIFY(snip.startsWith("…"));   // 5 chars before
    }
    void snippetAround_noMatch_returnsHead() {
        const QString text = "Hello world.";
        const QString snip = snippetAround(text, "missing", 5, 5);
        QVERIFY(!snip.isEmpty());
    }
    void snippetAround_emptyText() {
        QVERIFY(snippetAround("", "x").isEmpty());
    }

    // ---- stripControlChars ------------------------------------------------

    void stripControlChars_keepsPrintable() {
        QCOMPARE(stripControlChars("Hello, world!"), QString("Hello, world!"));
    }
    void stripControlChars_keepsNewlines() {
        QCOMPARE(stripControlChars("line1\nline2\ttab"), QString("line1\nline2\ttab"));
    }
    void stripControlChars_dropsControlCodes() {
        // 0x01 (SOH), 0x02 (STX) must be stripped; \n must be kept
        QString in;
        in.append(QChar(0x01));
        in.append('A');
        in.append(QChar(0x02));
        in.append('\n');
        in.append('B');
        QCOMPARE(stripControlChars(in), QString("A\nB"));
    }

    // ---- formatFileSize ---------------------------------------------------

    void formatFileSize_bytes() {
        QCOMPARE(formatFileSize(0),    QString("0 B"));
        QCOMPARE(formatFileSize(512),  QString("512 B"));
        QCOMPARE(formatFileSize(1023), QString("1023 B"));
    }
    void formatFileSize_kb() {
        QCOMPARE(formatFileSize(1024),    QString("1.0 KB"));
        QCOMPARE(formatFileSize(1536),    QString("1.5 KB"));
        QCOMPARE(formatFileSize(1024*999), QString("999.0 KB"));
    }
    void formatFileSize_mb() {
        QCOMPARE(formatFileSize(1024LL * 1024),         QString("1.0 MB"));
        QCOMPARE(formatFileSize(1024LL * 1024 * 12),    QString("12.0 MB"));
    }
    void formatFileSize_gb() {
        QCOMPARE(formatFileSize(1024LL * 1024 * 1024),        QString("1.0 GB"));
        QCOMPARE(formatFileSize(1024LL * 1024 * 1024 * 5),    QString("5.0 GB"));
    }

    // ---- slugify ----------------------------------------------------------

    void slugify_basic() {
        QCOMPARE(slugify("Hello, World!"),  QString("hello_world"));
    }
    void slugify_stripsAccents() {
        QCOMPARE(slugify(QString::fromUtf8("Café Résumé")), QString("cafe_resume"));
    }
    void slugify_trimsUnderscores() {
        QCOMPARE(slugify("  --foo--  "), QString("foo"));
    }

    // ---- levenshtein ------------------------------------------------------

    void levenshtein_identical() {
        QCOMPARE(levenshtein("hello", "hello"), 0);
    }
    void levenshtein_empty() {
        QCOMPARE(levenshtein("", "abc"), 3);
        QCOMPARE(levenshtein("abc", ""), 3);
    }
    void levenshtein_caseInsensitive() {
        // The impl compares lowercased chars, so "ABC" vs "abc" = 0
        QCOMPARE(levenshtein("ABC", "abc"), 0);
    }
    void levenshtein_oneEdit() {
        QCOMPARE(levenshtein("cat", "cot"), 1);
        QCOMPARE(levenshtein("cat", "cats"), 1);
        QCOMPARE(levenshtein("cats", "cat"), 1);
    }

    // ---- jaccardSimilarity ------------------------------------------------

    void jaccard_identical() {
        QCOMPARE(jaccardSimilarity("foo bar", "foo bar"), 1.0);
    }
    void jaccard_disjoint() {
        QCOMPARE(jaccardSimilarity("foo", "bar"), 0.0);
    }
    void jaccard_partial() {
        // Tokens: {foo, bar, baz} vs {foo, bar, qux}
        // Intersection = 2, Union = 4 -> 0.5
        QCOMPARE(jaccardSimilarity("foo bar baz", "foo bar qux"), 0.5);
    }
    void jaccard_bothEmpty() {
        QCOMPARE(jaccardSimilarity("", ""), 1.0);
    }

    // ---- splitFieldTerms --------------------------------------------------

    void splitFieldTerms_basic() {
        const auto r = splitFieldTerms("type:pdf date:2026 NOC");
        QCOMPARE(r.fields.size(), 2);
        QCOMPARE(r.fields[0].field, QString("type"));
        QCOMPARE(r.fields[0].value,  QString("pdf"));
        QCOMPARE(r.fields[1].field, QString("date"));
        QCOMPARE(r.fields[1].value,  QString("2026"));
        QCOMPARE(r.freeTerms.size(), 1);
        QCOMPARE(r.freeTerms[0], QString("NOC"));
    }
    void splitFieldTerms_quotedValue() {
        const auto r = splitFieldTerms("folder:\"My Documents\"");
        QCOMPARE(r.fields.size(), 1);
        QCOMPARE(r.fields[0].field, QString("folder"));
        QCOMPARE(r.fields[0].value,  QString("My Documents"));
    }
    void splitFieldTerms_noFields() {
        const auto r = splitFieldTerms("just a phrase");
        QVERIFY(r.fields.isEmpty());
        QCOMPARE(r.freeTerms.join(' '), QString("just a phrase"));
    }
};

QTEST_GUILESS_MAIN(TestStringUtils)
#include "tst_StringUtils.moc"
