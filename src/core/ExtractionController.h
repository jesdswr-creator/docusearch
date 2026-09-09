#pragma once

// ============================================================
// ExtractionController.h - Headless owner of the content-extraction
// session state machine (v1.7.21).
// ============================================================
//
// WHY THIS EXISTS: for fourteen releases the extraction pipeline
// (session setup, the 200 ms driver tick, the per-file pool hand-off,
// the DB accounting continuation, the OCR queue accounting) lived as
// ~900 lines inside MainWindow.cpp — a 6.7k-line god-object. That is
// exactly where two historical bugs hid (the OCR pool declared but
// never constructed; extraction gathered by nobody), and neither
// could be caught by the logic test-suite because the logic tests
// never exercise WIRING. This class moves the whole pipeline into a
// QObject that:
//   * has ZERO QtWidgets dependencies (QtCore + sqlite only), so a
//     test can construct it headless against a temp database;
//   * constructs EVERY resource eagerly in its constructor (pool,
//     watcher) — the lazy "if (!pool) create it inside the handler"
//     pattern is what made a declared-but-never-constructed member
//     possible in the first place;
//   * exposes verifyWiring() — a runtime audit the app calls once at
//     startup and the wiring tests call directly;
//   * reports through SIGNALS instead of touching the UI, so a test
//     can observe the pipeline with QSignalSpy.
//
// THREADING (unchanged from v1.7.20): one file at a time on a
// dedicated single-thread QThreadPool with 16 MB stacks (Poppler's
// recursive parser blows 1 MB); results land via QFutureWatcher on
// the UI thread; a session generation counter drops late results
// from cancelled/reset sessions; the in-flight guard makes the tick
// a no-op while a file runs (the old busy-wait re-entrancy bug).
// ============================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QVector>
#include <QThreadPool>
#include <QFutureWatcher>
#include <QSharedPointer>
#include <atomic>
#include <functional>

#include "documents/IDocumentExtractor.h"   // ExtractionResult
#include "database/Database.h"
#include "DuplicateExtractionGuard.h"

class QTimer;
class QFutureWatcherBase;

namespace DocuSearch {

// NOTE: this forward declaration MUST live inside namespace DocuSearch.
// Declaring it at global scope makes every member/pointer declared
// before the real header bind to ::FileRepository (a direct declaration
// beats a using-directive in unqualified lookup) — the type is then
// permanently incomplete even after database/FileRepository.h is
// included (CI-verified: MSVC C2027 in both the app and the tests).
class FileRepository;

class ExtractionController : public QObject {
    Q_OBJECT
public:
    // One file to extract. (Was a function-local struct inside
    // MainWindow::onExtract — promoted so tests can build lists.)
    struct ExtractionTodo {
        qint64 fileId = 0;
        QString path;
        QString ext;
    };
    struct TodoLists {
        QList<ExtractionTodo> text;   // inline extraction pipeline
        QList<ExtractionTodo> ocr;    // needs_ocr + images (OCR pool)
    };

    // The extraction entry. The app injects the real registry call;
    // tests inject a deterministic fake. There is deliberately NO
    // default — a controller without a worker fn refuses to start
    // (qWarning) instead of silently doing nothing, because "the
    // pipeline was never wired" is precisely the bug class this
    // refactor exists to catch.
    using WorkerFn = std::function<ExtractionResult(const QString& path,
                                                    const QString& ext)>;
    // Settings-driven admissibility of a single file path (indexed
    // folders, excluded folders, excluded extensions). Injected by
    // MainWindow; the controller only adds the constant allowlist +
    // existence checks on top.
    using PathGate = std::function<bool(const QString& path)>;
    // OCR-pool hooks. The controller never includes OcrWorkerPool.h
    // (WinRT chain); MainWindow binds the pool through these. When
    // unset, OCR work is recorded as expected but never enqueued —
    // tests drive noteOcrQueued()/noteOcrResult() directly.
    using OcrEnqueueFn = std::function<int(const QList<ExtractionTodo>&)>;
    using OcrClearFn   = std::function<void()>;
    using OcrSizeFn    = std::function<int()>;

