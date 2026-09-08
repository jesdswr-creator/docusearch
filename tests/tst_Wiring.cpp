// ============================================================
// tst_Wiring.cpp — WIRING tests for the pipeline controllers
// ============================================================
//
// Answers the review that motivated v1.7.21:
//   "UI and async pipelines are untested — the exact place two
//    serious bugs once hid (OCR pool declared but never
//    constructed; extraction never wired). Logic tests can't
//    catch wiring bugs."
//
// These are NOT logic tests. They construct the REAL controllers
// headless (QtCore only, a temp sqlite database per test) and
// assert the WIRING contract:
//   • everything the controller declares is eagerly CONSTRUCTED
//     (verifyWiring) — the "declared but never constructed" class;
//   • an unwired worker is DETECTED, never silently idle;
//   • driving the real pipeline produces the real DB writes and
//     the real signal sequence (QSignalSpy) — the "never wired"
//     class;
//   • regressions for two REAL bugs this suite's design is built
//     around:
//       - the v1.7.20 busy-wait re-entrancy bug (a file extracted
//         again by a re-entering tick — each file must be
//         extracted EXACTLY ONCE);
//       - the v1.7.20 stale-capture bug (the watcher continuation
//         captured session 1's state, so every later session's
//         results were dropped and the tick re-extracted todo[0]
//         forever with no DB writes).
//
// No QtWidgets, no ONNX, no OCR engine: the embedding backfill runs
// against a FakeEmbeddingService through IEmbeddingService, and
// extraction runs against an injected deterministic worker function.
// ============================================================

#include "../src/core/ExtractionController.h"
#include "../src/core/Constants.h"
#include "../src/embeddings/EmbeddingController.h"
#include "../src/embeddings/IEmbeddingService.h"
#include "../src/database/Database.h"
#include "../src/database/Schema.h"
#include "../src/database/FileRepository.h"

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSharedPointer>
#include <QThreadPool>
#include <sqlite3.h>

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

using namespace DocuSearch;

// Shorthands: the status strings live in inner NAMESPACES, so these
// are namespace aliases (a 'using X = namespace-name' type alias is
// ill-formed - CI-verified).
namespace IS = DocuSearch::Constants::IndexingStatus;
namespace OS = DocuSearch::Constants::OcrStatus;

// 'needs_ocr' is a raw SQL literal everywhere in the pipeline (the
// extractors write it directly); it has no Constants entry.
static const char* kNeedsOcr = "needs_ocr";

// ============================================================
// Fakes
// ============================================================

// Extraction worker: deterministic, thread-safe, optionally slow
// (slower than the test tick interval — that is the point).
struct FakeWorker {
    std::mutex mtx;
    QStringList calls;                 // every invocation, in order
    int sleepMs = 0;                   // simulated extraction time
    bool succeed = true;

    ExtractionResult operator()(const QString& path, const QString& ext) {
        Q_UNUSED(ext);
        {
            std::lock_guard<std::mutex> g(mtx);
            calls.append(path);
        }
        if (sleepMs > 0) QTest::qSleep(sleepMs);
        ExtractionResult r;
        if (succeed) {
            r.text    = QString("text of %1").arg(QFileInfo(path).fileName());
            r.source  = QStringLiteral("test");
        } else {
            r.needsOcr = true;         // scanned page: no text, wants OCR
        }
        return r;
    }
    QStringList snapshot() {
        std::lock_guard<std::mutex> g(mtx);
        return calls;
    }
    void reset() {
        std::lock_guard<std::mutex> g(mtx);
        calls.clear();
    }
};

// Embedding service: records batches, optionally "stores" embeddings
// the way BgeService would (so the drain chain can actually finish).
class FakeEmbeddingService : public IEmbeddingService {
public:
    bool ready = true;
    bool storeOnBatch = true;          // false = every file fails to embed
    struct Batch { QVector<int> fileIds; QStringList texts; };
    QList<Batch> batches;
    Database* db = nullptr;

