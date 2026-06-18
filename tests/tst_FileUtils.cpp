// ============================================================
// tst_FileUtils.cpp — Unit tests for core/FileUtils
// ============================================================
//
// Covers: extensionOf, hasExtension, isUnderAny, toNative,
//         filetimeToQDateTime, sha256OfFile, readHead.
//
// Uses the Qt Test framework and a temp dir for file I/O.
// ============================================================

#include "../src/core/FileUtils.h"
#include "../src/core/Constants.h"

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QDir>

using DocuSearch::FileUtils::extensionOf;
using DocuSearch::FileUtils::hasExtension;
using DocuSearch::FileUtils::isUnderAny;
using DocuSearch::FileUtils::toNative;
using DocuSearch::FileUtils::filetimeToQDateTime;
using DocuSearch::FileUtils::sha256OfFile;
using DocuSearch::FileUtils::readHead;

class TestFileUtils : public QObject {
    Q_OBJECT
private slots:

    // ---- extensionOf ------------------------------------------------------

    void extensionOf_basic() {
        QCOMPARE(extensionOf("foo.pdf"),    QString("pdf"));
        QCOMPARE(extensionOf("foo.TXT"),    QString("txt"));  // lowercase
        QCOMPARE(extensionOf("foo.tar.gz"), QString("gz"));
    }
    void extensionOf_noDot() {
        QVERIFY(extensionOf("README").isEmpty());
    }
    void extensionOf_trailingDot() {
        // "foo." -> empty extension
        QCOMPARE(extensionOf("foo."), QString(""));
    }
    void extensionOf_pathWithDots() {
        // Make sure directory names with dots don't pollute the ext
        QCOMPARE(extensionOf("C:/my.folder/file.txt"), QString("txt"));
    }
    void extensionOf_directorySeparator() {
        // A "dot" followed by a slash should not be treated as an extension
        QVERIFY(extensionOf("foo.bar/baz").isEmpty());
    }

    // ---- hasExtension -----------------------------------------------------

    void hasExtension_match() {
        QVERIFY(hasExtension("foo.pdf", {"pdf", "doc"}));
    }
    void hasExtension_caseInsensitive() {
        QVERIFY(hasExtension("FOO.PDF", {"pdf"}));
    }
    void hasExtension_noMatch() {
        QVERIFY(!hasExtension("foo.zip", {"pdf", "doc"}));
    }

    // ---- isUnderAny -------------------------------------------------------

    void isUnderAny_match() {
        QVERIFY(isUnderAny("D:\\Movies\\movie.mp4",
                           {"D:\\Movies"}));
    }
    void isUnderAny_subfolder() {
        QVERIFY(isUnderAny("D:\\Games\\Steam\\foo.exe",
                           {"D:\\Games"}));
    }
    void isUnderAny_noMatch() {
        QVERIFY(!isUnderAny("D:\\Docs\\file.pdf",
                            {"D:\\Movies", "D:\\Games"}));
    }
    void isUnderAny_siblingPrefix() {
        // "D:\\Movies Archive" should NOT be considered under "D:\\Movies"
        QVERIFY(!isUnderAny("D:\\Movies Archive\\x.mp4",
                            {"D:\\Movies"}));
    }
    void isUnderAny_forwardSlashes() {
        // Forward-slash input should be normalized before comparison
        QVERIFY(isUnderAny("D:/Movies/sub/x.mp4",
                           {"D:\\Movies"}));
    }

    // ---- toNative ---------------------------------------------------------

    void toNative_convertsSeparators() {
        // On Windows this should be backslashes; on Linux, forward slashes.
        // Either way the result must not contain the "other" separator.
        const QString native = toNative("a/b/c");
        QVERIFY(!native.contains('/') || !native.contains('\\'));
    }

    // ---- filetimeToQDateTime ---------------------------------------------

    void filetimeToQDateTime_unixEpoch() {
        // 1970-01-01 00:00:00 UTC as FILETIME = 116444736000000000 (100-ns)
        const quint64 ft = 116444736000000000ULL;
        const QDateTime dt = filetimeToQDateTime(ft);
        QCOMPARE(dt.toMSecsSinceEpoch(), qint64(0));
    }
    void filetimeToQDateTime_knownDate() {
        // 2025-01-01 00:00:00 UTC as FILETIME
        // = (1735689600 * 10^7) + 116444736000000000
        const quint64 unixSecs = 1735689600ULL;
        const quint64 ft = unixSecs * 10000000ULL + 116444736000000000ULL;
        const QDateTime dt = filetimeToQDateTime(ft);
        QCOMPARE(dt.toMSecsSinceEpoch(), qint64(unixSecs) * 1000);
    }

    // ---- sha256OfFile -----------------------------------------------------

    void sha256OfFile_knownContent() {
        // SHA-256 of "hello\n" (6 bytes) — verified with Python:
        //   hashlib.sha256(b"hello\n").hexdigest()
        QTemporaryFile f;
        QVERIFY(f.open());
        f.write("hello\n");
        f.close();

        const QString h = sha256OfFile(f.fileName());
        QCOMPARE(h, QString("5891b5b522d5df086d0ff0b110fbd9d21bb4fc7163af34d08286a2e846f6be03"));
    }
    void sha256OfFile_empty() {
        QTemporaryFile f;
        QVERIFY(f.open());
        f.close();
        // SHA-256 of empty input
        const QString h = sha256OfFile(f.fileName());
        QCOMPARE(h, QString("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    }
    void sha256OfFile_nonExistent() {
        QVERIFY(sha256OfFile("/nonexistent/path/xyz.bin").isEmpty());
    }

    // ---- readHead ---------------------------------------------------------

    void readHead_basic() {
        QTemporaryFile f;
        QVERIFY(f.open());
        f.write("0123456789ABCDEF");
        f.flush();

        const QByteArray head = readHead(f.fileName(), 8);
        QCOMPARE(head, QByteArray("01234567"));
    }
    void readHead_moreThanAvailable() {
        QTemporaryFile f;
        QVERIFY(f.open());
        f.write("short");
        f.flush();

        const QByteArray head = readHead(f.fileName(), 1024);
        QCOMPARE(head, QByteArray("short"));
    }
    void readHead_nonExistent() {
        QVERIFY(readHead("/nonexistent", 100).isEmpty());
    }

    // ---- walkDirectory ----------------------------------------------------

    void walkDirectory_visitsFiles() {
        // Create a small tree and verify we visit all files exactly once,
        // and that we skip the excluded folder.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.path();

        // Create directory structure
        QVERIFY(QDir(root).mkpath("sub1"));
        QVERIFY(QDir(root).mkpath("sub2/excluded"));

        // Helper to write a small file
        auto touch = [](const QString& path, const char* data = "x") {
            QFile f(path);
            if (f.open(QIODevice::WriteOnly)) f.write(data);
        };
        touch(root + "/file1.txt");
        touch(root + "/sub1/file2.txt");
        touch(root + "/sub2/file3.txt");
        touch(root + "/sub2/excluded/secret.txt");

        QStringList visited;
        DocuSearch::FileUtils::walkDirectory(
            root,
            { root + "/sub2/excluded" },
            [&](const QFileInfo& info) {
                visited.append(info.absoluteFilePath());
                return true;
            });

        // 3 files should be visited (file1, file2, file3) — secret.txt skipped
        QCOMPARE(visited.size(), 3);
        for (const auto& v : visited) {
            QVERIFY(!v.contains("excluded"));
        }
    }
};

QTEST_GUILESS_MAIN(TestFileUtils)
#include "tst_FileUtils.moc"
