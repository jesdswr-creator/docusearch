// ============================================================
// EmbeddingController.cpp - the AI-embedding backfill state machine
// ============================================================
// Ported verbatim from MainWindow::ensureEmbeddingsBackfill /
// startEmbeddingRebuild / purgeEmbeddingsTick / countMissing* /
// onBgeEmbeddingProgress / onBgeEmbeddingFinished (v1.7.20).
// Differences: UI calls became signals (chipChanged / statusMessage /
// rebuildBlocked) and BgeService became the IEmbeddingService
// interface so the whole machine runs headless in tst_Wiring.
// ============================================================

#include "EmbeddingController.h"

#include "database/Database.h"
#include "embeddings/BgeEmbeddingDb.h"
#include "core/Logger.h"

#include <QTimer>
#include <sqlite3.h>

namespace DocuSearch {

EmbeddingController::EmbeddingController(QObject* parent)
    : QObject(parent)
{
    // EAGER construction — a lazily-created pool is how "declared but
    // never constructed" bugs become possible at all.
    m_pool = new QThreadPool(this);
    m_pool->setMaxThreadCount(1);
}

EmbeddingController::~EmbeddingController()
{
    shutdownForTeardown();
}

bool EmbeddingController::verifyWiring() const
{
    const bool poolOk = m_pool != nullptr && m_pool->maxThreadCount() == 1;
    if (!poolOk) {
        DS_WARN("BGE", "WIRING AUDIT FAILED: embedding pool not constructed "
                       "or misconfigured.");
    }
    return poolOk;
}

void EmbeddingController::shutdownForTeardown()
{
    stopAll();
    if (m_pool) { m_pool->clear(); m_pool->waitForDone(); }
}

void EmbeddingController::stopAll()
{
    m_rebuildPurging = false;   // the purge-chain singleShots re-check
    m_backfillRunning = false;
}

// ============================================================
// Backfill
// ============================================================

void EmbeddingController::ensureBackfill()
{
    // One batch in flight at a time; noteEmbeddingFinished chains the
    // next batch while unembedded files remain, so a >1000-file backlog
    // drains progressively instead of being silently truncated.
    if (m_paused.load() || m_backfillRunning || m_rebuildPurging
        || !m_service || !m_service->isReady() || !m_db)
        return;
    sqlite3* raw = m_db->raw();
    if (!raw) return;

    // ── Phase 0 (v1.7.14): stale-embedding invalidation ─────────────
    // Extraction can rewrite DocumentText for a file that ALREADY has AI
    // vectors (file edited and re-extracted, OCR re-run, integrity
    // requeue). The backfill below only ever selected files with NO
    // embedding row, so vectors computed from the OLD text were searched
    // forever — semantic results silently drifted away from the file's
    // real content. Files whose text is newer than their whole-document
    // embedding get every vector row dropped here; Phase A below then
    // re-enqueues them in this very pass, so the index heals itself with
    // no user action. Bounded per tick to keep the tick cheap.
    {
        sqlite3_stmt* sel = nullptr;
        sqlite3_prepare_v2(raw,
            "SELECT dt.file_id "
            "FROM DocumentText dt "
            "JOIN BgeEmbeddings e ON e.file_id = dt.file_id "
            "WHERE dt.updated_at > e.updated_at "
            "LIMIT 200;",
            -1, &sel, nullptr);
        if (sel) {
            QList<int> stale;
            while (sqlite3_step(sel) == SQLITE_ROW) {
                stale.append(static_cast<int>(sqlite3_column_int64(sel, 0)));
            }
            sqlite3_finalize(sel);
            if (!stale.isEmpty()) {
                bool ok = true;
                sqlite3_stmt* delE = nullptr;
                sqlite3_stmt* delC = nullptr;
                sqlite3_prepare_v2(raw,
                    "DELETE FROM BgeEmbeddings WHERE file_id = ?1;",
                    -1, &delE, nullptr);
                sqlite3_prepare_v2(raw,
                    "DELETE FROM EmbeddingChunks WHERE file_id = ?1;",
                    -1, &delC, nullptr);
                m_db->begin();
                for (const int fid : stale) {
                    if (delE) {
                        sqlite3_bind_int64(delE, 1, fid);
                        if (sqlite3_step(delE) != SQLITE_DONE) ok = false;
                        sqlite3_reset(delE);
                    }
                    if (delC) {
                        sqlite3_bind_int64(delC, 1, fid);
                        if (sqlite3_step(delC) != SQLITE_DONE) ok = false;
                        sqlite3_reset(delC);
                    }
                }
                if (ok) m_db->commit(); else m_db->rollback();
                if (delE) sqlite3_finalize(delE);
                if (delC) sqlite3_finalize(delC);
                if (ok) {
                    DS_INFO("BGE", QString(
                        "Invalidated %1 stale embedding set(s) whose "
                        "extracted text changed — re-embedding now.")
                        .arg(stale.size()));
                }
            }
        }
    }

    QVector<int> fileIds;
    QStringList texts;
    bool chunkMode = false;

    // Phase A — documents with extracted text but no CURRENT embedding.
    // Sourced from DocumentText (the authoritative extraction store);
    // the old SearchIndex-based query missed most documents because the
    // FTS table only carries a subset of extracted content.
    // v1.7.15: rows stamped with an OLDER algo_version (or pre-versioning
    // rows, which default to 0) count as missing too — that is how the
    // garbage vectors written by the broken hash-fallback tokenizer
    // builds get replaced automatically, without the user ever finding
    // the "Rebuild AI Embeddings" button.
    // v1.7.26: ensureBackfill runs on the UI thread, so BOTH selection
    // queries (a) cap a single pathological text with SUBSTR and
    // (b) early-stop once the batch text budget is reached. The budget
    // is the pressure-aware byte cap that keeps this thread burst small
    // on low-RAM machines; files beyond it arrive with the next chained
    // batch (ORDER BY file_id — no starvation).
    const qint64 batchBudgetBytes = m_pressureMode.load()
        ? kBatchTextBytesPressure : kBatchTextBytesNormal;
    qint64 batchBytes = 0;
    {
        sqlite3_stmt* sel = nullptr;
        if (sqlite3_prepare_v2(raw,
            "SELECT dt.file_id, SUBSTR(dt.extracted_text, 1, ?2) "
            "FROM DocumentText dt "
            "LEFT JOIN BgeEmbeddings e ON e.file_id = dt.file_id "
            "WHERE (e.file_id IS NULL "
            "       OR COALESCE(e.algo_version, 0) < ?1) "
            "  AND length(dt.extracted_text) > 0 "
            "ORDER BY dt.file_id LIMIT 500;",
            -1, &sel, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(sel, 1, BgeEmbeddingDb::kAlgoVersion);
            sqlite3_bind_int(sel, 2, kMaxDocTextChars);
        }
        if (sel) {
            while (sqlite3_step(sel) == SQLITE_ROW) {
                const int fileId = static_cast<int>(sqlite3_column_int64(sel, 0));
                const unsigned char* c = sqlite3_column_text(sel, 1);
                if (c && c[0]) {
                    batchBytes += sqlite3_column_bytes(sel, 1);
                    fileIds.append(fileId);
                    texts.append(QString::fromUtf8(
                        reinterpret_cast<const char*>(c)));
                    if (batchBytes >= batchBudgetBytes) break;
                }
            }
            sqlite3_finalize(sel);
        }
    }

    // Phase B — documents with a CURRENT full-document embedding but no
    // CURRENT chunk embeddings. They either predate chunked indexing or
    // their chunks were built by an older algorithm (v1.7.15) — either
    // way, semantic search is blind to the relevant part of them until
    // the chunks are regenerated.
    if (fileIds.isEmpty()) {
        chunkMode = true;
        batchBytes = 0;
        sqlite3_stmt* sel = nullptr;
        if (sqlite3_prepare_v2(raw,
            "SELECT dt.file_id, SUBSTR(dt.extracted_text, 1, ?3) "
            "FROM DocumentText dt "
            "WHERE EXISTS (SELECT 1 FROM BgeEmbeddings b "
            "              WHERE b.file_id = dt.file_id "
            "                AND COALESCE(b.algo_version, 0) >= ?1) "
            "  AND NOT EXISTS (SELECT 1 FROM EmbeddingChunks c "
            "                   WHERE c.file_id = dt.file_id "
            "                     AND COALESCE(c.algo_version, 0) >= ?2) "
            "  AND length(dt.extracted_text) > 1000 "
            "ORDER BY dt.file_id LIMIT 300;",
            -1, &sel, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(sel, 1, BgeEmbeddingDb::kAlgoVersion);
            sqlite3_bind_int(sel, 2, BgeEmbeddingDb::kAlgoVersion);
            sqlite3_bind_int(sel, 3, kMaxDocTextChars);
        }
        if (sel) {
            while (sqlite3_step(sel) == SQLITE_ROW) {
                const int fileId = static_cast<int>(sqlite3_column_int64(sel, 0));
                const unsigned char* c = sqlite3_column_text(sel, 1);
                if (c && c[0]) {
                    batchBytes += sqlite3_column_bytes(sel, 1);
                    fileIds.append(fileId);
                    texts.append(QString::fromUtf8(
                        reinterpret_cast<const char*>(c)));
                    if (batchBytes >= batchBudgetBytes) break;
                }
            }
            sqlite3_finalize(sel);
        }
    }
    if (fileIds.isEmpty()) return;

    m_backfillRunning = true;
    m_backfillChunkMode = chunkMode;
    emit chipChanged(QString("0/%1").arg(fileIds.size()), true);
    emit statusMessage(
        chunkMode
            ? QString("AI: building chunk index for %1 document%2...")
                .arg(fileIds.size()).arg(fileIds.size() == 1 ? "" : "s")
            : QString("AI: generating embeddings for %1 unembedded file%2...")
                .arg(fileIds.size())
                .arg(fileIds.size() == 1 ? "" : "s"));
    m_service->embedDocumentsBatch(fileIds, texts);
}

void EmbeddingController::noteEmbeddingProgress(int current, int total)
{
    // Chip stays live during backfill so the user can SEE the AI working.
    emit chipChanged(QString("%1/%2").arg(current).arg(total), true);
    emit statusMessage(
        QString("Embedding documents: %1/%2").arg(current).arg(total));
}

void EmbeddingController::noteEmbeddingFinished(int success, int fail)
{
    m_backfillRunning = false;
    emit backfillBatchFinished(success, fail);
    // Deadlock guard: a batch where EVERY file failed would be re-selected
    // verbatim by the next query and retried forever. Two consecutive
    // all-fail batches means something is systematically wrong — stop and
    // say so instead of spinning.
    if (success == 0 && fail > 0) ++m_backfillDeadlock;
    else                          m_backfillDeadlock = 0;

    const qint64 remaining = countMissingEmbeddings() + countMissingChunkDocs();
    if (m_backfillDeadlock >= 2 && remaining > 0) {
        m_backfillDeadlock = 0;
        emit statusMessage(
            QString("AI indexing paused — %1 document%2 could not be "
                    "embedded (see log). Keyword search is unaffected.")
                .arg(remaining)
                .arg(remaining == 1 ? "" : "s"), 10000);
        emit chipChanged(m_aiEnabled ? "ON" : "OFF", false);
        return;
    }
    if (remaining > 0) {
        // Mid-drain: say exactly how much work is left instead of claiming
        // completion after every batch (the old message fired per batch,
        // which read as "done" while thousands were still queued).
        emit statusMessage(
            QString("AI indexing: %1 processed this pass, %2 remaining...")
                .arg(success).arg(remaining));
        emit chipChanged(QString("%1 left").arg(remaining), true);
    } else {
        emit statusMessage(
            QString("AI indexing complete — %1 document%2 embedded%3")
                .arg(success)
                .arg(success == 1 ? "" : "s")
                .arg(fail > 0 ? QString(", %1 failed").arg(fail) : QString()),
            8000);
        emit chipChanged(m_aiEnabled ? "ON" : "OFF", false);
    }
    // Chain unconditionally while work remains. The old gate stopped the
    // drain whenever the AI toggle was off, freezing the queue forever —
    // embeddings are cheap, async, and useful the moment AI is re-enabled.
    if (remaining > 0 && m_service && m_service->isReady()) {
        // v1.7.26: pressure mode stretches the chain delay 25 ms ->
        // 250 ms so ONNX inference on the embedding pool interleaves
        // politely with everything else on a low-RAM machine instead of
        // running back-to-back. The old 250 ms flat delay stretched the
        // whole backlog for no benefit on healthy machines — keep 25 ms
        // there. Either way inference is off the UI thread.
        const int chainDelayMs = m_pressureMode.load() ? 250 : 25;
        QTimer::singleShot(chainDelayMs, this, [this]() { ensureBackfill(); });
    }
}

// ============================================================
// Rebuild (purge chain + handover)
// ============================================================

void EmbeddingController::startRebuild()
{
    if (!m_service || !m_service->isReady() || !m_db) {
        // The UI owns dialogs — the controller reports the block.
        emit rebuildBlocked(QStringLiteral(
            "AI search is not ready, so there is nothing to rebuild.\n\n"
            "Make sure the AI model is installed at:\n"
            "  models/bge-small-en-v1.5/model.onnx\n"
            "  models/bge-small-en-v1.5/vocab.txt"));
        return;
    }
    if (m_backfillRunning || m_rebuildPurging) {
        emit statusMessage(
            "AI is already working — wait for the current batch to "
            "finish, then rebuild.", 6000);
        return;
    }
    sqlite3* raw = m_db->raw();
    if (!raw) return;

    // Only run when there is something to rebuild; otherwise the user
    // would watch a silent purge that accomplishes nothing.
    qint64 existing = 0;
    sqlite3_stmt* s = nullptr;
    if (sqlite3_prepare_v2(raw,
            "SELECT (SELECT COUNT(*) FROM EmbeddingChunks)"
            "     + (SELECT COUNT(*) FROM BgeEmbeddings);",
            -1, &s, nullptr) == SQLITE_OK) {
        if (sqlite3_step(s) == SQLITE_ROW)
            existing = sqlite3_column_int64(s, 0);
        sqlite3_finalize(s);
    }
    if (existing == 0) {
        emit statusMessage(
            "No embeddings stored yet — use 'Generate AI Embeddings for "
            "All Documents' instead.", 6000);
        return;
    }

    m_rebuildPurging = true;
    m_rebuildRetries = 0;
    emit statusMessage(QString(
        "AI: rebuilding embeddings — clearing %1 stored row%2 ...")
        .arg(existing).arg(existing == 1 ? "" : "s"));
    QTimer::singleShot(25, this, [this]() { purgeEmbeddingsTick(); });
}

void EmbeddingController::purgeEmbeddingsTick()
{
    if (!m_db || !m_rebuildPurging) return;
    sqlite3* raw = m_db->raw();
    if (!raw) {
        m_rebuildPurging = false;
        return;
    }

    // Chunks first so phase B of the backfill sees a clean slate the
    // moment doc-level embeddings start refilling. DELETE with LIMIT
    // is not compiled into every SQLite build, so batch via a subquery
    // instead — portable and just as fast at these sizes.
    static const char* const kDeletes[] = {
        "DELETE FROM EmbeddingChunks WHERE chunk_id IN "
        "(SELECT chunk_id FROM EmbeddingChunks LIMIT 1000);",
        "DELETE FROM BgeEmbeddings WHERE file_id IN "
        "(SELECT file_id FROM BgeEmbeddings LIMIT 500);"
    };
    int deleted = 0;
    bool sqlError = false;
    for (const char* sql : kDeletes) {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(raw, sql, -1, &st, nullptr) == SQLITE_OK
            && sqlite3_step(st) == SQLITE_DONE) {
            deleted += sqlite3_changes(raw);
        } else {
            sqlError = true;   // transient lock / I/O hiccup
        }
        if (st) sqlite3_finalize(st);
    }

    if (sqlError) {
        // busy_timeout (5-10 s) makes this nearly impossible; still,
        // never abandon the chain on the first hiccup — retry a while,
        // then give up loudly rather than half-purging in silence.
        if (++m_rebuildRetries <= 50) {
            QTimer::singleShot(100, this,
                [this]() { purgeEmbeddingsTick(); });
            return;
        }
        m_rebuildPurging = false;
        emit statusMessage(
            "AI rebuild stopped — the database stayed locked. Close other "
            "DocuSearch windows and try again.", 8000);
        return;
    }
    m_rebuildRetries = 0;

    if (deleted > 0) {
        qint64 remaining = 0;
        sqlite3_stmt* s = nullptr;
        if (sqlite3_prepare_v2(raw,
                "SELECT (SELECT COUNT(*) FROM EmbeddingChunks)"
                "     + (SELECT COUNT(*) FROM BgeEmbeddings);",
                -1, &s, nullptr) == SQLITE_OK) {
            if (sqlite3_step(s) == SQLITE_ROW)
                remaining = sqlite3_column_int64(s, 0);
            sqlite3_finalize(s);
        }
        emit statusMessage(QString(
            "AI: clearing old embeddings — %1 row%2 left ...")
            .arg(remaining).arg(remaining == 1 ? "" : "s"));
        QTimer::singleShot(25, this,
            [this]() { purgeEmbeddingsTick(); });
        return;
    }

    // Purge complete — hand over to the standard two-phase backfill.
    // Every remaining DocumentText row now lacks an embedding, so the
    // existing chain (doc-level first, then chunks) rebuilds the whole
    // library from FULL document text with live status-chip progress.
    m_rebuildPurging = false;
    emit statusMessage(
        "AI: old embeddings cleared — rebuilding from full document "
        "text. Progress: 'Embedding documents: X/Y'.", 8000);
    ensureBackfill();
}

// ============================================================
// Backlog counters
// ============================================================

qint64 EmbeddingController::countMissingEmbeddings()
{
    if (!m_db) return 0;
    sqlite3* raw = m_db->raw();
    if (!raw) return 0;
    sqlite3_stmt* s = nullptr;
    qint64 n = 0;
    // v1.7.15: stale-algorithm embeddings count as missing (see
    // ensureBackfill) — this is the number the "AI indexing:
    // N remaining" status line and the backfill chain work from.
    if (sqlite3_prepare_v2(raw,
            "SELECT COUNT(*) FROM DocumentText dt "
            "LEFT JOIN BgeEmbeddings e ON e.file_id = dt.file_id "
            "WHERE (e.file_id IS NULL "
            "       OR COALESCE(e.algo_version, 0) < ?1) "
            "  AND length(dt.extracted_text) > 0;",
            -1, &s, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(s, 1, BgeEmbeddingDb::kAlgoVersion);
        if (sqlite3_step(s) == SQLITE_ROW) n = sqlite3_column_int64(s, 0);
        sqlite3_finalize(s);
    }
    return n;
}

qint64 EmbeddingController::countMissingChunkDocs()
{
    if (!m_db) return 0;
    sqlite3* raw = m_db->raw();
    if (!raw) return 0;
    sqlite3_stmt* s = nullptr;
    qint64 n = 0;
    if (sqlite3_prepare_v2(raw,
            "SELECT COUNT(*) FROM DocumentText dt "
            "WHERE EXISTS (SELECT 1 FROM BgeEmbeddings b "
            "              WHERE b.file_id = dt.file_id "
            "                AND COALESCE(b.algo_version, 0) >= ?1) "
            "  AND NOT EXISTS (SELECT 1 FROM EmbeddingChunks c "
            "                   WHERE c.file_id = dt.file_id "
            "                     AND COALESCE(c.algo_version, 0) >= ?2) "
            "  AND length(dt.extracted_text) > 1000;",
            -1, &s, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(s, 1, BgeEmbeddingDb::kAlgoVersion);
        sqlite3_bind_int(s, 2, BgeEmbeddingDb::kAlgoVersion);
        if (sqlite3_step(s) == SQLITE_ROW) n = sqlite3_column_int64(s, 0);
        sqlite3_finalize(s);
    }
    return n;
}

} // namespace DocuSearch