    explicit ExtractionController(QObject* parent = nullptr);
    ~ExtractionController() override;

    // ---- wiring --------------------------------------------------
    void setDatabase(Database* db)              { m_db = db; }
    void setFileRepository(FileRepository* r)   { m_repo = r; }
    void setWorkerFn(WorkerFn fn)               { m_workerFn = std::move(fn); }
    void setPathGate(PathGate gate)             { m_pathGate = std::move(gate); }
    void setOcrHooks(OcrEnqueueFn enqueue, OcrClearFn clear, OcrSizeFn size)
    {
        m_ocrEnqueue = std::move(enqueue);
        m_ocrClear   = std::move(clear);
        m_ocrSize    = std::move(size);
    }
    // Test seam: the 200 ms pacing between files (20 ms in tests).
    // Also updates a live session timer so degradation can slow/restore mid-run.
    void setTickIntervalMs(int ms);
    int  tickIntervalMs() const                 { return m_tickIntervalMs; }

    // Memory-pressure pause: in-flight file finishes, no new files start.
    void setPaused(bool paused)                 { m_paused.store(paused); }
    bool isPaused() const                       { return m_paused.load(); }

    // Throughput for the health dashboard (0 if no session has run).
    int filesPerMinute() const;

    // v1.7.10 first-run extract-all: 200-file sessions, 3 s re-arm,
    // until the first full drain completes (then firstRunDrainComplete()).
    void setFirstRunMode(bool on)               { m_firstRunMode = on; }
    bool firstRunMode() const                   { return m_firstRunMode; }
    // Database reset in progress (Settings -> Remove Database): every
    // late continuation / OCR result becomes a no-op.
    void setDbResetting(bool on)                { m_dbResetting = on; }

    // Eagerly-constructed resources (v1.7.21 contract — never lazy).
    QThreadPool* extractionPool() const         { return m_pool; }

    // Runtime audit: every declared resource is constructed and
    // configured. Called by MainWindow once at startup (DS_WARN on
    // failure) and asserted by tst_Wiring.
    bool verifyWiring() const;

    // ---- session control ------------------------------------------
    // Gather todo lists from the database and start (or, if a session
    // is already running, act as the Stop-Extracting toggle). This is
    // the Extract button's entry — and the auto-extract wake's entry.
    void startFromDatabase();
    // Direct start with caller-provided work (tests / future callers).
    void startSession(QList<ExtractionTodo> todo, QList<ExtractionTodo> ocrTodo);
    // User-facing cancel (Stop Extracting): invalidates the session,
    // clears the OCR queue, reports through signals.
    void cancelSession();
    // Silent teardown (database reset / destruction): bump the
    // generation so late continuations write nothing. No UI signals.
    void invalidateSession();
    // Destructor-grade shutdown: invalidate + drain the pool.
    void shutdownForTeardown();

    bool isRunning() const          { return m_running.load(); }
    bool ocrWorkOutstanding() const;
    QString extractionStatusString();

    // ---- OCR accounting -------------------------------------------
    // Called by MainWindow's OCR hook right after enqueueBatch (and by
    // tests directly). n = tasks queued for the current session.
    void noteOcrQueued(int n);
    // One OCR result landed. Writes DocumentText/SearchIndex/Files
    // exactly as the pre-split MainWindow::onOcrTaskCompleted did and
    // ends the session when the last expected result arrives.
    void noteOcrResult(qint64 fileId, const QString& text, bool ok);

    // ---- single-file pipeline (watcher add/modify) -----------------
    // Upserts the Files row, extracts content inline, refreshes the
    // FTS row, clears stale AI embeddings and wakes the backfill.
    // Returns false when the path is not admissible (gate, allowlist,
    // gone). The settings-driven gate is injected; see setPathGate.
    bool extractAndIndexFile(const QString& path);

