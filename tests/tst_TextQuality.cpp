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

    // ---- v1.7.16: garbage: scanner-embedded fragment soup (gate C) ----

    void garbage_scannerFragmentSoup_flagged() {
        // Real-world pattern (reference: "Minhaj Pay 04 10 24.pdf",
        // Canon IJ Scan Utility): a ROTATED page OCR'd by the scanner
        // itself, embedded into the PDF as a text layer. Punctuation
        // soup + 1-2 letter fragments; gate B never even looked at it
        // because only 53% of the non-space characters are letters.
        const QString s = R"junk(AVAA'IIVH NHAISAAA HINOS ;;
i
g
!c
H
I-
aL
-)-
J-c
P-\
-^,!t
!
c 'J ;i:i
L^,/.
'u >
4)
' tr Arrj-o
*
d)t/
.-
>>.c)lJ
?^ 7 -a'.f, .,
il:!!'LY
\-V.n.'-
-! 'r)2-'
c. J Bcn^v
\
L
F
rA
<
--.lo^-J+
+-ra,
)'-:mL
\ir.-n.
-^^J
,.f. ^i a; < i
-2'V
Z,
^--aAry'-u.
'--l -:-
= 9
\- \- >.
r =rtn-
C
a.
_J)
ijr
^'
.^-
a r
,J-
V
A
. r .t
OJ
aL(,
U
k
tJ.
S.
rt
tb-
l-b ')T
l.F"
r!
3o
z
O
-)d
CJ
rlj
HV
trx
a*
L19
.e^
trL
a
}L'
ar '-tr
dI-r
i!
oJ
L:a
x
e-
_d
t-)
U
C
tf;
Uz
bo \-
.;Z
zii
L\
ul-
.Y
L--
A*
:o
:t^
-o-
>,.c
-5
>itn
>,x
ao
2.2
d:
P
FC
>,!
\-:
LC
(6 ni
:dD-=
LL
Ul
L
ilizc
a.
l
^.1
a't
l!
-i-
ol
.c
o.l
=f
c.laNNq
fiC
'.u
i/
: rc.!
:? i^C
G.-L.,
_li
v,
Ct rt --
'ir->
)\fr
.!i uf l_'
*J-i(,
a,
--)
4=',9)junk";
        QString why;
        QVERIFY(looksLikeGarbage(s, &why));
        QVERIFY(why.contains("fragment-soup"));
    }

    // ---- v1.7.16: real text that gate C must NEVER flag ----

    void clean_bankStatementTable_passes() {
        // Numeric-heavy bank statement: only ~45% letters (below gate
        // B's letter-dominance line - the exact zone gate C owns), but
        // punctuation stays ~11%, tokens are long real words.
        const QString s = "ACCOUNT STATEMENT - PERIOD 01/04/2024 TO "
                          "30/06/2024 DATE DESCRIPTION DEBIT CREDIT "
                          "BALANCE 01/04/2024 OPENING BALANCE 0.00 "
                          "125,430.00 03/04/2024 UPI/SALARY APR 78,250.00 "
                          "203,680.00 07/04/2024 CHQ 004351 CLEARED "
                          "25,000.00 178,680.00 15/04/2024 NEFT VENDOR "
                          "PYMT 46,300.00 132,380.00 TOTAL DEBITS "
                          "71,300.00 TOTAL CREDITS 78,250.00 CLOSING "
                          "BALANCE 132,380.00";
        QVERIFY(!looksLikeGarbage(s));
    }

    void clean_mediocreScannerOcr_passes() {
        // A scanner text layer that OCR'd READABLY (the common case):
        // imperfect words but real structure. Junk-classifying this
        // would discard a usable layer and waste OCR passes.
        const QString s = "No.SWR/P.676/I/Engg./D&D (57) Date: 19.09.2024. "
                          "MEMORANDUM Sub: Pay Fixation of Trainee IE/D & D "
                          "on Absorption to the regular working post as JE/D "
                          "& D in Civil Engg. Dept of SWR. In conformity with "
                          "Sr.DPO/UBL Office Order No.27/ENGG/SUP/2024 the "
                          "following Trainee Junior Engineer is now absorbed "
                          "to the regular working post in Level-6 of 7th PC "
                          "Pay Matrix, his pay is fixed duly taking training "
                          "period into account for granting of increment on "
                          "absorption as under.";
        QVERIFY(!looksLikeGarbage(s));
    }

    void scope_shortFragmentSoup_neverFlagged() {
        // Below gate C's 40-token signal bound - even obvious junk
        // fragments are kept (false positives are expensive).
        QVERIFY(!looksLikeGarbage(";; i g !c H I- at -)- J-c P- "
                                  "-^,!t ! c 'J ;i:i L^,/. 'u > 4) "
                                  "' tr Arrj-o * - d)t/ .-"));
    }

    // ---- v1.7.16: assessOcrText (OCR auto-orientation metrics) ----

    void assess_englishText_readsRealWords() {
        const auto q = DocuSearch::TextQuality::assessOcrText(
            "MEMORANDUM Sub: Pay Fixation of Trainee Engineer on "
            "Absorption to the regular working post in the Civil "
            "Department with pay fixed as under and increments "
            "granted from the date of absorption.");
        QVERIFY(q.latinDominant);
        QVERIFY(q.tokens >= 12);
        QVERIFY(q.wordRate > 0.10);
        QVERIFY(q.runScore > 100);
    }

    void assess_fragmentJunk_nearZeroWordRate() {
        const auto q = DocuSearch::TextQuality::assessOcrText(
            "AVAA'IIVH NHAISAAA HINOS ;; i g !c H I- aL -)- J-c P- "
            "-^,!t ! c 'J ;i:i L^,/. 'u > 4) ' tr Arrj-o - d)t/ .- "
            ">>.c)lJ ?^ 7 -a'.f, ., il:!!'LY \\-V.n.'- -! 'r)2-' "
            "c. J Bcn^v \\ L F rA < --.lo^-J+ +-ra, )'-:mL \\ir.-n.");
        QVERIFY(q.latinDominant);          // fragments ARE Latin letters
        QVERIFY(q.wordRate < 0.02);        // ...but they are not words
    }

    void assess_devanagari_notLatinDominant() {
        const auto q = DocuSearch::TextQuality::assessOcrText(
            "दक्षिण पश्चिम रेलवे मुख्यालय कार्यालय कार्मिक विभाग "
            "रेल सौधा गदाग रोड हुब्बल्ली दिनांक विषय संदर्भ");
        QVERIFY(!q.latinDominant);         // word rate is meaningless here
        QCOMPARE(q.dictHits, 0);
    }

    void assess_runScore_exactValues() {
        // Runs of >=3 alnum chars: "abc"(3) + "fghi"(4) count, "de" does not.
        QCOMPARE(DocuSearch::TextQuality::assessOcrText(
                     QStringLiteral("abc de fghi")).runScore, 7);
        QCOMPARE(DocuSearch::TextQuality::assessOcrText(
                     QStringLiteral("ab de fg")).runScore, 0);
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