    bool isReady() const override { return ready; }
    void embedDocumentsBatch(const QVector<int>& fileIds,
                             const QStringList& texts) override {
        Q_UNUSED(texts);
        batches.append({fileIds, texts});
        if (storeOnBatch && db) {
            sqlite3* raw = db->raw();
            for (int fid : fileIds) {
                const float v[2] = {1.0f, 0.0f};
                sqlite3_stmt* st = nullptr;
                sqlite3_prepare_v2(raw,
                    "INSERT OR REPLACE INTO BgeEmbeddings "
                    "(file_id, embedding, updated_at, algo_version) "
                    "VALUES (?1, ?2, ?3, 1);",
                    -1, &st, nullptr);
                if (st) {
                    sqlite3_bind_int64(st, 1, fid);
                    sqlite3_bind_blob(st, 2, v, sizeof(v), SQLITE_TRANSIENT);
                    sqlite3_bind_int64(st, 3, QDateTime::currentSecsSinceEpoch());
                    sqlite3_step(st);
                    sqlite3_finalize(st);
                }
            }
        }
    }
};

// ============================================================
// Test fixture — a FRESH database per test (init/cleanup)
// ============================================================

class TestWiring : public QObject {
    Q_OBJECT

    QTemporaryDir dir_;
    int dbCounter_ = 0;
    std::unique_ptr<Database>       db_;
    std::unique_ptr<FileRepository> repo_;

    bool waitForSpy(QSignalSpy& spy, int n, int timeoutMs = 15000) {
        const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
        while (spy.count() < n && QDateTime::currentMSecsSinceEpoch() < deadline)
            QTest::qWait(10);
        return spy.count() >= n;
    }

