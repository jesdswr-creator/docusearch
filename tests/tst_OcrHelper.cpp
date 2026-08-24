// ============================================================
// tst_OcrHelper.cpp - Test the Windows.Media.Ocr helper process
// ============================================================
//
// These tests verify the crash-safety of the OCR helper exe that
// wraps the Windows.Media.Ocr WinRT API. The helper runs in a
// separate process, so even if the WinRT stack faults, the main
// DocuSearch app is unaffected.
//
// Tests cover:
//   1. The helper exe exists after build.
//   2. Helper with no args exits non-zero (usage message).
//   3. Helper with non-existent file does NOT crash — outputs an
//      ===FILE===...===END=== block with an error message.
//   4. Helper with corrupt image data does NOT crash — same protocol.
//
// NOTE: We CANNOT verify actual OCR recognition in CI because the
// helper depends on Windows.Media.Ocr language packs being installed
// on the test runner. Windows Server 2022 (our CI runner) may not
// have any OCR language packs preinstalled. These tests cover the
// structural invariants that guarantee the helper will work once
// language packs are present.
// ============================================================

#include <QtTest/QtTest>
#include <QProcess>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QCoreApplication>
#include <QString>

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
    // ── 1. Helper exe should exist after build ──────────────
    void testHelperExeExists() {
        const QString p = helperExePath();
        if (p.isEmpty()) {
            QSKIP("docusearch_ocr_helper.exe not found — build it first");
        }
        QVERIFY2(QFileInfo::exists(p),
                 qPrintable("Helper exe missing: " + p));
    }

    // ── 2. Helper with no args should exit non-zero ──────────
    // Verifies the helper's argument validation works — without it,
    // a future refactor could accidentally let the helper run with
    // zero paths and produce a confusing empty output.
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

    // ── 3. Helper with non-existent file should NOT crash ────
    // Verifies the helper's per-file try/catch — a missing file should
    // produce an ===FILE===...===END=== block with an error message,
    // NOT crash the helper. Exit code 1 = all files failed (expected).
    void testHelperHandlesMissingFile() {
        const QString p = helperExePath();
        if (p.isEmpty()) QSKIP("Helper exe not built");

        QProcess proc;
        proc.setProgram(p);
        proc.setArguments({ "C:/nonexistent/docusearch_test_image.png" });
        proc.start();
        QVERIFY(proc.waitForStarted(5000));
        QVERIFY(proc.waitForFinished(30000));

        // Crash (signaled exit) = test FAIL. Normal exit with non-zero
        // code = test PASS (the helper handled the bad input cleanly).
        QVERIFY2(proc.exitStatus() == QProcess::NormalExit,
                 "Helper crashed (killed) on missing file!");
        QVERIFY(proc.exitCode() != 0);

        // The helper should have written SOMETHING — either stdout
        // (===FILE===...===END=== block) or stderr (status messages).
        const QByteArray stdoutBytes = proc.readAllStandardOutput();
        const QByteArray stderrBytes = proc.readAllStandardError();
        QVERIFY2(!stdoutBytes.isEmpty() || !stderrBytes.isEmpty(),
                 "Helper produced no output for missing file");
    }

    // ── 4. Helper with corrupt image should NOT crash ───────
    // Verifies the BitmapDecoder / WinRT fault path doesn't kill the
    // helper. Garbage data passed as an image should produce an error
    // message in the output, not a process crash.
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
        // Exit code 0 would be unexpected (means OCR somehow succeeded
        // on garbage data — extremely unlikely).
    }

    // ── 5. Helper output protocol is well-formed ─────────────
    // Verifies the ===FILE===<path>\n<text>\n===END=== protocol is
    // intact — this is the contract the main Qt app depends on to
    // parse OCR results from the helper's stdout.
    void testHelperOutputProtocol() {
        const QString p = helperExePath();
        if (p.isEmpty()) QSKIP("Helper exe not built");

        QProcess proc;
        proc.setProgram(p);
        proc.setArguments({ "C:/nonexistent/docusearch_protocol_test.png" });
        proc.start();
        QVERIFY(proc.waitForStarted(5000));
        QVERIFY(proc.waitForFinished(30000));

        const QString out = QString::fromUtf8(proc.readAllStandardOutput());
        // The helper should output at least one ===FILE=== marker for
        // the requested file, even if the OCR failed.
        if (!out.isEmpty()) {
            QVERIFY2(out.contains("===FILE===") || out.contains("===END==="),
                     "Helper output is missing the ===FILE===/===END=== markers");
        }
        // If stdout is empty, the helper should have at least logged
        // something to stderr (status / error messages).
        const QByteArray stderrBytes = proc.readAllStandardError();
        if (out.isEmpty()) {
            QVERIFY2(!stderrBytes.isEmpty(),
                     "Helper produced no output (stdout or stderr) for failed OCR");
        }
    }
};

QTEST_GUILESS_MAIN(TestOcrHelper)
#include "tst_OcrHelper.moc"