    // Gathers the extraction todo (documents due for text extraction +
    // needs_ocr/images due for OCR) directly from the Files table.
    // Also retires excluded plain-text types to 'skipped' (they must
    // not linger as forever-pending metadata_only rows). Static so the
    // selection SQL is testable without a session.
    static TodoLists gatherTodoItems(sqlite3* raw);

signals:
    // statusBar()->showMessage(msg, timeoutMs)
    void statusMessage(const QString& msg, int timeoutMs = 0);
    // SearchBar::setExtracting(on) — the Extract button becomes
    // "Stop Extracting" while a session runs.
    void extractingChanged(bool on);
    // Progress bar (value, max, visible).
    void progressUpdated(int value, int max, bool visible);
    // The index-stat badges should refresh (after every file).
    void statsDirty();
    // The selected file's preview should re-render.
    void previewDirty();
    // The first full extraction drain finished — persist firstRunDone.
    void firstRunDrainComplete();
    // Newly extracted text exists — wake the AI embedding backfill.
    void backfillWakeRequested();
    // A batch boundary wants the next batch after delayMs (3 s first
    // run / 60 s steady state / 1.5 s after an OCR-only drain).
    void autoRearmRequested(int delayMs);
    // A session fully ended (completedAll = drained the whole todo,
    // not just the per-session cap).
    void sessionFinished(int done, int failed, int total, bool completedAll);

private:
    void startSessionInternal(QList<ExtractionTodo> todo,
                              QList<ExtractionTodo> ocrTodo);
    void ensurePool();

    // Per-session accumulator. Lives behind QSharedPointer so the
    // watcher continuation and the tick share it safely. The v1.7.20
    // code captured `state`/`total`/`maxFiles` INSIDE a one-time
    // connect() lambda — every session after the first hit the stale
    // captures, dropped every result (session-gen mismatch) and the
    // tick re-extracted todo[0] forever. Storing total/maxFiles IN the
    // state and reading the CURRENT m_session in the continuation is
    // the structural fix (regression-tested in tst_Wiring).
    struct SessionState {
        QList<ExtractionTodo> todo;
        int idx = 0;
        int done = 0;
        int failed = 0;
        int total = 0;
        int maxFiles = 0;
        ExtractionTodo current;
        QString  currentFileName;
        quint64  sessionGen = 0;
    };

    Database*        m_db   = nullptr;
    FileRepository*  m_repo = nullptr;

    // Eagerly constructed (verifyWiring audits these).
    QThreadPool*  m_pool    = nullptr;
    QFutureWatcher<ExtractionResult>* m_watcher = nullptr;

    WorkerFn    m_workerFn;
    PathGate    m_pathGate;
    OcrEnqueueFn m_ocrEnqueue;
    OcrClearFn  m_ocrClear;
    OcrSizeFn   m_ocrSize;

    // Session state (guarded to the UI thread — every mutation happens
    // in the tick / watcher continuation, both UI-thread deliveries).
    QTimer*     m_timer = nullptr;            // per-session driver tick
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_cancelFlag{false};
    bool        m_inFlight = false;           // one file on the pool
    quint64     m_sessionGen = 0;             // bumped: cancel/reset/dtor
    int         m_tickIntervalMs = 200;

    bool        m_firstRunMode = false;
    bool        m_dbResetting = false;        // database swap in progress
    std::atomic<bool> m_paused{false};

    DuplicateExtractionGuard m_dupGuard;
    qint64      m_sessionStartedMs = 0;
    std::atomic<int> m_filesCompleted{0};

    // OCR accounting for the current session.
    int         m_ocrExpected = 0;
    int         m_ocrReceived = 0;

    // Shared per-session accumulator (todo + counters + generation).
    QSharedPointer<SessionState> m_session;
};

} // namespace DocuSearch