    qint64 seedFile(const QString& name, const QString& ext,
                    const QString& status, const QString& ocrStatus,
                    qint64 size = 128) {
        const QString path = dir_.filePath(name);
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QByteArray(int(size), 'x'));
        }
        FileRecord r;
        r.path            = QDir::toNativeSeparators(path);
        r.filename        = name;
        r.extension       = ext;
        r.size            = size;
        r.createdDate     = QDateTime::currentDateTime();
        r.modifiedDate    = QDateTime::currentDateTime();
        r.indexingStatus  = status;
        r.ocrStatus       = ocrStatus;
        repo_->upsertFile(r);
        FileRecord out;
        if (!repo_->getByPath(r.path, out)) return 0;
        return out.id;
    }

    qint64 fileRowStatus(qint64 fileId, QString* statusOut) {
        sqlite3* raw = db_->raw();
        sqlite3_stmt* s = nullptr;
        qint64 rows = 0;
        if (sqlite3_prepare_v2(raw,
                "SELECT indexing_status FROM Files WHERE id=?1;",
                -1, &s, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(s, 1, fileId);
            if (sqlite3_step(s) == SQLITE_ROW) {
                ++rows;
                if (statusOut) {
                    const unsigned char* t = sqlite3_column_text(s, 0);
                    *statusOut = t ? QString::fromUtf8(reinterpret_cast<const char*>(t)) : QString();
                }
            }
            sqlite3_finalize(s);
        }
        return rows;
    }

    bool documentTextEquals(qint64 fileId, const QString& expected) {
        sqlite3* raw = db_->raw();
        sqlite3_stmt* s = nullptr;
        bool ok = false;
        if (sqlite3_prepare_v2(raw,
                "SELECT extracted_text FROM DocumentText WHERE file_id=?1;",
                -1, &s, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(s, 1, fileId);
            if (sqlite3_step(s) == SQLITE_ROW) {
                const unsigned char* t = sqlite3_column_text(s, 0);
                ok = t && expected == QString::fromUtf8(reinterpret_cast<const char*>(t));
            }
            sqlite3_finalize(s);
        }
        return ok;
    }

    int documentTextRows(qint64 fileId) {
        sqlite3* raw = db_->raw();
        sqlite3_stmt* s = nullptr;
        int rows = 0;
        if (sqlite3_prepare_v2(raw,
                "SELECT COUNT(*) FROM DocumentText WHERE file_id=?1;",
                -1, &s, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(s, 1, fileId);
            if (sqlite3_step(s) == SQLITE_ROW)
                rows = sqlite3_column_int(s, 0);
            sqlite3_finalize(s);
        }
        return rows;
    }

    void seedDocumentText(qint64 fileId, const QString& text) {
        sqlite3_exec(db_->raw(),
            QString("INSERT OR REPLACE INTO DocumentText "
                    "(file_id, extracted_text, text_source, char_count, updated_at) "
                    "VALUES (%1, '%2', 'native', %3, 100);")
                .arg(fileId).arg(text).arg(text.size()).toUtf8().constData(),
            nullptr, nullptr, nullptr);
    }

    // Builds the todo list for the given file ids (path/extension come
    // from the repository rows). Q_ASSERT (not QVERIFY): this is a
    // non-void helper, so the test macros cannot return from it.
    QList<ExtractionController::ExtractionTodo> todosFor(
            std::initializer_list<qint64> ids) {
        QList<ExtractionController::ExtractionTodo> todo;
        for (qint64 id : ids) {
            FileRecord rec;
            Q_ASSERT(repo_->getById(id, rec));
            todo.append({id, rec.path, rec.extension});
        }
        return todo;
    }

private slots:
    void initTestCase() {
        QVERIFY(dir_.isValid());
    }

    // FRESH database before EVERY test — no cross-test coupling in
    // what the backfill/scan queries select.
    void init() {
        db_ = std::make_unique<Database>();
        QString err;
        const QString path = dir_.filePath(QString("wiring%1.db").arg(++dbCounter_));
        QVERIFY2(db_->open(path, &err), qPrintable(err));
        Schema::initialize(*db_);
        repo_ = std::make_unique<FileRepository>(*db_);
    }

    void cleanup() {
        repo_.reset();
        db_.reset();
    }

    // --------------------------------------------------------
    // 1. CONSTRUCTION CONTRACT — "declared but never constructed"
    // --------------------------------------------------------
    void wiringContractEagerConstruction() {
        ExtractionController ex;
        ex.setDatabase(db_.get());
        ex.setWorkerFn([](const QString&, const QString&) {
            ExtractionResult r; return r;
        });
        QVERIFY(ex.verifyWiring());
        QVERIFY(ex.extractionPool() != nullptr);
        QCOMPARE(ex.extractionPool()->maxThreadCount(), 1);
        // 16 MB stacks — Poppler recursion guard
        QCOMPARE(int(ex.extractionPool()->stackSize()), 16 * 1024 * 1024);
        QVERIFY(!ex.isRunning());

        EmbeddingController em;
        em.setDatabase(db_.get());
        QVERIFY(em.verifyWiring());
        QVERIFY(em.embeddingPool() != nullptr);
        QCOMPARE(em.embeddingPool()->maxThreadCount(), 1);
    }

    // An unwired worker must be DETECTED (verifyWiring false) and the
    // pipeline must fail loudly — never spin silently forever.
    void unwiredWorkerIsDetected() {
        ExtractionController ex;             // NO setWorkerFn call
        ex.setDatabase(db_.get());
        ex.setTickIntervalMs(20);
        QVERIFY(!ex.verifyWiring());         // the audit catches it

        const qint64 id = seedFile("unwired.pdf", "pdf",
                                   IS::kMetadataOnly,
                                   OS::kNotNeeded);
        QVERIFY(id > 0);

        QSignalSpy finishedSpy(&ex, &ExtractionController::sessionFinished);
        ex.startFromDatabase();
        QVERIFY(waitForSpy(finishedSpy, 1));
        QCOMPARE(finishedSpy.at(0).at(0).toInt(), 0);   // done == 0
        QCOMPARE(finishedSpy.at(0).at(1).toInt(), 1);   // failed == 1
        QString st;
        QCOMPARE(fileRowStatus(id, &st), (qint64)1);
        QCOMPARE(st, QString(IS::kFailed));      // marked failed, not lost
    }

    // --------------------------------------------------------
    // 2. GATHER SQL — the pure selection split
    // --------------------------------------------------------
    void gatherTodoItemsSplitsTextAndOcrWork() {
        const qint64 txtPdf = seedFile("doc1.pdf", "pdf",
                                       IS::kMetadataOnly, OS::kNotNeeded);
        const qint64 ocrPdf = seedFile("scan1.pdf", "pdf",
                                       kNeedsOcr,   OS::kPending);
        const qint64 img    = seedFile("pic1.png", "png",
                                       IS::kMetadataOnly, OS::kPending);
        const qint64 txt    = seedFile("note.txt", "txt",
                                       IS::kMetadataOnly, OS::kNotNeeded);
        const qint64 done   = seedFile("done.pdf", "pdf",
                                       IS::kContentDone, OS::kNotNeeded);

        const auto lists = ExtractionController::gatherTodoItems(db_->raw());

        QVector<qint64> textIds, ocrIds;
        for (const auto& t : lists.text) textIds.append(t.fileId);
        for (const auto& t : lists.ocr)  ocrIds.append(t.fileId);

        QVERIFY(textIds.contains(txtPdf));   // metadata pdf -> text pipeline
        QVERIFY(ocrIds.contains(ocrPdf));    // needs_ocr pdf -> OCR pool
        QVERIFY(ocrIds.contains(img));       // pending image -> OCR pool
        QVERIFY(!textIds.contains(txt));     // txt retired...
        QVERIFY(!ocrIds.contains(txt));
        QString st;
        fileRowStatus(txt, &st);
        QCOMPARE(st, QString(IS::kSkipped));   // ...to 'skipped'
        QVERIFY(!textIds.contains(ocrPdf));
        QVERIFY(!textIds.contains(done));
        QVERIFY(!ocrIds.contains(done));
        QVERIFY(!textIds.contains(img));
    }

    // --------------------------------------------------------
    // 3. SESSION: once-only extraction (re-entrancy regression)
    // --------------------------------------------------------
    void sessionExtractsEachFileExactlyOnce() {
        const qint64 f1 = seedFile("a1.pdf", "pdf", IS::kMetadataOnly, OS::kNotNeeded);
        const qint64 f2 = seedFile("a2.pdf", "pdf", IS::kMetadataOnly, OS::kNotNeeded);
        const qint64 f3 = seedFile("a3.pdf", "pdf", IS::kMetadataOnly, OS::kNotNeeded);

        auto worker = QSharedPointer<FakeWorker>::create();
        worker->sleepMs = 80;             // SLOWER than the 20 ms tick

        ExtractionController ex;
        ex.setDatabase(db_.get());
        const auto w = worker;
        ex.setWorkerFn([w](const QString& p, const QString& e) { return (*w)(p, e); });
        ex.setTickIntervalMs(20);

        QSignalSpy runningSpy(&ex, &ExtractionController::extractingChanged);
        QSignalSpy finishedSpy(&ex, &ExtractionController::sessionFinished);
        QSignalSpy statsSpy(&ex, &ExtractionController::statsDirty);

        ex.startSession(todosFor({f1, f2, f3}), {});
        QVERIFY(ex.isRunning());

        QVERIFY(waitForSpy(finishedSpy, 1));
        QCOMPARE(finishedSpy.at(0).at(0).toInt(), 3);     // done
        QCOMPARE(finishedSpy.at(0).at(1).toInt(), 0);     // failed
        QCOMPARE(finishedSpy.at(0).at(3).toBool(), true); // completedAll
        QVERIFY(!ex.isRunning());

        // THE CONTRACT: every file extracted EXACTLY once.
        const QStringList calls = worker->snapshot();
        QCOMPARE(calls.size(), 3);
        QSet<QString> unique;
        for (const QString& c : calls) unique.insert(c);
        QCOMPARE(unique.size(), 3);

        // DB accounting actually happened (the "never wired" class).
        QVERIFY(documentTextEquals(f1, "text of a1.pdf"));
        QVERIFY(documentTextEquals(f2, "text of a2.pdf"));
        QVERIFY(documentTextEquals(f3, "text of a3.pdf"));
        QString st;
        fileRowStatus(f1, &st);
        QCOMPARE(st, QString(IS::kContentDone));
        QVERIFY(statsSpy.count() >= 3);

        // Signal sequence: on at start, off at end.
        QVERIFY(runningSpy.count() >= 2);
        QCOMPARE(runningSpy.at(0).at(0).toBool(), true);
        QCOMPARE(runningSpy.at(runningSpy.count() - 1).at(0).toBool(), false);
    }

    // --------------------------------------------------------
    // 4. SECOND SESSION (the v1.7.20 stale-capture regression).
    //    The old continuation captured session 1's state — every
    //    later session's results were dropped and the tick spun on
    //    todo[0] forever. This test FAILS on that code.
    // --------------------------------------------------------
    void secondSessionIsFullyAccounted() {
        const qint64 f1 = seedFile("b1.pdf", "pdf", IS::kMetadataOnly, OS::kNotNeeded);
        const qint64 f2 = seedFile("b2.pdf", "pdf", IS::kMetadataOnly, OS::kNotNeeded);
        const qint64 f3 = seedFile("b3.pdf", "pdf", IS::kMetadataOnly, OS::kNotNeeded);

        auto worker = QSharedPointer<FakeWorker>::create();

        ExtractionController ex;
        ex.setDatabase(db_.get());
        const auto w = worker;
        ex.setWorkerFn([w](const QString& p, const QString& e) { return (*w)(p, e); });
        ex.setTickIntervalMs(20);

        QSignalSpy finishedSpy(&ex, &ExtractionController::sessionFinished);

        // Session A: one file, must complete.
        ex.startSession(todosFor({f1}), {});
        QVERIFY(waitForSpy(finishedSpy, 1));
        QCOMPARE(finishedSpy.at(0).at(0).toInt(), 1);
        QVERIFY(documentTextEquals(f1, "text of b1.pdf"));

        // Session B: two NEW files — results MUST be accounted.
        finishedSpy.clear();
        worker->reset();
        ex.startSession(todosFor({f2, f3}), {});
        QVERIFY(waitForSpy(finishedSpy, 1));
        QCOMPARE(finishedSpy.at(0).at(0).toInt(), 2);      // done == 2
        QVERIFY(!ex.isRunning());
        const QStringList calls = worker->snapshot();
        QCOMPARE(calls.size(), 2);                          // exactly once each
        QVERIFY(documentTextEquals(f2, "text of b2.pdf"));  // DB writes happened
        QVERIFY(documentTextEquals(f3, "text of b3.pdf"));
    }

    // --------------------------------------------------------
    // 5. CANCEL: session-gen drops the in-flight result
    // --------------------------------------------------------
    void cancelMidSessionDropsLateResults() {
        const qint64 f1 = seedFile("c1.pdf", "pdf", IS::kMetadataOnly, OS::kNotNeeded);
        const qint64 f2 = seedFile("c2.pdf", "pdf", IS::kMetadataOnly, OS::kNotNeeded);

        auto worker = QSharedPointer<FakeWorker>::create();
        worker->sleepMs = 250;

        ExtractionController ex;
        ex.setDatabase(db_.get());
        const auto w = worker;
        ex.setWorkerFn([w](const QString& p, const QString& e) { return (*w)(p, e); });
        ex.setTickIntervalMs(20);

        QSignalSpy finishedSpy(&ex, &ExtractionController::sessionFinished);
        QSignalSpy runningSpy(&ex, &ExtractionController::extractingChanged);

        ex.startSession(todosFor({f1, f2}), {});

        // Wait for file 1 to complete, then cancel while file 2 runs.
        QTRY_VERIFY(documentTextEquals(f1, "text of c1.pdf"));
        QVERIFY(ex.isRunning());
        ex.cancelSession();

        QVERIFY(waitForSpy(finishedSpy, 1));
        QCOMPARE(finishedSpy.at(0).at(3).toBool(), false);  // not completedAll
        QCOMPARE(runningSpy.at(runningSpy.count() - 1).at(0).toBool(), false);

        // The in-flight file 2 lands AFTER the cancel: its result must
        // write NOTHING (session generation bumped before flags dropped).
        QTest::qWait(600);   // let the pool thread finish + continuation fire
        QString st;
        fileRowStatus(f2, &st);
        QVERIFY(st != QString(IS::kContentDone));
        QCOMPARE(documentTextRows(f2), 0);
    }

    // --------------------------------------------------------
    // 6. DB RESET: late OCR results must not write
    // --------------------------------------------------------
    void dbResetDropsLateOcrResults() {
        const qint64 f1 = seedFile("d1.png", "png", kNeedsOcr, OS::kPending);

        ExtractionController ex;
        ex.setDatabase(db_.get());
        ex.setWorkerFn([](const QString&, const QString&) {
            ExtractionResult r; return r;
        });
        ex.setDbResetting(true);
        ex.noteOcrResult(f1, "should not be written", true);
        QCOMPARE(documentTextRows(f1), 0);
    }

    // --------------------------------------------------------
    // 7. OCR session accounting ends the session + re-arms
    // --------------------------------------------------------
    void ocrAccountingEndsSession() {
        const qint64 f1 = seedFile("e1.png", "png", kNeedsOcr, OS::kPending);
        const qint64 f2 = seedFile("e2.png", "png", kNeedsOcr, OS::kPending);

        ExtractionController ex;
        ex.setDatabase(db_.get());
        ex.setWorkerFn([](const QString&, const QString&) {
            ExtractionResult r; return r;
        });
        ex.setTickIntervalMs(20);

        QSignalSpy runningSpy(&ex, &ExtractionController::extractingChanged);
        QSignalSpy rearmSpy(&ex, &ExtractionController::autoRearmRequested);
        QSignalSpy progressSpy(&ex, &ExtractionController::progressUpdated);

        ex.startSession({}, {});      // OCR-only session (text todo empty)
        ex.noteOcrQueued(2);
        QVERIFY(ex.ocrWorkOutstanding());

        ex.noteOcrResult(f1, "ocr text one", true);
        QVERIFY(ex.isRunning());      // still one result outstanding
        ex.noteOcrResult(f2, "ocr text two", true);
        QVERIFY(!ex.ocrWorkOutstanding());
        QVERIFY(!ex.isRunning());     // session ended with the last result

        QVERIFY(documentTextEquals(f1, "ocr text one"));
        QVERIFY(documentTextEquals(f2, "ocr text two"));
        QVERIFY(progressSpy.count() >= 2);
        QVERIFY(rearmSpy.count() >= 1);                        // auto re-arm wake
        QCOMPARE(rearmSpy.at(0).at(0).toInt(), 1500);
        QCOMPARE(runningSpy.at(runningSpy.count() - 1).at(0).toBool(), false);
    }

    // --------------------------------------------------------
    // 8. BACKFILL: drain, chain, complete (via the interface seam)
    // --------------------------------------------------------
    void backfillDrainsBacklog() {
        const qint64 f1 = seedFile("g1.pdf", "pdf", IS::kContentDone, OS::kNotNeeded);
        const qint64 f2 = seedFile("g2.pdf", "pdf", IS::kContentDone, OS::kNotNeeded);
        const qint64 f3 = seedFile("g3.pdf", "pdf", IS::kContentDone, OS::kNotNeeded);
        for (qint64 id : {f1, f2, f3}) seedDocumentText(id, "body text");

        FakeEmbeddingService fake;
        fake.db = db_.get();

        EmbeddingController em;
        em.setDatabase(db_.get());
        em.attachService(&fake);
        QVERIFY(em.verifyWiring());

        QSignalSpy chipSpy(&em, &EmbeddingController::chipChanged);
        em.ensureBackfill();

        QCOMPARE(fake.batches.size(), 1);
        QCOMPARE(fake.batches.at(0).fileIds.size(), 3);
        QCOMPARE(em.countMissingEmbeddings(), (qint64)3);

        // The service reports the batch done — the drain is complete.
        em.noteEmbeddingFinished(3, 0);
        QCOMPARE(em.countMissingEmbeddings(), (qint64)0);
        QCOMPARE(em.isBackfillRunning(), false);
        QVERIFY(chipSpy.count() >= 1);
        QCOMPARE(chipSpy.at(chipSpy.count() - 1).at(0).toString(), QString("OFF"));
    }

    // --------------------------------------------------------
    // 9. BACKFILL: the all-fail deadlock guard stops the chain
    // --------------------------------------------------------
    void backfillDeadlockGuardStopsAfterTwoAllFailBatches() {
        const qint64 f1 = seedFile("h1.pdf", "pdf", IS::kContentDone, OS::kNotNeeded);
        seedDocumentText(f1, "doomed text");

        FakeEmbeddingService fake;
        fake.db = db_.get();
        fake.storeOnBatch = false;     // every "embedding" fails to store

        EmbeddingController em;
        em.setDatabase(db_.get());
        em.attachService(&fake);

        QSignalSpy statusSpy(&em, &EmbeddingController::statusMessage);
        em.ensureBackfill();
        QCOMPARE(fake.batches.size(), 1);
        em.noteEmbeddingFinished(0, 1);        // batch 1: all failed
        QTest::qWait(120);                     // chain singleShot (25 ms)
        QCOMPARE(fake.batches.size(), 2);      // batch 2 re-queued (retry once)
        em.noteEmbeddingFinished(0, 1);        // batch 2: all failed again
        QTest::qWait(120);                     // chain must NOT re-queue
        QCOMPARE(fake.batches.size(), 2);      // paused — no batch 3
        bool paused = false;
        for (int i = 0; i < statusSpy.count(); ++i)
            if (statusSpy.at(i).at(0).toString().contains("paused"))
                paused = true;
        QVERIFY(paused);
    }

    // --------------------------------------------------------
    // 10. REBUILD: purge chain wipes rows, then re-embeds
    // --------------------------------------------------------
    void rebuildHandoverReachesBackfill() {
        const qint64 f1 = seedFile("q1.pdf", "pdf", IS::kContentDone, OS::kNotNeeded);
        seedDocumentText(f1, "rebuildable body");
        {
            const float v[2] = {0.5f, 0.5f};
            sqlite3* raw = db_->raw();
            sqlite3_stmt* st = nullptr;
            sqlite3_prepare_v2(raw,
                "INSERT INTO BgeEmbeddings (file_id, embedding, updated_at, algo_version) "
                "VALUES (?1, ?2, 1, 0);",
                -1, &st, nullptr);
            sqlite3_bind_int64(st, 1, f1);
            sqlite3_bind_blob(st, 2, v, sizeof(v), SQLITE_TRANSIENT);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }

        FakeEmbeddingService fake;
        fake.db = db_.get();

        EmbeddingController em;
        em.setDatabase(db_.get());
        em.attachService(&fake);

        QSignalSpy blockedSpy(&em, &EmbeddingController::rebuildBlocked);
        em.startRebuild();
        QCOMPARE(blockedSpy.count(), 0);

        // Purge chain (25 ms ticks) empties the tables, then hands over
        // to the backfill — the fake's first batch is the proof.
        const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 15000;
        while (fake.batches.isEmpty() &&
               QDateTime::currentMSecsSinceEpoch() < deadline)
            QTest::qWait(10);
        QVERIFY(!fake.batches.isEmpty());      // re-embed started
        QCOMPARE(fake.batches.last().fileIds.size(), 1);
        QCOMPARE(fake.batches.last().fileIds.at(0), int(f1));
        em.noteEmbeddingFinished(fake.batches.last().fileIds.size(), 0);
        QCOMPARE(em.countMissingEmbeddings(), (qint64)0);
    }

    // --------------------------------------------------------
    // 11. STATS QUERIES (pure SQL)
    // --------------------------------------------------------
    void statsQueriesCountMissing() {
        const qint64 fresh   = seedFile("s1.pdf", "pdf", IS::kContentDone, OS::kNotNeeded);
        const qint64 stale   = seedFile("s2.pdf", "pdf", IS::kContentDone, OS::kNotNeeded);
        const qint64 noVec   = seedFile("s3.pdf", "pdf", IS::kContentDone, OS::kNotNeeded);
        const qint64 longDoc = seedFile("s4.pdf", "pdf", IS::kContentDone, OS::kNotNeeded);
        for (qint64 id : {fresh, stale, noVec}) seedDocumentText(id, "t");
        seedDocumentText(longDoc, QString(1100, 'w'));

        EmbeddingController em;
        em.setDatabase(db_.get());

        // 'fresh' gets a CURRENT-algo embedding (not missing any more).
        FakeEmbeddingService good;
        good.db = db_.get();
        em.attachService(&good);
        good.embedDocumentsBatch(QVector<int>{int(fresh)}, {"x"});

        QCOMPARE(em.countMissingEmbeddings(), (qint64)3);  // stale+noVec+longDoc

        // Give longDoc a current embedding: it drops out of the doc
        // count but appears in the CHUNK-missing count (>1000 chars).
        FakeEmbeddingService g2;
        g2.db = db_.get();
        em.attachService(&g2);
        g2.embedDocumentsBatch(QVector<int>{int(longDoc)}, {"y"});

        QCOMPARE(em.countMissingEmbeddings(), (qint64)2);  // stale+noVec
        QCOMPARE(em.countMissingChunkDocs(),  (qint64)1);  // longDoc, no chunks
    }

    void cleanupTestCase() {
        // nothing beyond the per-test cleanup
    }
};

QTEST_GUILESS_MAIN(TestWiring)
#include "tst_Wiring.moc"
