// ============================================================
// ExtractionController.cpp - the extraction session state machine
// ============================================================
// Ported from MainWindow::onExtract / onOcrTaskCompleted /
// extractAndIndexFile / getExtractionStatusString (v1.7.20) with two
// deliberate differences:
//   1. UI calls (statusBar, progress bar, Extract button, stats
//      badges) became signals — the controller is headless.
//   2. The QFutureWatcher continuation reads the CONTROLLER'S CURRENT
//      session instead of a captured one. The v1.7.20 code connected
//      the continuation ONCE (guarded by "if (!extractWatcher_)") and
//      captured the first session's state/total/maxFiles by value —
//      every later session's results failed the generation check and
//      were dropped, so the tick re-extracted todo[0] forever with no
//      DB writes. That is exactly the "wiring bug logic tests cannot
//      catch" class; tst_Wiring drives a second session to prove the
//      fix.
// ============================================================

#include "ExtractionController.h"

#include "Constants.h"
#include "FileUtils.h"
#include "Logger.h"
#include "SehTranslator.h"
#include "database/FileRepository.h"

#include <QDateTime>
#include <QFileInfo>
#include <QSharedPointer>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

#include <sqlite3.h>

namespace DocuSearch {

// ============================================================
// Construction / destruction
// ============================================================

ExtractionController::ExtractionController(QObject* parent)
    : QObject(parent)
{
    // EAGER construction — the historical bug this class exists to
    // prevent was a pool "declared but never constructed", possible
    // only because construction was lazy inside the handler.
    ensurePool();

    // The watcher lives for the controller's lifetime and its
    // continuation reads the CURRENT session — never a captured one.
    m_watcher = new QFutureWatcher<ExtractionResult>(this);
    connect(m_watcher, &QFutureWatcher<ExtractionResult>::finished,
            this, [this]() {
        m_inFlight = false;
        if (m_dbResetting) return;
        const auto state = m_session;
        // Session cancelled or replaced while this file was in
        // flight — drop the result, touch nothing.
        if (!state || state->sessionGen != m_sessionGen) return;

        try {
            const ExtractionTodo& item = state->current;
            sqlite3* raw = m_db ? m_db->raw() : nullptr;
            ExtractionResult result = m_watcher->result();
            QString extractedText = result.text;
            QString source = result.source.isEmpty() ? "native" : result.source;
            bool ok = true;

            DS_INFO("Extract",
                QString("DONE  file %1/%2: %3 — %4 chars, source=%5%6")
                    .arg(state->idx + 1).arg(state->maxFiles)
                    .arg(item.path).arg(extractedText.size()).arg(source)
                    .arg(result.needsOcr ? " (needs OCR)" : ""));

            if (result.needsOcr && extractedText.isEmpty()) {
                if (raw) {
                    sqlite3_exec(raw,
                        QString("UPDATE Files SET indexing_status='needs_ocr' WHERE id=%1;")
                            .arg(item.fileId).toUtf8().constData(),
                        nullptr, nullptr, nullptr);
                }
                ++state->done;
                ok = false;
            }

            if (ok && extractedText.size() > Constants::kMaxExtractTextChars) {
                extractedText = extractedText.left(Constants::kMaxExtractTextChars) + "\n\n[... text truncated for memory ...]";
            }

            if (ok && raw) {
                // v1.7.9: write DocumentText EVEN when the extracted text
                // is empty — an empty-but-valid document is DONE; leaving
                // no row made the integrity pass requeue it on every
                // launch. SearchIndex still only gets real content.
                QByteArray textBytes = extractedText.toUtf8();
                QByteArray srcBytes = source.toUtf8();
                qint64 charCount = extractedText.size();
                qint64 now = QDateTime::currentSecsSinceEpoch();

                sqlite3_stmt* upd = nullptr;
                sqlite3_prepare_v2(raw,
                    "INSERT INTO DocumentText (file_id, extracted_text, text_source, char_count, updated_at) "
                    "VALUES (?1, ?2, ?3, ?4, ?5) "
                    "ON CONFLICT(file_id) DO UPDATE SET "
                    "  extracted_text=excluded.extracted_text, "
                    "  text_source=excluded.text_source, "
                    "  char_count=excluded.char_count, "
                    "  updated_at=excluded.updated_at;",
                    -1, &upd, nullptr);
                if (upd) {
                    sqlite3_bind_int64(upd, 1, item.fileId);
                    sqlite3_bind_text(upd, 2, textBytes.constData(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(upd, 3, srcBytes.constData(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int64(upd, 4, charCount);
                    sqlite3_bind_int64(upd, 5, now);
                    sqlite3_step(upd);
                    sqlite3_finalize(upd);
                }

                sqlite3_exec(raw,
                    QString("UPDATE Files SET indexing_status='content_done', ocr_status='not_needed' WHERE id=%1;")
                        .arg(item.fileId).toUtf8().constData(),
                    nullptr, nullptr, nullptr);

                sqlite3_stmt* del = nullptr;
                sqlite3_prepare_v2(raw, "DELETE FROM SearchIndex WHERE file_id=?1;",
                                   -1, &del, nullptr);
                if (del) {
                    sqlite3_bind_int64(del, 1, item.fileId);
                    sqlite3_step(del);
                    sqlite3_finalize(del);
                }

                if (!extractedText.isEmpty()) {
                    QByteArray fn = state->currentFileName.toUtf8();
                    QByteArray pth = item.path.toUtf8();
                    QByteArray ext = item.ext.toUtf8();
                    sqlite3_stmt* ins = nullptr;
                    sqlite3_prepare_v2(raw,
                        "INSERT INTO SearchIndex (filename, content, path, extension, file_id) "
                        "VALUES (?1, ?2, ?3, ?4, ?5);",
                        -1, &ins, nullptr);
                    if (ins) {
                        sqlite3_bind_text(ins, 1, fn.constData(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(ins, 2, textBytes.constData(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(ins, 3, pth.constData(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(ins, 4, ext.constData(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_int64(ins, 5, item.fileId);
                        sqlite3_step(ins);
                        sqlite3_finalize(ins);
                    }
                }

                // NOTE: BGE embedding generation during extraction is DISABLED.
                // (Runs on the dedicated embedding pool via the AI backfill
                // instead — see EmbeddingController::ensureBackfill().)

                ++state->done;
            } else if (raw) {
                sqlite3_exec(raw,
                    QString("UPDATE Files SET indexing_status='failed' WHERE id=%1;")
                        .arg(item.fileId).toUtf8().constData(),
                    nullptr, nullptr, nullptr);
                ++state->failed;
            } else {
                ++state->failed;
            }
        } catch (const std::exception& e) {
            DS_WARN("Extract", QString("Post-extract error: %1").arg(e.what()));
            const auto st = m_session;
            if (st) ++st->failed;
        } catch (...) {
            const auto st = m_session;
            if (st) ++st->failed;
        }

        // v1.7.4: refresh the "N indexed" badge after EVERY file so the
        // counter visibly climbs while extraction runs.
        emit statsDirty();
        if (m_session) ++m_session->idx;
    });
}

ExtractionController::~ExtractionController()
{
    shutdownForTeardown();
}

void ExtractionController::ensurePool()
{
    if (m_pool) return;
    m_pool = new QThreadPool(this);
    m_pool->setMaxThreadCount(1);
    // CRITICAL: large stack — Poppler's recursive parser blows a 1 MB
    // stack on deeply nested PDFs (the global pool had this via
    // main.cpp; the dedicated pool needs it set itself).
    m_pool->setStackSize(16 * 1024 * 1024);
}

bool ExtractionController::verifyWiring() const
{
    const bool poolOk   = m_pool != nullptr && m_pool->maxThreadCount() == 1
                          && m_pool->stackSize() == 16 * 1024 * 1024;
    const bool watcherOk = m_watcher != nullptr;
    const bool workerOk  = static_cast<bool>(m_workerFn);
    if (!poolOk || !watcherOk || !workerOk) {
        DS_WARN("Extract", QString(
            "WIRING AUDIT FAILED: pool=%1 watcher=%2 workerFn=%3")
            .arg(poolOk).arg(watcherOk).arg(workerOk));
    }
    return poolOk && watcherOk && workerOk;
}

void ExtractionController::shutdownForTeardown()
{
    invalidateSession();
    if (m_pool) { m_pool->clear(); m_pool->waitForDone(); }
}

// ============================================================
// Session control
// ============================================================

void ExtractionController::invalidateSession()
{
    ++m_sessionGen;
    m_session.clear();
    m_cancelFlag.store(false);
    m_inFlight = false;
    m_ocrExpected = 0;
    m_ocrReceived = 0;
    if (m_timer) {
        m_timer->stop();
        m_timer->deleteLater();
        m_timer = nullptr;
    }
}

void ExtractionController::cancelSession()
{
    if (!m_running.load()) return;
    m_cancelFlag.store(true);   // the tick performs the teardown + signals
}

void ExtractionController::startFromDatabase()
{
    if (!m_db) return;
    // Toggle cancel if already running (the Extract button doubles as
    // "Stop Extracting").
    if (m_running.load()) {
        m_cancelFlag.store(true);
        emit statusMessage("Cancelling extraction...", 3000);
        return;
    }

    sqlite3* raw = m_db->raw();
    if (!raw) return;

    const TodoLists lists = gatherTodoItems(raw);

    if (lists.text.isEmpty() && lists.ocr.isEmpty()) {
        // Show detailed extraction status instead of a generic message.
        emit statusMessage(extractionStatusString(), 5000);
        return;
    }
    startSessionInternal(lists.text, lists.ocr);
}

void ExtractionController::startSession(QList<ExtractionTodo> todo,
                                        QList<ExtractionTodo> ocrTodo)
{
    if (m_running.load()) return;   // tests drive sessions one at a time
    startSessionInternal(std::move(todo), std::move(ocrTodo));
}

void ExtractionController::startSessionInternal(QList<ExtractionTodo> todo,
                                                QList<ExtractionTodo> ocrTodo)
{
    m_running.store(true);
    m_cancelFlag.store(false);
    m_dbResetting = false;
    emit extractingChanged(true);

    const int total = todo.size();
    // Restore the 30-file batch limit. Processing all pending files in one
    // go was unstable (large batches + 10ms timer interval -> memory pressure
    // + UI event starvation -> crashes). The 30-file batch + 200ms interval
    // was the original stable behavior.
    //
    // v1.7.10 FIRST-RUN MODE: until the very first full extraction drain
    // finishes, sessions are 200 files and re-arm after 3 s instead of
    // 60 s — a brand-new index extracts itself end-to-end without the
    // user babysitting the Extract button. The 200 ms per-file pacing
    // is untouched, so stability is preserved.
    const int sessionCap = m_firstRunMode ? 200 : 30;
    const int maxFilesThisSession = qMin(total, sessionCap);
    emit statusMessage(
        QString("Extracting %1 of %2 files... (click Stop Extracting to cancel)")
            .arg(maxFilesThisSession).arg(total));

    emit progressUpdated(0, maxFilesThisSession, true);

    // v1.7.9: hand the OCR work to the pool. The session stays open until
    // the pool drains (the last noteOcrResult tears it down), so the
    // button's "Stop Extracting" cancel covers OCR too.
    m_ocrReceived = 0;
    m_ocrExpected = 0;
    if (!ocrTodo.isEmpty()) {
        if (m_ocrEnqueue) {
            const int queued = m_ocrEnqueue(ocrTodo);
            m_ocrExpected = queued;
            if (queued > 0)
                emit statusMessage(
                    QString("OCR queued for %1 scanned/image file%2...")
                        .arg(queued).arg(queued == 1 ? "" : "s"), 5000);
        } else {
            DS_WARN("Extract", QString(
                "%1 OCR candidate(s) gathered but no OCR pool is wired — "
                "wiring bug; files would have been stranded forever.")
                .arg(ocrTodo.size()));
        }
    }

    auto state = QSharedPointer<SessionState>::create();
    state->todo = std::move(todo);
    state->total = total;
    state->maxFiles = maxFilesThisSession;
    ++m_sessionGen;
    state->sessionGen = m_sessionGen;
    m_session = state;

    // 200ms between files — gives the UI time to process events between
    // heavy extractions. The 10ms interval was too aggressive and caused
    // event starvation on large batches.
    m_timer = new QTimer(this);
    m_timer->setInterval(m_tickIntervalMs);
    m_timer->setSingleShot(false);

    connect(m_timer, &QTimer::timeout, this, [this, state]() {
      try {
        // Session invalidated (Stop Extracting / db reset / teardown) —
        // the timer dies with it. A file may still be in flight on the
        // pool; its continuation sees the stale generation and drops
        // the result.
        if (!m_session || state->sessionGen != m_sessionGen) {
            if (m_timer) { m_timer->stop(); m_timer->deleteLater(); m_timer = nullptr; }
            return;
        }
        // The previous file is still being extracted on the pool —
        // this tick is a no-op and simply fires again one interval
        // later. (The old busy-wait version re-entered here for every
        // file slower than the timer interval.)
        if (m_inFlight) return;
        // v1.7.10: the database was removed (Settings -> Remove Database)
        // while this session ran — kill the session instead of writing
        // into the freshly created database.
        if (m_dbResetting) {
            if (m_timer) { m_timer->stop(); m_timer->deleteLater(); m_timer = nullptr; }
            return;
        }
        // Check cancel flag.
        if (m_cancelFlag.load()) {
            if (m_timer) { m_timer->stop(); m_timer->deleteLater(); m_timer = nullptr; }
            // Invalidate the in-flight continuation BEFORE the session
            // flags drop — a result landing after this point is from a
            // dead session and must write nothing.
            ++m_sessionGen;
            if (m_ocrClear) m_ocrClear();   // v1.7.9: cancel OCR too
            m_ocrExpected = 0;
            m_ocrReceived = 0;
            m_running.store(false);
            m_cancelFlag.store(false);
            m_session.clear();
            emit extractingChanged(false);
            emit statsDirty();
            emit previewDirty();
            emit statusMessage(
                QString("Extraction cancelled (%1/%2 completed).")
                    .arg(state->done + state->failed).arg(state->total), 8000);
            emit sessionFinished(state->done, state->failed, state->total, false);
            return;
        }

        sqlite3* raw = m_db ? m_db->raw() : nullptr;

        if (state->idx >= state->total || state->idx >= state->maxFiles) {
            if (m_timer) { m_timer->stop(); m_timer->deleteLater(); m_timer = nullptr; }

            // v1.7.9: OCR work runs in the pool — keep the session open
            // until it drains; the last taskCompleted tears the session
            // down and re-arms auto-extraction for any remaining batches.
            if (ocrWorkOutstanding()) {
                const int left = m_ocrExpected - m_ocrReceived
                                 + (m_ocrSize ? m_ocrSize() : 0);
                emit statusMessage(
                    QString("Text extraction done — OCR running: %1 file(s) "
                            "left (click Stop Extracting to cancel).")
                        .arg(left), 6000);
                emit progressUpdated(m_ocrReceived, qMax(1, m_ocrExpected), true);
                return;
            }

            m_running.store(false);
            m_session.clear();
            emit extractingChanged(false);
            emit progressUpdated(0, 1, false);
            emit statsDirty();
            emit previewDirty();
            if (state->idx >= state->total) {
                emit statusMessage(
                    QString("Extraction complete: %1 succeeded, %2 failed (out of %3).")
                        .arg(state->done).arg(state->failed).arg(state->total), 8000);
                emit sessionFinished(state->done, state->failed, state->total, true);

                // v1.7.10: the first FULL drain is done — first run is
                // over. The app persists firstRunDone so later sessions
                // return to the conservative 30-file/60 s cadence. (OCR
                // tasks may still be in flight; they complete on the
                // pool independently.)
                if (m_firstRunMode) {
                    m_firstRunMode = false;
                    emit firstRunDrainComplete();
                    DS_INFO("Extract", "First-run extraction drain complete.");
                }

                // Auto-queue AI embedding generation for all newly-
                // extracted files via the standard backfill wake. The
                // pre-split code re-read SearchIndex here and called
                // bgeService_->embedDocumentsBatch directly — a SECOND
                // embedding path; the backfill reads DocumentText (the
                // authoritative store) and already drains everything
                // missing, so one wake covers the session's files and
                // the rest of the backlog with the same battle-tested
                // queue.
                emit backfillWakeRequested();
            } else {
                emit statusMessage(
                    QString("Extracted %1 of %2 — next batch runs automatically "
                            "%3 (Extract = start now).")
                        .arg(state->done + state->failed).arg(state->total)
                        .arg(m_firstRunMode ? QStringLiteral("in 3 seconds")
                                            : QStringLiteral("in 1 minute")), 8000);
                emit sessionFinished(state->done, state->failed, state->total, false);
                // Auto-continue: drain the queue without the user having to
                // click Extract after every 30-file batch. The 200ms per-file
                // pacing keeps UI responsive exactly as before; this only
                // removes the mandatory click between batches.
                // v1.7.10: first-run mode re-arms after 3 s so a new index
                // drains continuously instead of taking minutes per batch.
                emit autoRearmRequested(m_firstRunMode ? 3 * 1000 : 60 * 1000);
            }
            return;
        }

        const ExtractionTodo& item = state->todo[state->idx];
        QFileInfo fi(item.path);
        emit statusMessage(
            QString("Extracting: %1 (%2/%3)...")
                .arg(fi.fileName()).arg(state->idx + 1).arg(state->maxFiles));
        emit progressUpdated(state->idx + 1, state->maxFiles, true);

        if (!QFileInfo::exists(item.path)) {
            if (raw) {
                sqlite3_exec(raw,
                    QString("UPDATE Files SET indexing_status='failed' WHERE id=%1;")
                        .arg(item.fileId).toUtf8().constData(),
                    nullptr, nullptr, nullptr);
            }
            ++state->failed;
        } else {
            // Skip files that are too large (protects low-end systems).
            if (fi.size() > Constants::kMaxFilesizeToExtract) {
                if (raw) {
                    sqlite3_exec(raw,
                        QString("UPDATE Files SET indexing_status='skipped' WHERE id=%1;")
                            .arg(item.fileId).toUtf8().constData(),
                        nullptr, nullptr, nullptr);
                }
                ++state->failed;
            } else {

                // ── BEFORE log: filename + type + size ──────────────────
                // If the app crashes during extraction, this is the LAST
                // line in the log — it tells us exactly which file killed it.
                DS_INFO("Extract",
                    QString("START file %1/%2: %3 [%4, %5 bytes]")
                        .arg(state->idx + 1).arg(state->maxFiles)
                        .arg(item.path).arg(item.ext).arg(fi.size()));

                // HAND OFF to the dedicated pool and RETURN — no
                // busy-wait on the UI thread. The tick starts ONE file
                // and returns; the watcher continuation does the DONE
                // log, DB writes and accounting when the pool thread
                // delivers. The UI thread is fully free meanwhile —
                // typing a search during a big extraction stays instant.
                if (!m_workerFn) {
                    // Refuse loudly: an unwired pipeline must never
                    // silently spin (that is the bug class this class
                    // exists to prevent).
                    DS_WARN("Extract",
                        "No worker function wired — extraction pipeline "
                        "was never connected. Marking file failed.");
                    if (raw) {
                        sqlite3_exec(raw,
                            QString("UPDATE Files SET indexing_status='failed' WHERE id=%1;")
                                .arg(item.fileId).toUtf8().constData(),
                            nullptr, nullptr, nullptr);
                    }
                    ++state->failed;
                    ++state->idx;
                    emit statsDirty();
                    return;
                }
                state->current = item;
                state->currentFileName = fi.fileName();
                m_inFlight = true;
                const ExtractionTodo cur = item;
                m_watcher->setFuture(QtConcurrent::run(
                    m_pool, [this, cur]() -> ExtractionResult {
                        // SEH translator is PER-THREAD — install it on this
                        // worker so an access violation inside a parser is
                        // caught by catch(...) instead of crashing. Runs on
                        // the dedicated 16 MB-stack extraction thread (deep
                        // PDF recursion; see the pool setup).
                        installSehTranslator();
                        return m_workerFn(cur.path, cur.ext);
                    }));
                // NOTE: return BEFORE the common tail (statsDirty /
                // ++idx) — the file has not finished yet; the
                // continuation owns that accounting for this file.
                return;
            }
        }

        // The 200ms timer interval already provides CPU relief between
        // extractions. The previous adaptive CPU throttle (GetSystemTimes +
        // Sleep(100) on the main thread) was removed — it blocked the UI
        // for an extra 100ms per file and wasn't necessary with the 30-file
        // batch limit.

        // v1.7.4: refresh the "N indexed" badge after EVERY file so the
        // counter visibly climbs while extraction runs (the 20 s poll
        // alone read as a frozen number).
        emit statsDirty();

        ++state->idx;
      } catch (const std::exception& e) {
          emit statusMessage(QString("Extraction error: %1").arg(e.what()), 5000);
          if (state) ++state->idx;
      } catch (...) {
          emit statusMessage("Extraction error — skipping file.", 3000);
          if (state) ++state->idx;
      }
    });

    m_timer->start();
}

// ============================================================
// OCR accounting
// ============================================================

void ExtractionController::noteOcrQueued(int n)
{
    if (n <= 0) return;
    m_ocrExpected += n;
}

void ExtractionController::noteOcrResult(qint64 fileId, const QString& text, bool ok)
{
    // The database is being removed/rebuilt — this is a late result for
    // a row that no longer exists. Drop it instead of writing stale
    // text into the fresh database.
    if (m_dbResetting) return;
    if (!m_db || fileId <= 0) return;
    sqlite3* raw = m_db->raw();
    if (!raw) return;

    QString t = text;
    if (t.size() > Constants::kMaxExtractTextChars)
        t = t.left(Constants::kMaxExtractTextChars) +
            QStringLiteral("\n\n[... text truncated for memory ...]");

    if (ok) {
        const QByteArray textBytes = t.toUtf8();
        const QByteArray srcBytes  = QByteArray("ocr");
        sqlite3_stmt* upd = nullptr;
        sqlite3_prepare_v2(raw,
            "INSERT INTO DocumentText (file_id, extracted_text, text_source, char_count, updated_at) "
            "VALUES (?1, ?2, ?3, ?4, ?5) "
            "ON CONFLICT(file_id) DO UPDATE SET "
            "  extracted_text=excluded.extracted_text, "
            "  text_source=excluded.text_source, "
            "  char_count=excluded.char_count, "
            "  updated_at=excluded.updated_at;",
            -1, &upd, nullptr);
        if (upd) {
            sqlite3_bind_int64(upd, 1, fileId);
            sqlite3_bind_text(upd, 2, textBytes.constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(upd, 3, srcBytes.constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(upd, 4, t.size());
            sqlite3_bind_int64(upd, 5, QDateTime::currentSecsSinceEpoch());
            sqlite3_step(upd);
            sqlite3_finalize(upd);
        }
        sqlite3_exec(raw,
            QString("UPDATE Files SET indexing_status='content_done', "
                    "ocr_status='not_needed' WHERE id=%1;")
                .arg(fileId).toUtf8().constData(),
            nullptr, nullptr, nullptr);

        sqlite3_stmt* del = nullptr;
        sqlite3_prepare_v2(raw, "DELETE FROM SearchIndex WHERE file_id=?1;",
                           -1, &del, nullptr);
        if (del) {
            sqlite3_bind_int64(del, 1, fileId);
            sqlite3_step(del);
            sqlite3_finalize(del);
        }

        if (!t.isEmpty()) {
            QString fn, pth, ext;
            sqlite3_stmt* f = nullptr;
            if (sqlite3_prepare_v2(raw,
                    "SELECT filename, path, extension FROM Files WHERE id=?1;",
                    -1, &f, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(f, 1, fileId);
                if (sqlite3_step(f) == SQLITE_ROW) {
                    const unsigned char* a = sqlite3_column_text(f, 0);
                    const unsigned char* b = sqlite3_column_text(f, 1);
                    const unsigned char* c = sqlite3_column_text(f, 2);
                    fn  = a ? QString::fromUtf8(reinterpret_cast<const char*>(a)) : QString();
                    pth = b ? QString::fromUtf8(reinterpret_cast<const char*>(b)) : QString();
                    ext = c ? QString::fromUtf8(reinterpret_cast<const char*>(c)) : QString();
                }
                sqlite3_finalize(f);
            }
            sqlite3_stmt* ins = nullptr;
            sqlite3_prepare_v2(raw,
                "INSERT INTO SearchIndex (filename, content, path, extension, file_id) "
                "VALUES (?1, ?2, ?3, ?4, ?5);",
                -1, &ins, nullptr);
            if (ins) {
                const QByteArray fnb  = fn.toUtf8();
                const QByteArray pthb = pth.toUtf8();
                const QByteArray extb = ext.toUtf8();
                sqlite3_bind_text(ins, 1, fnb.constData(),  -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(ins, 2, textBytes.constData(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(ins, 3, pthb.constData(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(ins, 4, extb.constData(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(ins, 5, fileId);
                sqlite3_step(ins);
                sqlite3_finalize(ins);
            }
        }
    } else {
        // OCR failed (no language packs, unreadable image...). 'failed'
        // is honest; the next content change re-queues the file.
        sqlite3_exec(raw,
            QString("UPDATE Files SET indexing_status='failed' WHERE id=%1;")
                .arg(fileId).toUtf8().constData(),
            nullptr, nullptr, nullptr);
    }

    emit statsDirty();  // badge climbs while OCR runs (same as text path)

    // Session accounting — a cancel/reset already cleared the counters,
    // so late results from a cancelled queue land here harmlessly.
    if (m_running.load() && m_ocrExpected > 0 &&
        m_ocrReceived < m_ocrExpected) {
        ++m_ocrReceived;
        emit progressUpdated(m_ocrReceived, qMax(1, m_ocrExpected), true);
        if (m_ocrReceived >= m_ocrExpected &&
            (!m_ocrSize || m_ocrSize() == 0)) {
            const int finished = m_ocrExpected;
            m_running.store(false);
            m_cancelFlag.store(false);
            m_ocrExpected = 0;
            m_ocrReceived = 0;
            m_session.clear();
            emit extractingChanged(false);
            emit progressUpdated(0, 1, false);
            emit statsDirty();
            emit previewDirty();
            emit statusMessage(
                QString("OCR complete (%1 file%2) — checking for more work...")
                    .arg(finished).arg(finished == 1 ? "" : "s"), 6000);
            emit sessionFinished(0, 0, finished, false);
            // Re-arm extraction: any remaining text batches continue.
            emit autoRearmRequested(1500);
        }
    }
}

bool ExtractionController::ocrWorkOutstanding() const
{
    return m_ocrExpected > 0 &&
           (m_ocrReceived < m_ocrExpected ||
            (m_ocrSize && m_ocrSize() > 0));
}

// ============================================================
// Todo gathering (pure SQL — unit-testable)
// ============================================================

ExtractionController::TodoLists
ExtractionController::gatherTodoItems(sqlite3* raw)
{
    TodoLists out;
    if (!raw) return out;
    struct TodoItem { qint64 fileId; QString path; QString ext; };

    // Retire plain-text-ish types the user excluded from extraction.
    // Leaving them as 'metadata_only' made them look like pending work
    // forever; 'skipped' is honest and keeps them filename-searchable.
    sqlite3_exec(raw,
        "UPDATE Files SET indexing_status='skipped' "
        "WHERE indexing_status='metadata_only' "
        "AND lower(extension) IN ('txt','csv','md','rtf','log');",
        nullptr, nullptr, nullptr);

    sqlite3_stmt* s = nullptr;
    const char* sql =
        "SELECT id, path, extension FROM Files "
        "WHERE indexing_status = 'metadata_only' "
        "AND extension IN ("
        "'pdf','doc','docx',"
        "'xls','xlsx','xlsm',"
        "'ppt','pptx') "
        "ORDER BY id;";
    if (sqlite3_prepare_v2(raw, sql, -1, &s, nullptr) == SQLITE_OK) {
        while (sqlite3_step(s) == SQLITE_ROW) {
            ExtractionTodo it;
            it.fileId = sqlite3_column_int64(s, 0);
            const unsigned char* p = sqlite3_column_text(s, 1);
            const unsigned char* e = sqlite3_column_text(s, 2);
            it.path = p ? QString::fromUtf8(reinterpret_cast<const char*>(p)) : QString();
            it.ext  = e ? QString::fromUtf8(reinterpret_cast<const char*>(e)) : QString();
            out.text.append(it);
        }
        sqlite3_finalize(s);
    }

    // v1.7.9: gather OCR work — this used to be collected by NOBODY.
    // needs_ocr rows (scanned PDFs, garbled text layers flagged by the
    // extractors) and images (ocr_status 'pending' from the scan) were
    // invisible, so "Extract" said there was nothing to do while those
    // files never became searchable. They run on the OCR worker pool
    // (PDFium page renders + Windows OCR), not the text-extractor
    // pipeline.
    sqlite3_stmt* o = nullptr;
    const char* ocrSql =
        "SELECT id, path, extension FROM Files "
        "WHERE (indexing_status = 'needs_ocr' "
        "       AND extension IN ('pdf','doc','docx',"
        "                        'xls','xlsx','xlsm','ppt','pptx')) "
        "   OR (extension IN ('jpg','jpeg','png','tif','tiff',"
        "                     'bmp','gif','webp') "
        "       AND indexing_status IN ('metadata_only','needs_ocr') "
        "       AND ocr_status IN ('pending','needs_ocr')) "
        "ORDER BY id;";
    if (sqlite3_prepare_v2(raw, ocrSql, -1, &o, nullptr) == SQLITE_OK) {
        while (sqlite3_step(o) == SQLITE_ROW) {
            ExtractionTodo it;
            it.fileId = sqlite3_column_int64(o, 0);
            const unsigned char* p = sqlite3_column_text(o, 1);
            const unsigned char* e = sqlite3_column_text(o, 2);
            it.path = p ? QString::fromUtf8(reinterpret_cast<const char*>(p)) : QString();
            it.ext  = e ? QString::fromUtf8(reinterpret_cast<const char*>(e)) : QString();
            out.ocr.append(it);
        }
        sqlite3_finalize(o);
    }
    return out;
}

// ============================================================
// Single-file pipeline (FileWatcher add/modify)
// ============================================================

bool ExtractionController::extractAndIndexFile(const QString& path)
{
    if (!m_repo || !m_db) return false;

    // SAFETY: only files the injected settings gate admits (indexed
    // folders, excluded folders, excluded extensions). The file
    // watcher should only fire for these, but this is a defensive
    // check in case a watch was added for a folder the user later
    // removed from Settings.
    if (m_pathGate && !m_pathGate(path)) return false;

    // Check if extension is supported.
    // v1.7.7: one central allowlist rules every ingest path.
    const QString ext = FileUtils::extensionOf(path).toLower();
    if (!Constants::isIndexableExtension(ext)) return false;

    const QFileInfo fi(path);
    if (!fi.exists()) return false;

    FileRecord r;
    r.path         = FileUtils::toNative(path);
    r.filename     = fi.fileName();
    r.extension    = FileUtils::extensionOf(path);
    r.size         = fi.size();
    r.createdDate  = fi.birthTime();
    r.modifiedDate = fi.lastModified();
    r.indexingStatus = Constants::IndexingStatus::kPending;
    r.ocrStatus      = Constants::OcrStatus::kPending;
    m_repo->upsertFile(r);

    // Extract text immediately for the new/changed file (single-file
    // extraction — fast and non-blocking enough for the main thread).
    if (fi.size() <= Constants::kMaxFilesizeToExtract) {
        try {
            if (!m_workerFn) {
                DS_WARN("Watcher",
                    "No worker function wired — single-file extraction "
                    "pipeline was never connected (wiring bug).");
                return true;
            }
            auto result = m_workerFn(path, ext);
            QString extractedText = result.text;
            if (extractedText.size() > Constants::kMaxExtractTextChars) {
                extractedText = extractedText.left(Constants::kMaxExtractTextChars);
            }

            sqlite3* raw = m_db->raw();
            if (!raw) return true;

            FileRecord rec;
            if (!m_repo->getByPath(r.path, rec)) return true;  // row vanished
            const qint64 fileId = rec.id;

            if (!extractedText.isEmpty()) {
                const qint64 now = QDateTime::currentSecsSinceEpoch();
                sqlite3_stmt* upd = nullptr;
                sqlite3_prepare_v2(raw,
                    "INSERT INTO DocumentText (file_id, extracted_text, text_source, char_count, updated_at) "
                    "VALUES (?1, ?2, ?3, ?4, ?5) "
                    "ON CONFLICT(file_id) DO UPDATE SET "
                    "  extracted_text=excluded.extracted_text, "
                    "  text_source=excluded.text_source, "
                    "  char_count=excluded.char_count, "
                    "  updated_at=excluded.updated_at;",
                    -1, &upd, nullptr);
                if (upd) {
                    QByteArray textBytes = extractedText.toUtf8();
                    QByteArray srcBytes = (result.source.isEmpty() ? "native" : result.source).toUtf8();
                    sqlite3_bind_int64(upd, 1, fileId);
                    sqlite3_bind_text(upd, 2, textBytes.constData(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(upd, 3, srcBytes.constData(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int64(upd, 4, extractedText.size());
                    sqlite3_bind_int64(upd, 5, now);
                    sqlite3_step(upd);
                    sqlite3_finalize(upd);

                    // Update Files status.
                    sqlite3_exec(raw,
                        QString("UPDATE Files SET indexing_status='content_done' WHERE id=%1;")
                            .arg(fileId).toUtf8().constData(),
                        nullptr, nullptr, nullptr);

                    // Update SearchIndex (delete + insert = clean FTS row).
                    sqlite3_stmt* del = nullptr;
                    sqlite3_prepare_v2(raw, "DELETE FROM SearchIndex WHERE file_id=?1;", -1, &del, nullptr);
                    if (del) { sqlite3_bind_int64(del, 1, fileId); sqlite3_step(del); sqlite3_finalize(del); }

                    QByteArray fn = fi.fileName().toUtf8();
                    QByteArray pth = r.path.toUtf8();
                    QByteArray extB = ext.toUtf8();
                    sqlite3_stmt* ins = nullptr;
                    sqlite3_prepare_v2(raw,
                        "INSERT INTO SearchIndex (filename, content, path, extension, file_id) "
                        "VALUES (?1, ?2, ?3, ?4, ?5);",
                        -1, &ins, nullptr);
                    if (ins) {
                        sqlite3_bind_text(ins, 1, fn.constData(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(ins, 2, textBytes.constData(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(ins, 3, pth.constData(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(ins, 4, extB.constData(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_int64(ins, 5, fileId);
                        sqlite3_step(ins);
                        sqlite3_finalize(ins);
                    }
                }
            } else if (result.needsOcr) {
                // Scanned PDF / image — mark as needs_ocr.
                sqlite3_exec(raw,
                    QString("UPDATE Files SET indexing_status='needs_ocr' WHERE id=%1;")
                        .arg(fileId).toUtf8().constData(),
                    nullptr, nullptr, nullptr);
            }

            // v1.7.5: the file's content just changed — any stored AI
            // embedding was computed from the OLD text and must not
            // survive. Drop both embedding tables for this file; the
            // background backfill queue re-embeds it from the new text
            // (EmbeddingController::ensureBackfill runs ONNX off the
            // main thread).
            for (const char* delSql : {
                     "DELETE FROM BgeEmbeddings WHERE file_id=?1;",
                     "DELETE FROM EmbeddingChunks WHERE file_id=?1;" }) {
                sqlite3_stmt* delE = nullptr;
                if (sqlite3_prepare_v2(raw, delSql, -1, &delE, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int64(delE, 1, fileId);
                    sqlite3_step(delE);
                    sqlite3_finalize(delE);
                }
            }
            emit backfillWakeRequested();
        } catch (...) {
            // Extraction failed — file stays as 'pending', user can retry.
            DS_INFO("Watcher", "Extraction failed for: " + path);
        }
    }
    return true;
}

// ============================================================
// Status string (pure SQL — unit-testable)
// ============================================================

QString ExtractionController::extractionStatusString()
{
    if (!m_db) return "Database not open.";
    sqlite3* raw = m_db->raw();
    if (!raw) return "Database not accessible.";

    int total = 0, done = 0, failed = 0, pending = 0, needsOcr = 0, skipped = 0;
    sqlite3_stmt* s = nullptr;
    if (sqlite3_prepare_v2(raw,
        "SELECT indexing_status, COUNT(*) FROM Files GROUP BY indexing_status;",
        -1, &s, nullptr) == SQLITE_OK) {
        while (sqlite3_step(s) == SQLITE_ROW) {
            const char* status = reinterpret_cast<const char*>(sqlite3_column_text(s, 0));
            const int count = sqlite3_column_int(s, 1);
            total += count;
            QString sStr = status ? QString::fromUtf8(status) : "";
            if (sStr == "content_done") done += count;
            else if (sStr == "failed") failed += count;
            else if (sStr == "pending") pending += count;
            else if (sStr == "metadata_only") pending += count;
            else if (sStr == "needs_ocr") needsOcr += count;
            else if (sStr == "skipped") skipped += count;
        }
        sqlite3_finalize(s);
    }

    if (pending == 0 && needsOcr == 0) {
        return QString("All files extracted: %1 done, %2 failed, %3 skipped (total: %4)")
            .arg(done).arg(failed).arg(skipped).arg(total);
    }
    return QString("Extraction: %1 done, %2 pending, %3 need OCR, %4 failed (total: %5)")
        .arg(done).arg(pending).arg(needsOcr).arg(failed).arg(total);
}

} // namespace DocuSearch
