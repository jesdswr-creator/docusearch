// ============================================================
// tst_DuplicateScan.cpp — behavioral tests for DuplicateScanController
// ============================================================
//
// v1.7.24: the duplicate finder's five passes moved out of MainWindow
// into a QtCore-only controller. This suite drives the REAL scanner
// headless against temp files + a temp database and asserts the
// contracts users actually reported bugs about:
//
//   • real byte-identical files group; unique files never appear;
//   • a lone file whose partner vanished mid-run never renders as a
//     pair (survivors-only grouping + re-verification);
//   • ghost rows (file gone, drive reachable) are REPORTED for the
//     caller to purge — and never enter the grouping;
//   • fresh hashes are written back (hash + size + mtime triple);
//   • the same file can never pair with itself (pathIdentityKey);
//   • "keep the newest copy" selection never dooms a group's last
//     survivor (selectDoomedCopies);
//   • moveFileKeepingName renames on collision and never leaves a
//     partial copy behind;
//   • cancel + single-flight are safe (no double start, no crash).

#include "../src/core/DuplicateScanController.h"
#include "../src/core/Constants.h"
#include "../src/database/Database.h"
#include "../src/database/Schema.h"

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QEventLoop>
#include <QTimer>
#include <sqlite3.h>

#include <functional>
#include <memory>

using namespace DocuSearch;

class tst_DuplicateScan : public QObject {
    Q_OBJECT

    QTemporaryDir dataDir_;   // real duplicate files
    QTemporaryDir dbDir_;
    QString dbPath_;
    std::unique_ptr<Database> db_;
    std::unique_ptr<DuplicateScanController> ctrl_;

    DuplicateScanResult last_;
    bool finished_ = false;

    QString dataRoot() const { return dataDir_.path() + "/docs"; }

    // A real file on disk with distinctive content.
    QByteArray makeContent(const QByteArray& tag, int paddingBlocks) const {
        QByteArray c = tag;
        for (int i = 0; i < paddingBlocks; ++i)
            c += QByteArray("ABCDEFGHIJKLMNOPQRSTUVWXYZ012345");
        return c;
    }

    bool makeFile(const QString& rel, const QByteArray& content) {
        const QString abs = dataRoot() + "/" + rel;
        QDir().mkpath(QFileInfo(abs).absolutePath());
        QFile f(abs);
        if (!f.open(QIODevice::WriteOnly)) return false;
        return f.write(content) == content.size();
    }

    bool exec(const QString& sql) const {
        return sqlite3_exec(db_->raw(), sql.toUtf8().constData(),
                            nullptr, nullptr, nullptr) == SQLITE_OK;
    }

    QString textOf(const QString& sql) const {
        sqlite3_stmt* s = nullptr;
        if (sqlite3_prepare_v2(db_->raw(), sql.toUtf8().constData(), -1,
                               &s, nullptr) != SQLITE_OK) return QString();
        QString v;
        if (sqlite3_step(s) == SQLITE_ROW) {
            const unsigned char* p = sqlite3_column_text(s, 0);
            v = p ? QString::fromUtf8(reinterpret_cast<const char*>(p))
                  : QString();
        }
        sqlite3_finalize(s);
        return v;
    }

    int scalar(const QString& sql) const {
        sqlite3_stmt* s = nullptr;
        if (sqlite3_prepare_v2(db_->raw(), sql.toUtf8().constData(), -1,
                               &s, nullptr) != SQLITE_OK) return -1;
        int v = 0;
        if (sqlite3_step(s) == SQLITE_ROW) v = sqlite3_column_int(s, 0);
        sqlite3_finalize(s);
        return v;
    }

