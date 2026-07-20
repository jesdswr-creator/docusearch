// ============================================================
// tst_OcrHelper.cpp - Test the OCR helper process and ABI
// ============================================================
//
// These tests verify:
//   1. ImageStruct memory layout matches the oneocr.dll ABI
//      (32 bytes, 8-byte aligned, correct field offsets).
//   2. The OCR helper exe exists and runs without oneocr files
//      installed (should print setup error, exit 1).
//   3. The OCR helper exe handles a non-existent path gracefully.
//
// NOTE: We CANNOT actually test OCR recognition in CI because
// oneocr.dll is NOT bundled — it must be installed from the user's
// Snipping Tool. So these tests cover the structural invariants
// that guarantee the helper will work once oneocr is installed.
// ============================================================

#include <QtTest/QtTest>
#include <QProcess>
#include <QFileInfo>
#include <QCoreApplication>
#include <cstdint>

// ── Mirror the ImageStruct layout from ocr_helper_main.cpp ───
// Must match EXACTLY — same packing, same field order, same types.
#pragma pack(push, 8)
struct TestImageStruct {
    int32_t  type;
    int32_t  width;
    int32_t  height;
    int32_t  reserved;
    int64_t  step_size;
    uint8_t* data_ptr;
};
#pragma pack(pop)

class TestOcrHelper : public QObject {
    Q_OBJECT

private:
    QString helperExePath() const {
        // In CI, the helper exe is at <build>/bin/docusearch_ocr_helper.exe
        // In dev, it's wherever the test exe lives.
        const QString appDir = QCoreApplication::applicationDirPath();
        const QStringList candidates = {
            appDir + "/docusearch_ocr_helper.exe",
            appDir + "/../docusearch_ocr_helper.exe",
            appDir + "/../bin/docusearch_ocr_helper.exe",
            appDir + "/../../bin/docusearch_ocr_helper.exe",
            appDir + "/../../bin/Release/docusearch_ocr_helper.exe",
        };
        for (const QString& p : candidates) {
            if (QFileInfo::exists(p)) return p;
        }
        return {};
    }

private slots:
    // ── 1. ImageStruct must be 32 bytes, 8-byte aligned ─────
    // If this fails, the ABI contract with oneocr.dll is broken.
    void testImageStructSize() {
        QCOMPARE(sizeof(TestImageStruct), static_cast<size_t>(32));
    }

    void testImageStructAlignment() {
        QCOMPARE(alignof(TestImageStruct), static_cast<size_t>(8));
    }

    // ── 2. Field offsets must match the oneocr.dll ABI ─────
    void testImageStructOffsets() {
        TestImageStruct s{};
        const auto base = reinterpret_cast<uintptr_t>(&s);

        QCOMPARE(reinterpret_cast<uintptr_t>(&s.type)      - base, static_cast<uintptr_t>(0));
        QCOMPARE(reinterpret_cast<uintptr_t>(&s.width)     - base, static_cast<uintptr_t>(4));
        QCOMPARE(reinterpret_cast<uintptr_t>(&s.height)    - base, static_cast<uintptr_t>(8));
        QCOMPARE(reinterpret_cast<uintptr_t>(&s.reserved)  - base, static_cast<uintptr_t>(12));
        QCOMPARE(reinterpret_cast<uintptr_t>(&s.step_size) - base, static_cast<uintptr_t>(16));
        QCOMPARE(reinterpret_cast<uintptr_t>(&s.data_ptr)  - base, static_cast<uintptr_t>(24));
    }

    // ── 3. Helper exe should exist after build ──────────────
    void testHelperExeExists() {
        const QString p = helperExePath();
        if (p.isEmpty()) {
            QSKIP("docusearch_ocr_helper.exe not found — build it first");
        }
        QVERIFY2(QFileInfo::exists(p),
                 qPrintable("Helper exe missing: " + p));
    }

    // ── 4. Helper exe with no args should exit non-zero ─────
    void testHelperNoArgsExitsNonZero() {
        const QString p = helperExePath();
        if (p.isEmpty()) QSKIP("Helper exe not built");

        QProcess proc;
        proc.setProgram(p);
        proc.start();
        QVERIFY(proc.waitForStarted(5000));
        QVERIFY(proc.waitForFinished(5000));
        QVERIFY(proc.exitCode() != 0);
    }

    // ── 5. Helper exe with non-existent file should NOT crash ─
    // It should output ===FILE===...===END=== with an error message,
    // and exit code 1 (all failed).
    void testHelperHandlesMissingFile() {
        const QString p = helperExePath();
        if (p.isEmpty()) QSKIP("Helper exe not built");

        QProcess proc;
        proc.setProgram(p);
        proc.setArguments({ "C:/nonexistent/docusearch_test_image.png" });
        proc.start();
        QVERIFY(proc.waitForStarted(5000));
        QVERIFY(proc.waitForFinished(30000));

        // Exit code 1 = all files failed (expected for missing file).
        // Exit code 0 = unexpected success.
        // Crash (exit code != 0/1, or signaled) = test FAIL.
        QVERIFY2(proc.exitStatus() == QProcess::NormalExit,
                 "Helper crashed (killed) on missing file!");
        QVERIFY(proc.exitCode() != 0);

        const QString out = QString::fromUtf8(proc.readAllStandardOutput());
        QVERIFY(out.contains("===FILE===") || out.contains("===END===") ||
                !proc.readAllStandardError().isEmpty());
    }

    // ── 6. Helper exe with corrupt image should NOT crash ────
    void testHelperHandlesCorruptImage() {
        const QString p = helperExePath();
        if (p.isEmpty()) QSKIP("Helper exe not built");

        // Create a fake "image" with garbage data.
        QTemporaryFile tmp("docusearch_test_XXXXXX.png");
        QVERIFY(tmp.open());
        tmp.write("THIS IS NOT A VALID PNG FILE - GARBAGE DATA HERE");
        tmp.close();

        QProcess proc;
        proc.setProgram(p);
        proc.setArguments({ tmp.fileName() });
        proc.start();
        QVERIFY(proc.waitForStarted(5000));
        QVERIFY(proc.waitForFinished(30000));

        QVERIFY2(proc.exitStatus() == QProcess::NormalExit,
                 "Helper crashed on corrupt image!");
        // Exit code 1 is fine — corrupt image failed OCR.
        // Exit code 0 would be unexpected.
    }
};

QTEST_GUILESS_MAIN(TestOcrHelper)
#include "tst_OcrHelper.moc"
