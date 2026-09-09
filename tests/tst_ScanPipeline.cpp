// ============================================================
// tst_ScanPipeline.cpp — behavioral tests for ScanPipelineController
// ============================================================
//
// v1.7.24: the scan pipeline (auto-scan, folder ingest, startup
// integrity pass, legacy purge) was extracted from MainWindow into a
// QtCore-only controller. This suite drives the REAL controller
// headless against a temp database + temp files and asserts the
// pipeline contracts that used to be untestable inside the window:
//
//   • the extension allowlist + user-exclusion gates on every ingest;
//   • new/changed/unchanged row semantics (queued rows must survive
//     unchanged scans untouched — the v1.7.3 regression class);
//   • the Pass-2 prune (rows whose file vanished are removed WITH
//     their EmbeddingChunks — the v1.7.24 orphaned-chunks fix);
//   • unavailable folders are skipped, never pruned;
//   • the integrity pass (fake-done requeue, failed-row retry, hash
//     backfill, the one-shot junk-audit flag contract);
//   • the folder ingest upsert with hash write-back (ON CONFLICT);
//   • the static purges (folder cascade, non-indexable rows);
//   • single-flight + FIFO queue semantics.

#include "../src/core/ScanPipelineController.h"
#include "../src/core/Constants.h"
#include "../src/core/Config.h"
#include "../src/core/StorageHealth.h"
#include "../src/database/Database.h"
#include "../src/database/Schema.h"

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QEventLoop>
#include <QTimer>
#include <sqlite3.h>

#include <functional>
#include <memory>

using namespace DocuSearch;

class tst_ScanPipeline : public QObject {
    Q_OBJECT

    QTemporaryDir dataDir_;     // scanned content
    QTemporaryDir dbDir_;       // the database
    QString dbPath_;
    std::unique_ptr<Database> db_;
    std::unique_ptr<ScanPipelineController> ctrl_;

    // Last-reported results (connected once, in init()).
    ScanStats       lastAuto_;
    bool            autoFired_    = false;
    IntegrityResult lastInteg_;
    bool            integFired_   = false;
    int             fsIndexed_    = -1;
    int             fsSkipped_    = -1;
    int             fsHashed_     = -1;
    bool            fsFired_      = false;
    int             lastPurge_    = -1;
    bool            purgeFired_   = false;

    QString dataRoot() const { return dataDir_.path() + "/library"; }

    bool makeFile(const QString& rel, const QByteArray& content) {
        const QString abs = dataRoot() + "/" + rel;
        QDir().mkpath(QFileInfo(abs).absolutePath());
        QFile f(abs);
        if (!f.open(QIODevice::WriteOnly)) return false;
        return f.write(content) == content.size();
    }