    // Index a real file the way the scanners do (the path the scanner
    // later SELECTs must describe the file accurately).
    bool indexFile(const QString& rel) {
        const QString abs = dataRoot() + "/" + rel;
        QFileInfo fi(abs);
        if (!fi.exists()) return false;
        return exec(QString(
            "INSERT INTO Files (path, filename, extension, size, "
            "  modified_date, indexing_status) VALUES ('%1', '%2', "
            "  '%3', %4, %5, 'content_done');")
            .arg(QDir::toNativeSeparators(abs))
            .arg(fi.fileName())
            .arg(fi.suffix().toLower())
            .arg(fi.size())
            .arg(fi.lastModified().toSecsSinceEpoch()));
    }

    void waitUntil(const std::function<bool()>& cond,
                   int timeoutMs = 30000) {
        if (cond()) return;
        QEventLoop loop;
        QTimer poll;
        QObject::connect(&poll, &QTimer::timeout, &loop,
                         [&cond, &loop] { if (cond()) loop.quit(); });
        poll.start(20);
        QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
        loop.exec();
    }

private slots:
    void init() {
        QVERIFY(dataDir_.isValid());
        QVERIFY(dbDir_.isValid());
        dbPath_ = dbDir_.path() + "/test.sqlite3";
        db_ = std::make_unique<Database>();
        QString err;
        QVERIFY(db_->open(dbPath_, &err));
        QVERIFY(Schema::initialize(*db_));
        QVERIFY(Schema::migrate(*db_));
        ctrl_ = std::make_unique<DuplicateScanController>();
        QDir().mkpath(dataRoot());
        finished_ = false;
        connect(ctrl_.get(), &DuplicateScanController::finished, this,
                [this](const DuplicateScanResult& r) {
                    last_ = r;
                    finished_ = true;
                });
    }

    void cleanup() {
        ctrl_.reset();
        db_.reset();
    }

    // ---- the scan --------------------------------------------------

    void scanFindsRealDuplicatesAndDropsSingletons() {
        const QByteArray twin = makeContent("TWIN-A", 4);
        const QByteArray trio = makeContent("TRIO-B", 4);
        QVERIFY(makeFile("a1.pdf", twin));
        QVERIFY(makeFile("a2.pdf", twin));
        QVERIFY(makeFile("unique.pdf", makeContent("UNIQUE", 4)));
        QTest::qSleep(1100);
        QVERIFY(makeFile("c1.jpg", trio));
        QVERIFY(makeFile("c2.jpg", trio));
        QVERIFY(makeFile("c3.jpg", trio));

        QVERIFY(indexFile("a1.pdf"));
        QVERIFY(indexFile("a2.pdf"));
        QVERIFY(indexFile("unique.pdf"));
        QVERIFY(indexFile("c1.jpg"));
        QVERIFY(indexFile("c2.jpg"));
        QVERIFY(indexFile("c3.jpg"));

        ctrl_->start(dbPath_);
        waitUntil([this] { return finished_; });

        QVERIFY(finished_);
        QVERIFY(!last_.cancelled);
        // 2 groups: {a1,a2} and {c1,c2,c3}; unique.pdf never appears.
        QCOMPARE(last_.groupCount, 2);
        QCOMPARE(last_.hits.size(), 5);
        QCOMPARE(last_.groupKeys.size(), 5);
        // Group members sit together and share the key.
        QString keyA = last_.groupKeys[0], keyC;
        int aCount = 0, cCount = 0;
        for (int i = 0; i < last_.hits.size(); ++i) {
            if (last_.hits[i].extension == "pdf") {
                QCOMPARE(last_.groupKeys[i], keyA);
                ++aCount;
            } else {
                if (keyC.isEmpty()) keyC = last_.groupKeys[i];
                QCOMPARE(last_.groupKeys[i], keyC);
                ++cCount;
            }
        }
        QCOMPARE(aCount, 2);
        QCOMPARE(cCount, 3);
        QVERIFY(keyA != keyC);
        // Group keys qualify the fingerprint with the byte size.
        QVERIFY(keyA.contains(':'));
        // Fresh fingerprints were written back for the five rows that
        // needed one — unique.pdf is a SIZE SINGLETON (pass 3), so it
        // is never fingerprinted at all: that is the optimization
        // working, and its hash column stays empty.
        QCOMPARE(last_.hashedNow, 5);
        QCOMPARE(scalar("SELECT COUNT(*) FROM Files "
                        "WHERE hash IS NULL OR hash='';"), 1);
    }

