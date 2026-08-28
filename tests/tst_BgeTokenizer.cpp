// ============================================================
// tst_BgeTokenizer.cpp — Unit tests for embeddings/BgeTokenizer
// ============================================================
//
// Covers: vocab loading, exact-length (unpadded) encoding, CLS/SEP
//         framing, per-character punctuation splitting, accent
//         folding, WordPiece subwords, UNK handling, and the
//         512-token cap with trailing [SEP].
//
// Pure Qt — no ONNX Runtime needed. Uses a synthetic vocab.txt
// fixture whose line numbers ARE the token IDs (BERT convention):
//   0 [PAD], 1..99 [unusedN], 100 [UNK], 101 [CLS], 102 [SEP],
//   103 hello, 104 world, 105 cafe, 106 ##s, 107 ., 108 run,
//   109 ##ning, 110 123
//
// Uses the Qt Test framework.
// ============================================================

#include "../src/embeddings/BgeTokenizer.h"

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>

using DocuSearch::BgeTokenizer;

namespace {

// Writes the synthetic vocab fixture; returns the file path.
QString writeFixtureVocab(const QTemporaryDir& dir) {
    QStringList lines;
    lines << QStringLiteral("[PAD]");
    for (int i = 1; i <= 99; ++i) {
        lines << QStringLiteral("[unused%1]").arg(i);
    }
    lines << QStringLiteral("[UNK]")
          << QStringLiteral("[CLS]")
          << QStringLiteral("[SEP]")
          << QStringLiteral("hello")
          << QStringLiteral("world")
          << QStringLiteral("cafe")
          << QStringLiteral("##s")
          << QStringLiteral(".")
          << QStringLiteral("run")
          << QStringLiteral("##ning")
          << QStringLiteral("123");
    const QString path = dir.filePath(QStringLiteral("vocab.txt"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return QString();
    f.write(lines.join(QLatin1Char('\n')).toUtf8());
    f.close();
    return path;
}

} // namespace

class TestBgeTokenizer : public QObject {
    Q_OBJECT
private slots:

    // ---- vocab loading -----------------------------------------------------

    void withoutVocabEncodeIsEmpty() {
        BgeTokenizer t;
        QVERIFY(!t.hasVocabulary());
        const auto out = t.encode(QStringLiteral("hello world"));
        QVERIFY(out.inputIds.empty());
        QVERIFY(out.attentionMask.empty());
        QVERIFY(out.tokenTypeIds.empty());
    }

    void vocabLoads() {
        QTemporaryDir dir;
        const QString path = writeFixtureVocab(dir);
        BgeTokenizer t;
        QVERIFY(!path.isEmpty());
        QVERIFY(t.loadVocabulary(path));
        QVERIFY(t.hasVocabulary());
    }

    void missingVocabFileFails() {
        BgeTokenizer t;
        QVERIFY(!t.loadVocabulary(QStringLiteral("/nonexistent/vocab.txt")));
        QVERIFY(!t.hasVocabulary());
    }

    // ---- exact-length (unpadded) output ------------------------------------

    void basicEncode() {
        QTemporaryDir dir;
        BgeTokenizer t;
        QVERIFY(t.loadVocabulary(writeFixtureVocab(dir)));

        const auto out = t.encode(QStringLiteral("hello world"));
        QCOMPARE(static_cast<int>(out.inputIds.size()), 4);   // CLS hello world SEP
        QCOMPARE(out.inputIds,
                 (std::vector<int64_t>{101, 103, 104, 102}));
        QCOMPARE(out.attentionMask,
                 (std::vector<int64_t>{1, 1, 1, 1}));
        QCOMPARE(out.tokenTypeIds,
                 (std::vector<int64_t>{0, 0, 0, 0}));
    }

    void noFixedPadding() {
        // Regression: the tokenizer used to always emit exactly 128
        // entries (padded), forcing truncated chunks and 128-token
        // query inference. Output length must now be exact.
        QTemporaryDir dir;
        BgeTokenizer t;
        QVERIFY(t.loadVocabulary(writeFixtureVocab(dir)));

        const auto out = t.encode(QStringLiteral("hello"));
        QCOMPARE(static_cast<int>(out.inputIds.size()), 3);   // CLS hello SEP
        QCOMPARE(static_cast<int>(out.attentionMask.size()), 3);
        QCOMPARE(static_cast<int>(out.tokenTypeIds.size()), 3);
    }

    void emptyTextYieldsClsSepOnly() {
        QTemporaryDir dir;
        BgeTokenizer t;
        QVERIFY(t.loadVocabulary(writeFixtureVocab(dir)));

        const auto out = t.encode(QString());
        QCOMPARE(out.inputIds, (std::vector<int64_t>{101, 102}));
    }

    // ---- text normalization ------------------------------------------------

    void punctuationSplitsPerCharacter() {
        // Regression: "..." used to be kept as ONE token (→ UNK);
        // BERT splits every punctuation mark individually.
        QTemporaryDir dir;
        BgeTokenizer t;
        QVERIFY(t.loadVocabulary(writeFixtureVocab(dir)));

        const auto out = t.encode(QStringLiteral("hello..."));
        QCOMPARE(out.inputIds,
                 (std::vector<int64_t>{101, 103, 107, 107, 107, 102}));
    }

    void accentsFoldToAsciiBase() {
        // Regression: "café" used to become "caf" (é dropped) — a
        // different word. Must fold to "cafe" exactly.
        QTemporaryDir dir;
        BgeTokenizer t;
        QVERIFY(t.loadVocabulary(writeFixtureVocab(dir)));

        const auto accented = t.encode(QString::fromUtf8("caf\xc3\xa9"));  // café
        const auto plain    = t.encode(QStringLiteral("cafe"));
        QCOMPARE(accented.inputIds, plain.inputIds);
        QCOMPARE(accented.inputIds, (std::vector<int64_t>{101, 105, 102}));
    }

    void nonAsciiNonLetterDropped() {
        QTemporaryDir dir;
        BgeTokenizer t;
        QVERIFY(t.loadVocabulary(writeFixtureVocab(dir)));

        // CJK has no ASCII base after NFD — dropped (English model).
        const auto out = t.encode(QString::fromUtf8("hello \xe4\xb8\x96\xe7\x95\x8c"));
        QCOMPARE(out.inputIds, (std::vector<int64_t>{101, 103, 102}));
    }

    void whitespaceNormalized() {
        QTemporaryDir dir;
        BgeTokenizer t;
        QVERIFY(t.loadVocabulary(writeFixtureVocab(dir)));

        const auto out = t.encode(QStringLiteral("hello\tworld\n"));
        QCOMPARE(out.inputIds, (std::vector<int64_t>{101, 103, 104, 102}));
    }

    void digitsKept() {
        QTemporaryDir dir;
        BgeTokenizer t;
        QVERIFY(t.loadVocabulary(writeFixtureVocab(dir)));

        const auto out = t.encode(QStringLiteral("123"));
        QCOMPARE(out.inputIds, (std::vector<int64_t>{101, 110, 102}));
    }

    // ---- WordPiece ---------------------------------------------------------

    void subwordDecomposition() {
        QTemporaryDir dir;
        BgeTokenizer t;
        QVERIFY(t.loadVocabulary(writeFixtureVocab(dir)));

        // "running" → "run" + "##ning"
        const auto out = t.encode(QStringLiteral("running"));
        QCOMPARE(out.inputIds, (std::vector<int64_t>{101, 108, 109, 102}));
    }

    void unknownWordMapsToUnk() {
        QTemporaryDir dir;
        BgeTokenizer t;
        QVERIFY(t.loadVocabulary(writeFixtureVocab(dir)));

        const auto out = t.encode(QStringLiteral("zzz"));
        QCOMPARE(out.inputIds, (std::vector<int64_t>{101, 100, 102}));
    }

    // ---- length cap --------------------------------------------------------

    void longInputCappedAt512WithTrailingSep() {
        QTemporaryDir dir;
        BgeTokenizer t;
        QVERIFY(t.loadVocabulary(writeFixtureVocab(dir)));

        // 600 known words → 602 tokens → capped at 512, last is [SEP].
        QString text;
        for (int i = 0; i < 600; ++i) {
            text += QStringLiteral(i % 2 == 0 ? "hello " : "world ");
        }
        const auto out = t.encode(text);
        QCOMPARE(static_cast<int>(out.inputIds.size()),
                 BgeTokenizer::MAX_SEQ_LENGTH);
        QCOMPARE(BgeTokenizer::MAX_SEQ_LENGTH, 512);
        QCOMPARE(out.inputIds.front(), static_cast<int64_t>(101));  // CLS
        QCOMPARE(out.inputIds.back(),  static_cast<int64_t>(102));  // SEP
        QCOMPARE(static_cast<int>(out.attentionMask.size()), 512);
    }

    void mediumInputNotCapped() {
        QTemporaryDir dir;
        BgeTokenizer t;
        QVERIFY(t.loadVocabulary(writeFixtureVocab(dir)));

        // ~250 words ≈ a full 1000-char chunk — well under the 512 cap,
        // so nothing may be truncated anymore.
        QString text;
        for (int i = 0; i < 250; ++i) text += QStringLiteral("hello ");
        const auto out = t.encode(text);
        QCOMPARE(static_cast<int>(out.inputIds.size()), 252);  // 250 + CLS + SEP
    }
};

QTEST_GUILESS_MAIN(TestBgeTokenizer)
#include "tst_BgeTokenizer.moc"