    // Append to change size AND ensure a fresh mtime second.
    bool changeFile(const QString& rel, const QByteArray& extra) {
        QTest::qSleep(1100);
        QFile f(dataRoot() + "/" + rel);
        if (!f.open(QIODevice::Append)) return false;
        return f.write(extra) == extra.size();
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

    bool exec(const QString& sql) const {
        return sqlite3_exec(db_->raw(), sql.toUtf8().constData(),
                            nullptr, nullptr, nullptr) == SQLITE_OK;
    }

    ScanParams params(const QStringList& folders,
                      bool hashEnabled = false,
                      const QStringList& excludedExts = {}) const {
        ScanParams p;
        p.folders            = folders;
        p.excludedFolders    = {};
        p.excludedExtensions = excludedExts;
        p.dbPath             = dbPath_;
        p.hashEnabled        = hashEnabled;
        return p;
    }

    // Polls the nested event loop until cond() turns true (or the
    // timeout elapses — every assert afterwards then reports honestly).
    void waitUntil(const std::function<bool()>& cond,
                   int timeoutMs = 20000) {
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
        ctrl_ = std::make_unique<ScanPipelineController>();
        QDir().mkpath(dataRoot());

        autoFired_ = false; integFired_ = false; fsFired_ = false;
        purgeFired_ = false;
        fsIndexed_ = -1; fsSkipped_ = -1; fsHashed_ = -1; lastPurge_ = -1;

        connect(ctrl_.get(),
            &ScanPipelineController::autoScanFinished, this,
            [this](const ScanStats& s) { lastAuto_ = s; autoFired_ = true; });
        connect(ctrl_.get(),
            &ScanPipelineController::integrityFinished, this,
            [this](const IntegrityResult& r) { lastInteg_ = r;
                                               integFired_ = true; });
        connect(ctrl_.get(),
            &ScanPipelineController::folderScanFinished, this,
            [this](int i, int s, int h) { fsIndexed_ = i; fsSkipped_ = s;
                                          fsHashed_ = h; fsFired_ = true; });
        connect(ctrl_.get(),
            &ScanPipelineController::purgeNonIndexableFinished, this,
            [this](int n) { lastPurge_ = n; purgeFired_ = true; });
    }

    void cleanup() {
        ctrl_.reset();
        db_.reset();
    }

    // ---- auto-scan -------------------------------------------------

    void autoScanIndexesOnlyAllowlistedFiles() {
        QVERIFY(makeFile("a.pdf", "%PDF-1.4 document"));
        QVERIFY(makeFile("notes.txt", "plain text"));
        QVERIFY(makeFile("pic.png", QByteArray("\x89PNG\r\n", 6)));

        QVERIFY(ctrl_->startAutoScan(params({dataRoot()})));
        waitUntil([this] { return autoFired_; });

        QVERIFY(autoFired_);
        QCOMPARE(lastAuto_.newFiles, 2);    // pdf + png; txt gated out
        QCOMPARE(lastAuto_.updatedFiles, 0);
        QCOMPARE(scalar("SELECT COUNT(*) FROM Files;"), 2);
        QCOMPARE(textOf("SELECT ocr_status FROM Files "
                        "WHERE extension='pdf';"), "pending");
        QCOMPARE(textOf("SELECT ocr_status FROM Files "
                        "WHERE extension='png';"), "pending");
        QCOMPARE(textOf("SELECT indexing_status FROM Files "
                        "WHERE extension='pdf';"), "metadata_only");
    }

    void autoScanPreservesQueuedRowsUntilContentChanges() {
        QVERIFY(makeFile("a.pdf", "%PDF-1.4 one"));
        QVERIFY(makeFile("b.pdf", "%PDF-1.4 two"));
        QVERIFY(ctrl_->startAutoScan(params({dataRoot()})));
        waitUntil([this] { return autoFired_; });

        // Simulate a pipeline mid-flight: queued + needs-OCR.
        QVERIFY(exec("UPDATE Files SET indexing_status='metadata_only' "
                     "WHERE extension='pdf';"));
        QVERIFY(exec("UPDATE Files SET ocr_status='needs_ocr' "
                     "WHERE filename='b.pdf';"));

        autoFired_ = false;
        QVERIFY(ctrl_->startAutoScan(params({dataRoot()})));
        waitUntil([this] { return autoFired_; });
        QCOMPARE(lastAuto_.updatedFiles, 0);   // nothing changed on disk
        // Queued rows must survive an unchanged scan untouched
        // (the v1.7.3 "scan silently completed everything" bug).
        QCOMPARE(textOf("SELECT indexing_status FROM Files "
                        "WHERE filename='a.pdf';"), "metadata_only");
        QCOMPARE(textOf("SELECT ocr_status FROM Files "
                        "WHERE filename='b.pdf';"), "needs_ocr");

        // Now ACTUALLY change one file: only that one re-queues.
        QVERIFY(changeFile("a.pdf", " + appended content"));
        autoFired_ = false;
        QVERIFY(ctrl_->startAutoScan(params({dataRoot()})));
        waitUntil([this] { return autoFired_; });
        QCOMPARE(lastAuto_.updatedFiles, 1);
        QCOMPARE(textOf("SELECT indexing_status FROM Files "
                        "WHERE filename='a.pdf';"), "metadata_only");
        // ...and the untouched file keeps its state.
        QCOMPARE(textOf("SELECT ocr_status FROM Files "
                        "WHERE filename='b.pdf';"), "needs_ocr");
    }

    void autoScanPrunesGoneFilesIncludingChunks() {
        QVERIFY(makeFile("gone.pdf", "%PDF-1.4 vanished"));
        QVERIFY(ctrl_->startAutoScan(params({dataRoot()})));
        waitUntil([this] { return autoFired_; });
        const QString id = textOf("SELECT id FROM Files "
                                  "WHERE filename='gone.pdf';");
        QVERIFY(!id.isEmpty());
        // A leftover chunk row (e.g. from a completed AI embedding).
        QVERIFY(exec("INSERT INTO EmbeddingChunks (file_id, chunk_index, "
                     "embedding) VALUES (" + id + ", 0, x'00');"));

        QVERIFY(QFile::remove(dataRoot() + "/gone.pdf"));
        autoFired_ = false;
        QVERIFY(ctrl_->startAutoScan(params({dataRoot()})));
        waitUntil([this] { return autoFired_; });

        QCOMPARE(lastAuto_.removedFiles, 1);
        QCOMPARE(scalar("SELECT COUNT(*) FROM Files "
                        "WHERE filename='gone.pdf';"), 0);
        // v1.7.24 fix: the prune must also remove EmbeddingChunks —
        // the old pass deleted only Files/SearchIndex/BgeEmbeddings.
        QCOMPARE(scalar("SELECT COUNT(*) FROM EmbeddingChunks "
                        "WHERE file_id=" + id + ";"), 0);
    }

    void autoScanSkipsUnavailableFoldersWithoutPurging() {
        QVERIFY(makeFile("a.pdf", "%PDF-1.4 stay"));
        const QString offline = dataRoot() + "_offline";  // never created
        QVERIFY(ctrl_->startAutoScan(params({dataRoot(), offline})));
        waitUntil([this] { return autoFired_; });

        QCOMPARE(lastAuto_.unavailable, 1);
        // The live folder's rows survive; nothing was pruned.
        QCOMPARE(scalar("SELECT COUNT(*) FROM Files;"), 1);
    }

    void autoScanHonorsUserExcludedExtensions() {
        QVERIFY(makeFile("a.pdf", "%PDF-1.4 keep"));
        QVERIFY(makeFile("b.png", QByteArray("\x89PNG\r\n", 6)));
        QVERIFY(ctrl_->startAutoScan(params({dataRoot()})));
        waitUntil([this] { return autoFired_; });
        QCOMPARE(scalar("SELECT COUNT(*) FROM Files;"), 2);

        // Exclude png: the walk never sees it, so Pass-2 prunes it.
        autoFired_ = false;
        QVERIFY(ctrl_->startAutoScan(
            params({dataRoot()}, false, {"png"})));
        waitUntil([this] { return autoFired_; });
        QCOMPARE(scalar("SELECT COUNT(*) FROM Files "
                        "WHERE extension='png';"), 0);
        QCOMPARE(scalar("SELECT COUNT(*) FROM Files "
                        "WHERE extension='pdf';"), 1);
    }

    void autoScanSingleFlightAndWatchdogWindow() {
        QVERIFY(makeFile("a.pdf", "%PDF-1.4 x"));
        QVERIFY(ctrl_->startAutoScan(params({dataRoot()})));
        // A second start while the first is healthy must be refused.
        QVERIFY(!ctrl_->startAutoScan(params({dataRoot()})));
        waitUntil([this] { return !ctrl_->isAutoScanRunning(); });
        QVERIFY(!ctrl_->isAutoScanRunning());
    }

    // ---- startup integrity pass ------------------------------------

    void integrityPassRequeuesFakeDoneAndFailedRows() {
        QVERIFY(exec(
            "INSERT INTO Files (path, filename, extension, size, "
            "  indexing_status, ocr_status) VALUES "
            "  ('X:\\fake\\doc.pdf', 'doc.pdf', 'pdf', 10, "
            "   'content_done', 'not_needed'),"
            "  ('X:\\fake\\scan.jpg', 'scan.jpg', 'jpg', 10, "
            "   'failed', 'failed');"));

        QVERIFY(ctrl_->isIntegrityRunning() == false);
        ctrl_->startIntegrityPass(params({}), false);
        waitUntil([this] { return integFired_; });

        QVERIFY(integFired_);
        QCOMPARE(lastInteg_.requeued, 1);        // fake-done pdf
        QCOMPARE(lastInteg_.failedRequeued, 1);  // failed jpg retry
        QCOMPARE(lastInteg_.junkAuditScanned, false);  // flag was false
        QCOMPARE(textOf("SELECT indexing_status FROM Files "
                        "WHERE filename='doc.pdf';"), "metadata_only");
        QCOMPARE(textOf("SELECT indexing_status FROM Files "
                        "WHERE filename='scan.jpg';"), "metadata_only");
        QCOMPARE(textOf("SELECT ocr_status FROM Files "
                        "WHERE filename='scan.jpg';"), "pending");
    }

    void integrityPassBackfillsMissingHashes() {
        QVERIFY(exec(
            "INSERT INTO Files (path, filename, extension, size, "
            "  indexing_status, ocr_status, hash) VALUES "
            "  ('X:\\fake\\doc.pdf', 'doc.pdf', 'pdf', 10, "
            "   'content_done', 'not_needed', '');"));
        // hashEnabled=false: the backfill is skipped entirely.
        ctrl_->startIntegrityPass(params({}, false), false);
        waitUntil([this] { return integFired_; });
        QCOMPARE(lastInteg_.hashed, 0);

        // Point the row at a REAL file and enable hashing.
        QVERIFY(makeFile("h.pdf", "%PDF-1.4 hashable"));
        QVERIFY(exec("UPDATE Files SET path='" + dataRoot() +
                     "/h.pdf', filename='h.pdf' "
                     "WHERE filename='doc.pdf';"));
        integFired_ = false;
        ctrl_->startIntegrityPass(params({}, true), false);
        waitUntil([this] { return integFired_; });
        QCOMPARE(lastInteg_.hashed, 1);
        QCOMPARE(textOf("SELECT length(hash) FROM Files "
                        "WHERE filename='h.pdf';"), 64);
    }

    void integrityJunkAuditRunsExactlyOnce() {
        // No DocumentText rows at all: the audit still SCANS (the
        // flag contract), finds nothing, and the caller persists.
        ctrl_->startIntegrityPass(params({}), true);
        waitUntil([this] { return integFired_; });
        QVERIFY(integFired_);
        QCOMPARE(lastInteg_.junkAuditScanned, true);
        QCOMPARE(lastInteg_.junkRequeued, 0);

        // The caller persisted the flag: the next pass must NOT scan.
        integFired_ = false;
        ctrl_->startIntegrityPass(params({}), false);
        waitUntil([this] { return integFired_; });
        QCOMPARE(lastInteg_.junkAuditScanned, false);
    }

    // ---- folder ingest scan ----------------------------------------

    void folderScanUpsertsHashesAndPreservesExistingHashes() {
        QVERIFY(makeFile("a.pdf", "%PDF-1.4 unique content"));
        QVERIFY(makeFile("b.txt", "excluded"));
        ctrl_->startFolderScan(dataRoot(), params({dataRoot()}, true));
        waitUntil([this] { return fsFired_; });

        QVERIFY(fsFired_);
        QCOMPARE(fsIndexed_, 1);
        QCOMPARE(fsSkipped_, 1);
        QCOMPARE(fsHashed_, 1);
        QCOMPARE(textOf("SELECT length(hash) FROM Files "
                        "WHERE filename='a.pdf';"), 64);

        // Second run: same count (ON CONFLICT upsert, no dup rows).
        fsFired_ = false;
        ctrl_->startFolderScan(dataRoot(), params({dataRoot()}, true));
        waitUntil([this] { return fsFired_; });
        QCOMPARE(fsIndexed_, 1);
        QCOMPARE(scalar("SELECT COUNT(*) FROM Files;"), 1);
    }

    void folderScanQueueRunsEveryRequest() {
        QVERIFY(makeFile("q1.pdf", "%PDF-1.4 q1"));
        QVERIFY(makeFile("q2/q2.pdf", "%PDF-1.4 q2"));
        // The requests land back-to-back; whether the second is QUEUED
        // (first scan still walking) or STARTED immediately (first done
        // — a tiny folder can finish that fast) is timing-dependent and
        // deliberately not asserted. The CONTRACT is: every request is
        // eventually executed exactly once, in order.
        ctrl_->startFolderScan(dataRoot(), params({dataRoot()}));
        ctrl_->startFolderScan(dataRoot() + "/q2",
                               params({dataRoot() + "/q2"}));
        waitUntil([&] {
            return scalar("SELECT COUNT(*) FROM Files;") == 2;
        });
        QCOMPARE(scalar("SELECT COUNT(*) FROM Files;"), 2);
        // Exactly-once: one row per file — no scan re-ran another's
        // work into duplicates.
        QCOMPARE(scalar("SELECT COUNT(*) FROM Files "
                        "WHERE filename='q1.pdf';"), 1);
        QCOMPARE(scalar("SELECT COUNT(*) FROM Files "
                        "WHERE filename='q2.pdf';"), 1);
    }

    // ---- static purges ----------------------------------------------

    void purgeFolderCascadeRemovesEveryFootprint() {
        QVERIFY(exec(
            "INSERT INTO Files (path, filename, extension, size, "
            "  indexing_status) VALUES "
            "  ('D:\\Docs\\a.pdf', 'a.pdf', 'pdf', 1, 'content_done'),"
            "  ('D:\\Docs\\sub\\b.pdf', 'b.pdf', 'pdf', 1, "
            "   'content_done'),"
            "  ('D:\\Other\\c.pdf', 'c.pdf', 'pdf', 1, 'content_done');"));
        QVERIFY(exec("INSERT INTO EmbeddingChunks (file_id, chunk_index, "
                     "embedding) SELECT id, 0, x'00' FROM Files "
                     "WHERE path LIKE 'D:\\Docs%';"));
        QCOMPARE(scalar("SELECT COUNT(*) FROM EmbeddingChunks;"), 2);

        const qint64 purged = ScanPipelineController::purgeFolderFromIndex(
            *db_, "D:\\Docs");
        QCOMPARE(purged, 2);
        QCOMPARE(scalar("SELECT COUNT(*) FROM Files "
                        "WHERE path LIKE 'D:\\Docs%';"), 0);
        QCOMPARE(scalar("SELECT COUNT(*) FROM EmbeddingChunks;"), 0);
        QCOMPARE(scalar("SELECT COUNT(*) FROM Files;"), 1);  // Other/c.pdf
    }

    void purgeNonIndexableRemovesLegacyRows() {
        QVERIFY(exec(
            "INSERT INTO Files (path, filename, extension, size) VALUES "
            "  ('D:\\x\\readme.md', 'readme.md', 'md', 1),"
            "  ('D:\\x\\ok.pdf', 'ok.pdf', 'pdf', 1);"));
        const int purged = ScanPipelineController::purgeNonIndexableRows(
            *db_);
        QCOMPARE(purged, 1);
        QCOMPARE(scalar("SELECT COUNT(*) FROM Files;"), 1);
    }
};

QTEST_GUILESS_MAIN(tst_ScanPipeline)
#include "tst_ScanPipeline.moc"