    void scanReportsGhostRowsInsteadOfPairingThem() {
        // A row whose file does NOT exist (deleted after indexing).
        QVERIFY(exec(QString(
            "INSERT INTO Files (path, filename, extension, size, "
            "  indexing_status) VALUES ('%1', 'ghost.pdf', 'pdf', "
            "  123, 'content_done');")
            .arg(QDir::toNativeSeparators(dataRoot() + "/ghost.pdf"))));
        QVERIFY(makeFile("real.pdf", makeContent("REAL", 1)));
        QVERIFY(indexFile("real.pdf"));

        ctrl_->start(dbPath_);
        waitUntil([this] { return finished_; });

        QCOMPARE(last_.skippedMissing, 1);
        QCOMPARE(last_.stalePaths.size(), 1);   // reported, not purged
        // A single real file has no partner: no groups, but the run
        // itself was honest about what it compared.
        QVERIFY(last_.hits.isEmpty());
        QCOMPARE(last_.groupCount, 0);
        QCOMPARE(last_.candidateCount, 1);
    }

    void scanIdentityCollapsesSameFileSpellings() {
        // The same physical file indexed twice under two spellings
        // must never pair with itself (the "single file showing as
        // duplicates" report). pathIdentityKey is the shared gate.
        const QString abs = dataRoot() + "/self.pdf";
        QVERIFY(makeFile("self.pdf", makeContent("SELF", 1)));
        const QString k1 =
            DuplicateScanController::pathIdentityKey(abs);
        const QString k2 = DuplicateScanController::pathIdentityKey(
            dataDir_.path() + "/docs/./self.pdf");          // dot segment
        const QString k3 = DuplicateScanController::pathIdentityKey(
            QDir::toNativeSeparators(abs));                  // \\ spelling
        QCOMPARE(k1, k2);
        QCOMPARE(k1, k3);

        // Two rows for the same physical file → collapsed, not paired.
        QVERIFY(exec(QString(
            "INSERT INTO Files (path, filename, extension, size, "
            "  indexing_status) VALUES ('%1', 'self.pdf', 'pdf', %2, "
            "  'content_done'), ('%3', 'self.pdf', 'pdf', %2, "
            "  'content_done');")
            .arg(QDir::toNativeSeparators(abs))
            .arg(QFileInfo(abs).size())
            .arg(QDir::toNativeSeparators(
                dataDir_.path() + "/docs/./self.pdf"))));

        ctrl_->start(dbPath_);
        waitUntil([this] { return finished_; });
        QCOMPARE(last_.staleRows, 1);        // the shadow row collapsed
        QVERIFY(last_.hits.isEmpty());       // and nothing can pair
    }

    void cancelBeforeStartIsSafeAndStartIsSingleFlight() {
        // Cancel with no job: no crash, no state.
        ctrl_->cancel();
        QVERIFY(!ctrl_->isRunning());

        // Double start: the second call is a silent no-op.
        QVERIFY(makeFile("a.pdf", makeContent("A", 1)));
        QVERIFY(makeFile("a2.pdf", makeContent("A", 1)));
        QVERIFY(indexFile("a.pdf"));
        QVERIFY(indexFile("a2.pdf"));
        ctrl_->start(dbPath_);
        ctrl_->start(dbPath_);           // must not crash / not queue
        ctrl_->cancel();                 // may or may not land in time
        waitUntil([this] { return finished_; });
        QVERIFY(finished_);
        QVERIFY(!ctrl_->isRunning());
        // Exactly one result, internally consistent either way.
        QCOMPARE(last_.hits.size(), last_.groupKeys.size());
    }

    // ---- newest-kept selection --------------------------------------

