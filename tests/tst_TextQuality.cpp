// ============================================================
// tst_TextQuality.cpp — Unit tests for core/TextQuality
// ============================================================
//
// v1.7.2: PDFs with broken /ToUnicode CMaps decode to gibberish
// ("meaningless alphabet") that must be discarded and routed to
// OCR. These tests pin the detector's behaviour on both sides:
//
//   MUST be flagged garbage   - converter alphabet soup
//                               - Private Use Area soup
//   MUST NOT be flagged       - English / French / German prose
//                               - Devanagari text (non-Latin scope)
//                               - all-caps legal text, invoices,
//                                 code listings, short snippets
//
// Samples mirror the synthesized repro corpus built while
// investigating the v1.7.2 bug report.
//
// Uses the Qt Test framework.
// ============================================================

#include "../src/core/TextQuality.h"

#include <QtTest/QtTest>
#include <QString>

using DocuSearch::TextQuality::looksLikeGarbage;

class TestTextQuality : public QObject {
    Q_OBJECT
private slots:

    // ---- garbage: converter alphabet soup (bogus ToUnicode) ----

    void garbage_alphabetSoup_flagged() {
        // "The quick brown fox..." decoded through a junk ToUnicode map.
        const QString s = "xwj qvxqz xzwxv kwq qvkxj wwjz kwj jwjz zwv. "
                          "wjwzqw jvvxvjj xvzjq zwqvkjvkj jw kwwk vjjzj qwv "
                          "kxvz kwjk wqzwjj jwzvj qwjjjqkxwvj wk kxjjj "
                          "qv xqzjz xzwxv kwq qvkxj wwjz kwj jwjz zwv "
                          "wjwzqw jvvxvjj xvzjq zwqvkjvkj";
        QString why;
        QVERIFY(looksLikeGarbage(s, &why));
        QVERIFY(why.contains("word-sanity"));
    }

    void garbage_puaSoup_flagged() {
        // Letters mapped into the Private Use Area (vector/chart tools).
        QString s;
        for (int i = 0; i < 120; ++i) {
            s.append(QChar(static_cast<ushort>(0xE000 + (i * 7) % 100)));
            if (i % 5 == 4) s.append(QLatin1Char(' '));
        }
        QString why;
        QVERIFY(looksLikeGarbage(s, &why));
        QVERIFY(why.contains("pua"));
    }

    void garbage_longLegacyFontSample_flagged() {
        // Legacy-font layout text (e.g. Kruti-Dev-style Devanagari
        // typeset in an ASCII legacy font): plenty of tokens, zero
        // common words, mostly vowel-less runs.
        const QString s = "dqN O;atu vkSj fQj Hkh lqcg dk;e gksrs gSaA "
                          "blesa x, HykV dk iky/k Hkh gksrk gSA lqcg dk "
                          "vkSj Hkh fQj Hkh x, HykV iky/k gksrs gSaA "
                          "dqN vkSj lqcg fQj dk;e gksrs gSaA blesa HykV "
                          "vkSj iky/k Hkh gksrs gSaA";
        QVERIFY(looksLikeGarbage(s));
    }

    // ---- real text: must NEVER be flagged ----

    void clean_englishProse_passes() {
        const QString s = "The quick brown fox jumps over the lazy dog. "
                          "Search engines index documents so that users can "
                          "find them across large collections of files "
                          "quickly and reliably. Every document is parsed, "
                          "tokenized, and stored in a full-text index.";
        QVERIFY(!looksLikeGarbage(s));
    }

    void clean_englishAllCaps_passes() {
        const QString s = "THE PARTY OF THE FIRST PART HEREBY AGREES THAT "
                          "ALL OBLIGATIONS UNDER THIS AGREEMENT SHALL REMAIN "
                          "IN FULL FORCE AND EFFECT UNTIL THE PARTIES "
                          "EXECUTE A WRITTEN AMENDMENT SIGNED BY BOTH "
                          "PARTIES AND DULY NOTARIZED BEFORE THE DEADLINE";
        QVERIFY(!looksLikeGarbage(s));
    }

    void clean_frenchProse_passes() {
        const QString s = "Le renard brun rapide saute par-dessus le chien "
                          "paresseux. Les moteurs de recherche indexent les "
                          "documents pour que les utilisateurs puissent les "
                          "retrouver facilement dans de grandes collections "
                          "de fichiers avec des mots très précis";
        QVERIFY(!looksLikeGarbage(s));
    }

    void clean_germanProse_passes() {
        const QString s = "Der schnelle braune Fuchs springt über den "
                          "faulen Hund. Suchmaschinen indexieren Dokumente, "
                          "damit Benutzer sie in großen Sammlungen schnell "
                          "und zuverlässig finden können, weil alle Wörter "
                          "in einem Index gespeichert werden und die Suche "
                          "damit sehr schnell ist";
        QVERIFY(!looksLikeGarbage(s));
    }

    void clean_devanagari_passes() {
        // Non-Latin scripts are out of the word gate's scope entirely.
        const QString s = "तेज़ भूरी लomी आलसी कुत्ते पर कूद गई। खोज इंजन "
                          "दस्तावेज़ों को अनुक्रमित करते हैं ताकि उपयोगकर्ता "
                          "उन्हें ढूंढ सकें। यह अनुच्छेद पाठ निष्कर्षण "
                          "गुणवत्ता के परीक्षण के लिए मौजूद है।";
        QVERIFY(!looksLikeGarbage(s));
    }

    void clean_codeListing_passes() {
        const QString s = "void Application::onSearchClicked() { const "
                          "QString query = searchInput->text().trimmed(); if "
                          "(query.isEmpty()) return; emit searchRequested("
                          "query); } // results handled in onResultsReady "
                          "which updates the model and view with matches";
        QVERIFY(!looksLikeGarbage(s));
    }

    void clean_invoiceNumbers_passes() {
        const QString s = "INVOICE #45123  DATE: 12/03/2024  DUE: 11/04/2024 "
                          "BILL TO: Acme Corp, 42 Industrial Estate, Phase 2 "
                          "QTY 12 x Steel Bracket M8 = 1,240.00  TAX 18% = "
                          "223.20  TOTAL INR 1,463.20  GSTIN 27AAAPZ1234C1ZV "
                          "PAN AAAPZ1234C  TERMS: Net 30 days from invoice "
                          "date  BANK HDFC0000123 IFSC CODE REF 440092";
        QVERIFY(!looksLikeGarbage(s));
    }

    // ---- conservative scope guards ----

    void scope_shortSnippet_neverFlagged() {
        // Too little signal to judge - even obvious junk is kept.
        QVERIFY(!looksLikeGarbage("xwj qvxqz kwq"));
        QVERIFY(!looksLikeGarbage(QString()));
        QVERIFY(!looksLikeGarbage("12345 67890 2468 1357 9012 3456"));
    }

    void scope_smallPuaShare_notFlagged() {
        // A stray PUA glyph (<2%) amid real text is not evidence.
        const QString s = "The quick brown fox jumps over the lazy dog. "
                          "Search engines index documents so that users can "
                          "find them across large collections of files "
                          "quickly." + QString(QChar(static_cast<ushort>(0xE0B3))) +
                          " and reliably every single time";
        QVERIFY(!looksLikeGarbage(s));
    }

    void reason_outParam_optional() {
        QString why;
        looksLikeGarbage("The quick brown fox jumps over the lazy dog and "
                         "keeps running through the meadow beyond the old "
                         "fence.", &why);
        QVERIFY(why.isEmpty());
    }
};

#include "tst_TextQuality.moc"
QTEST_GUILESS_MAIN(TestTextQuality)
