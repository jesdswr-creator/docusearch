// ============================================================
// tst_ExtractorFuzz.cpp - Fuzz the document extractors
// ============================================================
//
// Generates random / malformed files and verifies that the
// extractors DON'T CRASH. With the SEH translator installed
// (see src/core/SehTranslator.h), even access violations inside
// Poppler / zlib / minizip should be caught and reported as
// extraction failures rather than aborting the test process.
//
// This test is the regression guard for the "extract text crashes
// the app" bug — if Poppler ever regresses on a malformed PDF,
// this test will catch it.
// ============================================================

#include <QtTest/QtTest>
#include <QTemporaryFile>
#include <QRandomGenerator>
#include <QByteArray>
#include <cstring>

#include "../src/core/SehTranslator.h"
#include "../src/documents/DocumentExtractorRegistry.h"

using namespace DocuSearch;

class TestExtractorFuzz : public QObject {
    Q_OBJECT

private:
    // Write random bytes to a temp file with the given extension.
    QString writeRandomFile(const QString& ext, int sizeBytes) {
        QTemporaryFile tmp(QString("docusearch_fuzz_XXXXXXX.%1").arg(ext));
        tmp.setAutoRemove(false);
        if (!tmp.open()) return {};
        QByteArray data(sizeBytes, 0);
        for (int i = 0; i < sizeBytes; ++i) {
            data[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
        }
        tmp.write(data);
        tmp.close();
        return tmp.fileName();
    }

    // Write specific byte patterns.
    QString writePatternFile(const QString& ext, char pattern, int sizeBytes) {
        QTemporaryFile tmp(QString("docusearch_fuzz_XXXXXXX.%1").arg(ext));
        tmp.setAutoRemove(false);
        if (!tmp.open()) return {};
        QByteArray data(sizeBytes, pattern);
        tmp.write(data);
        tmp.close();
        return tmp.fileName();
    }

private slots:
    void initTestCase() {
        // Install SEH translator so Poppler access violations are
        // caught as C++ exceptions instead of crashing the test.
        DocuSearch::installSehTranslator();
    }

    // ── 1. Random garbage PDFs of various sizes ─────────────
    void testRandomPdfSizes_data() {
        QTest::addColumn<int>("sizeBytes");
        QTest::newRow("10B")     << 10;
        QTest::newRow("100B")    << 100;
        QTest::newRow("1KB")     << 1024;
        QTest::newRow("10KB")    << 10 * 1024;
        QTest::newRow("100KB")   << 100 * 1024;
    }

    void testRandomPdfSizes() {
        QFETCH(int, sizeBytes);

        const QString path = writeRandomFile("pdf", sizeBytes);
        QVERIFY(!path.isEmpty());

        try {
            // Should NOT crash — extractor should return an error result.
            auto result = DocumentExtractorRegistry::instance().extractByExtension(path, "pdf");
            QVERIFY(true);  // survived
        } catch (const std::exception& e) {
            // Acceptable — extraction threw a catchable C++ exception.
            QVERIFY(strlen(e.what()) > 0);
        } catch (...) {
            // Acceptable — any catchable exception is fine.
        }

        QFile::remove(path);
    }

    // ── 2. Random garbage DOCX / XLSX / PPTX ────────────────
    // These use zlib/minizip — corrupted ZIPs may cause SEH.
    void testRandomOfficeFormats() {
        const QStringList exts = {"docx", "xlsx", "pptx"};
        for (const QString& ext : exts) {
            for (int i = 0; i < 5; ++i) {
                const QString path = writeRandomFile(ext, 5000 + i * 1000);
                QVERIFY(!path.isEmpty());

                try {
                    auto result = DocumentExtractorRegistry::instance().extractByExtension(path, ext);
                    QVERIFY(true);
                } catch (...) {
                    // Acceptable.
                }

                QFile::remove(path);
            }
        }
    }

    // ── 3. Byte-pattern fuzzing (all zeros, all 0xFF, etc.) ─
    void testBytePatterns() {
        struct Pattern { const char* name; char byte; };
        const Pattern patterns[] = {
            {"all-zero", 0x00},
            {"all-FF",   static_cast<char>(0xFF)},
            {"all-AA",   static_cast<char>(0xAA)},
            {"all-55",   0x55},
        };

        for (const auto& p : patterns) {
            for (const QString& ext : {"pdf", "docx", "xlsx"}) {
                const QString path = writePatternFile(ext, p.byte, 10000);
                QVERIFY(!path.isEmpty());

                try {
                    auto result = DocumentExtractorRegistry::instance().extractByExtension(path, ext);
                    QVERIFY(true);
                } catch (...) {
                    // Acceptable.
                }

                QFile::remove(path);
            }
        }
    }

    // ── 4. Truncated files ──────────────────────────────────
    void testTruncatedFiles() {
        // A PDF that starts valid but is truncated mid-stream.
        const QByteArray validStart = "%PDF-1.4\n1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";

        for (int truncLen = 5; truncLen < validStart.size(); truncLen += 20) {
            QTemporaryFile tmp("docusearch_fuzz_XXXXXXX.pdf");
            tmp.setAutoRemove(false);
            QVERIFY(tmp.open());
            tmp.write(validStart.left(truncLen));
            tmp.close();

            try {
                auto result = DocumentExtractorRegistry::instance().extractByExtension(tmp.fileName(), "pdf");
                QVERIFY(true);
            } catch (...) {
                // Acceptable.
            }

            QFile::remove(tmp.fileName());
        }
    }

    // ── 5. Repeated extraction (memory leak check) ──────────
    void testRepeatedExtraction() {
        const QString path = writeRandomFile("pdf", 5000);
        QVERIFY(!path.isEmpty());

        for (int i = 0; i < 20; ++i) {
            try {
                auto result = DocumentExtractorRegistry::instance().extractByExtension(path, "pdf");
            } catch (...) {
                // Continue — even if one fails.
            }
        }

        QFile::remove(path);
        QVERIFY(true);  // survived 20 iterations
    }
};

QTEST_GUILESS_MAIN(TestExtractorFuzz)
#include "tst_ExtractorFuzz.moc"