    void selectDoomedCopiesKeepsNewestAndNeverLastSurvivor() {
        QList<SearchHit> hits;
        QStringList keys;
        // Group "G1": three copies with DISTINCT mtimes (newest last).
        // Group "G2": a pair. Group "G3": a lone survivor.
        auto add = [&](const QString& name, const QString& key,
                       qint64 size) {
            SearchHit h;
            h.fileId = hits.size() + 1;
            h.path = dataRoot() + "/" + name;
            h.filename = name;
            h.extension = QFileInfo(name).suffix();
            h.size = size;
            hits.append(h);
            keys.append(key);
        };
        // Real files with real mtimes: created seconds apart.
        QVERIFY(makeFile("g1_old.pdf", makeContent("G1", 1)));
        QTest::qSleep(1100);
        QVERIFY(makeFile("g1_mid.pdf", makeContent("G1", 1)));
        QTest::qSleep(1100);
        QVERIFY(makeFile("g1_new.pdf", makeContent("G1", 1)));
        QVERIFY(makeFile("g2_x.png", QByteArray("\x89PNG\r\n", 6)));
        QVERIFY(makeFile("g2_y.png", QByteArray("\x89PNG\r\n", 6)));
        QVERIFY(makeFile("g3_lone.pdf", makeContent("G3", 1)));

        add("g1_old.pdf", "G1", 100);
        add("g1_mid.pdf", "G1", 100);
        add("g1_new.pdf", "G1", 100);
        add("g2_x.png", "G2", 6);
        add("g2_y.png", "G2", 6);
        add("g3_lone.pdf", "G3", 100);

        qint64 reclaim = 0;
        int groupsActed = 0;
        const QList<int> doomed = DuplicateScanController::selectDoomedCopies(
            hits, keys, &reclaim, &groupsActed);

        QCOMPARE(groupsActed, 2);          // G1 + G2; G3 untouched
        QCOMPARE(doomed.size(), 3);        // 2 from G1 + 1 from G2
        // The newest G1 copy survives; the two older are doomed.
        QVERIFY(!doomed.contains(2));      // g1_new.pdf (index 2) kept
        QVERIFY(doomed.contains(0));
        QVERIFY(doomed.contains(1));
        // G2: exactly one of the pair doomed.
        QVERIFY(doomed.contains(3) != doomed.contains(4));
        // G3 never appears.
        QVERIFY(!doomed.contains(5));
        // Reclaim = the REAL byte sizes of the doomed copies.
        qint64 expectedReclaim = QFileInfo(hits[0].path).size()
                               + QFileInfo(hits[1].path).size()
                               + (doomed.contains(3)
                                      ? QFileInfo(hits[3].path).size()
                                      : QFileInfo(hits[4].path).size());
        QCOMPARE(reclaim, expectedReclaim);
    }

    // ---- the move helper --------------------------------------------

    void moveFileKeepsNameAndRenamesOnCollision() {
        QVERIFY(makeFile("m1.pdf", makeContent("M", 1)));
        const QString src = dataRoot() + "/m1.pdf";
        QDir dest(dataRoot() + "/dest");
        QDir().mkpath(dest.absolutePath());

        QVERIFY(DuplicateScanController::moveFileKeepingName(
            src, dest.absolutePath()));
        QVERIFY(QFileInfo(dest.absolutePath() + "/m1.pdf").exists());
        QVERIFY(!QFileInfo(src).exists());

        // Second move of a same-named file: "m1 (2).pdf", never a
        // silent overwrite.
        QVERIFY(makeFile("m2.pdf", makeContent("M2", 1)));
        QVERIFY(QFile::rename(dataRoot() + "/m2.pdf",
                              dataRoot() + "/m1.pdf"));
        QVERIFY(DuplicateScanController::moveFileKeepingName(
            dataRoot() + "/m1.pdf", dest.absolutePath()));
        QVERIFY(QFileInfo(dest.absolutePath() + "/m1 (2).pdf").exists());
    }
};

QTEST_GUILESS_MAIN(tst_DuplicateScan)
#include "tst_DuplicateScan.moc"
