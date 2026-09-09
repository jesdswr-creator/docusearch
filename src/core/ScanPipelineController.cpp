// ============================================================
// ScanPipelineController.cpp — implementation
// ============================================================
// Every async body opens its OWN sqlite connection (FULLMUTEX +
// busy_timeout 5000, the contract Database::open sets on the main
// connection) and captures NOTHING but values — so a controller
// destroyed mid-job can never leave a worker touching dead members.

#include "ScanPipelineController.h"

#include "Config.h"
#include "Constants.h"
#include "FileUtils.h"
#include "Logger.h"
#include "TextQuality.h"
#include "StorageHealth.h"
#include "Database.h"

#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QtConcurrent>

namespace DocuSearch {

// ============================================================
// Construction / teardown
// ============================================================

ScanPipelineController::ScanPipelineController(QObject* parent)
    : QObject(parent) {}

// Per-run watchers make the lifetime story explicit: each job owns a
// QFutureWatcher parented to `this`; its finished handler carries that
// run's shared result. A controller destroyed mid-job destroys the
// watcher with it (no delivery), and the worker — which captured only
// values — finishes harmlessly into the void.

ScanPipelineController::~ScanPipelineController() = default;

qint64 ScanPipelineController::autoScanElapsedMs() const {
    if (!autoScanRunning_.load()) return 0;
    return QDateTime::currentMSecsSinceEpoch() - autoScanStartedMs_;
}

// ============================================================
// Auto-scan (hourly / startup diff)
// ============================================================

bool ScanPipelineController::startAutoScan(const ScanParams& params) {
    if (autoScanRunning_.load()) {
        // v1.7.3 watchdog: a scan stuck for >30 min (network share gone
        // silent, dead drive) used to block EVERY future scan via the
        // running flag. Re-arm: start a fresh scan under a NEW
        // generation, so the stale job's finished handler (still armed
        // on its own per-run watcher) can never clear the flag or emit
        // a result that belongs to the past.
        if (autoScanElapsedMs() < 30 * 60 * 1000)
            return false;   // previous scan still healthy — caller skips
        DS_WARN("Scan", "Auto-scan watchdog: previous scan stuck >30 min "
                        "- re-arming");
    }
    autoScanRunning_  = true;
    autoScanStartedMs_ = QDateTime::currentMSecsSinceEpoch();
    const qint64 gen = ++autoScanGen_;

    auto stats = std::make_shared<ScanStats>();

    const QStringList folderList        = params.folders;
    const QStringList excludedFolders   = params.excludedFolders;
    const QSet<QString> userExcludedExts =
        normalizedExtSet(params.excludedExtensions);
    const QString dbPath                = params.dbPath;
    const bool hashEnabled              = params.hashEnabled;

    QFuture<void> future = QtConcurrent::run(
        [folderList, excludedFolders, userExcludedExts,
         dbPath, hashEnabled, stats]() {
        sqlite3* workerDb = nullptr;
        if (sqlite3_open_v2(dbPath.toUtf8().constData(), &workerDb,
                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX,
                            nullptr) != SQLITE_OK) {
            return;
        }
        // v1.7.11: the main connection sets busy_timeout (Database::open);
        // this raw worker connection must too, or a concurrent UI-thread
        // write hands it SQLITE_BUSY and silently skips its work.
        sqlite3_exec(workerDb, "PRAGMA busy_timeout = 5000;",
                     nullptr, nullptr, nullptr);

        for (const auto& folder : folderList) {
            try {
                const QString root = FileUtils::toNative(folder);

                // v1.7.3: if the folder is temporarily unavailable
                // (unplugged drive, disconnected share) DO NOT walk and
                // DO NOT prune - pruning would wipe the entire index for
                // it and the files would have to be re-extracted from
                // scratch on return. Skip and report instead.
                if (!QDir(root).exists()) { ++stats->unavailable; continue; }

                // ---- Pass 1: walk + upsert, remembering what we saw ----
                QSet<QString> seen;              // case-folded native paths
                seen.reserve(1024);
                FileUtils::walkDirectory(folder, excludedFolders,
                    [&](const QFileInfo& fi) -> bool {
                        // v1.7.7: THE extension allowlist gate. Checked
                        // BEFORE the path enters `seen`, so the Pass-2
                        // prune below treats every non-indexable file
                        // (md notes, txt, logs, installers, archives...)
                        // as unseen and deletes any row an older version
                        // indexed — the index self-heals to documents +
                        // images only.
                        // v1.7.11: the user's Excluded Extensions list is
                        // an additional gate on top of the allowlist.
                        const QString ext =
                            FileUtils::extensionOf(fi.absoluteFilePath());
                        if (!Constants::isIndexableExtension(ext))
                            return true;
                        if (userExcludedExts.contains(ext.toLower()))
                            return true;

                        const QString path =
                            FileUtils::toNative(fi.absoluteFilePath());
                        seen.insert(path.toLower());

                        // Read the existing row (if any) so we can tell
                        // "same file, metadata refresh" from "file CHANGED".
                        qint64 oldSize = -1, oldModified = -1;
                        QString oldHash;
                        bool isNew = true;
                        sqlite3_stmt* chk = nullptr;
                        if (sqlite3_prepare_v2(workerDb,
                                "SELECT id, size, modified_date, hash FROM "
                                "Files WHERE path = ?1;",
                                -1, &chk, nullptr) == SQLITE_OK) {
                            sqlite3_bind_text(chk, 1,
                                              path.toUtf8().constData(), -1,
                                              SQLITE_TRANSIENT);
                            if (sqlite3_step(chk) == SQLITE_ROW) {
                                isNew       = false;
                                oldSize     = sqlite3_column_int64(chk, 1);
                                oldModified = sqlite3_column_int64(chk, 2);
                                const unsigned char* h =
                                    sqlite3_column_text(chk, 3);
                                oldHash = h ? QString::fromUtf8(
                                    reinterpret_cast<const char*>(h))
                                            : QString();
                            }
                            sqlite3_finalize(chk);
                        }

                        const qint64 size = fi.size();
                        const qint64 modified =
                            fi.lastModified().toSecsSinceEpoch();
                        // Same size + same mtime = untouched file.
                        const bool changed =
                            !isNew && (size != oldSize ||
                                       modified != oldModified);

                        // (Re)compute the hash only when it can have
                        // changed - brand-new, modified, or rows whose hash
                        // was never computed (backfill so the duplicates
                        // finder stays meaningful). Otherwise preserve the
                        // stored hash: re-hashing every unchanged file on
                        // every scan would hammer the disk for nothing.
                        QString hash;
                        if (!hashEnabled) {
                            hash = oldHash;              // preserve whatever exists
                        } else if (isNew || changed || oldHash.isEmpty()) {
                            hash = FileUtils::sha256OfFile(
                                path, 64 * 1024 * 1024);
                        } else {
                            hash = oldHash;
                        }

                        const char* ocrStat =
                            (Constants::kDocumentExtensions.contains(ext) ||
                             Constants::kImageExtensions.contains(ext))
                                ? "pending" : "not_needed";

                        if (isNew) {
                            sqlite3_stmt* upd = nullptr;
                            sqlite3_prepare_v2(workerDb,
                                "INSERT INTO Files (path, filename, "
                                "  extension, size, created_date, "
                                "  modified_date, hash, indexing_status, "
                                "  ocr_status) "
                                "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)",
                                -1, &upd, nullptr);
                            if (upd) {
                                sqlite3_bind_text(upd, 1,
                                                  path.toUtf8().constData(),
                                                  -1, SQLITE_TRANSIENT);
                                sqlite3_bind_text(upd, 2, fi.fileName()
                                                          .toUtf8()
                                                          .constData(),
                                                  -1, SQLITE_TRANSIENT);
                                sqlite3_bind_text(upd, 3,
                                                  ext.toUtf8().constData(),
                                                  -1, SQLITE_TRANSIENT);
                                sqlite3_bind_int64(upd, 4, size);
                                sqlite3_bind_int64(
                                    upd, 5, fi.birthTime().toSecsSinceEpoch());
                                sqlite3_bind_int64(upd, 6, modified);
                                sqlite3_bind_text(
                                    upd, 7, hash.toUtf8().constData(), -1,
                                    SQLITE_TRANSIENT);
                                sqlite3_bind_text(upd, 8, "metadata_only",
                                                  -1, SQLITE_TRANSIENT);
                                sqlite3_bind_text(upd, 9, ocrStat, -1,
                                                  SQLITE_TRANSIENT);
                                sqlite3_step(upd);
                                sqlite3_finalize(upd);
                            }
                            ++stats->newFiles;
                        } else if (changed) {
                            // v1.7.3 CRITICAL FIX: refresh metadata, and
                            // ONLY when the file actually changed re-queue
                            // it for extraction/OCR (the old upsert stamped
                            // content_done on every row every scan).
                            sqlite3_stmt* upd = nullptr;
                            sqlite3_prepare_v2(workerDb,
                                "UPDATE Files SET filename = ?2, "
                                "  extension = ?3, size = ?4, "
                                "  modified_date = ?6, hash = ?7, "
                                "  indexing_status = 'metadata_only', "
                                "  ocr_status = ?9 "
                                "WHERE id = (SELECT id FROM Files "
                                "            WHERE path = ?1)",
                                -1, &upd, nullptr);
                            if (upd) {
                                sqlite3_bind_text(upd, 1,
                                                  path.toUtf8().constData(),
                                                  -1, SQLITE_TRANSIENT);
                                sqlite3_bind_text(upd, 2, fi.fileName()
                                                          .toUtf8()
                                                          .constData(),
                                                  -1, SQLITE_TRANSIENT);
                                sqlite3_bind_text(upd, 3,
                                                  ext.toUtf8().constData(),
                                                  -1, SQLITE_TRANSIENT);
                                sqlite3_bind_int64(upd, 4, size);
                                sqlite3_bind_int64(upd, 6, modified);
                                sqlite3_bind_text(
                                    upd, 7, hash.toUtf8().constData(), -1,
                                    SQLITE_TRANSIENT);
                                sqlite3_bind_text(upd, 9, ocrStat, -1,
                                                  SQLITE_TRANSIENT);
                                sqlite3_step(upd);
                                sqlite3_finalize(upd);
                            }
                            ++stats->updatedFiles;
                        } else if (hashEnabled && !hash.isEmpty()) {
                            // Unchanged file: refresh metadata + (preserved
                            // or backfilled) hash. NEVER touch
                            // indexing_status/ocr_status here - queued
                            // (metadata_only), needs-OCR and failed rows
                            // must survive scans untouched.
                            sqlite3_stmt* upd = nullptr;
                            sqlite3_prepare_v2(workerDb,
                                "UPDATE Files SET filename = ?2, "
                                "  extension = ?3, size = ?4, "
                                "  modified_date = ?6, hash = ?7 "
                                "WHERE id = (SELECT id FROM Files "
                                "            WHERE path = ?1)",
                                -1, &upd, nullptr);
                            if (upd) {
                                sqlite3_bind_text(upd, 1,
                                                  path.toUtf8().constData(),
                                                  -1, SQLITE_TRANSIENT);
                                sqlite3_bind_text(upd, 2, fi.fileName()
                                                          .toUtf8()
                                                          .constData(),
                                                  -1, SQLITE_TRANSIENT);
                                sqlite3_bind_text(upd, 3,
                                                  ext.toUtf8().constData(),
                                                  -1, SQLITE_TRANSIENT);
                                sqlite3_bind_int64(upd, 4, size);
                                sqlite3_bind_int64(upd, 6, modified);
                                sqlite3_bind_text(
                                    upd, 7, hash.toUtf8().constData(), -1,
                                    SQLITE_TRANSIENT);
                                sqlite3_step(upd);
                                sqlite3_finalize(upd);
                            }
                        } else {
                            // Unchanged, no hash refresh: filename/ext
                            // metadata only.
                            sqlite3_stmt* upd = nullptr;
                            sqlite3_prepare_v2(workerDb,
                                "UPDATE Files SET filename = ?2, "
                                "  extension = ?3, size = ?4, "
                                "  modified_date = ?6 "
                                "WHERE id = (SELECT id FROM Files "
                                "            WHERE path = ?1)",
                                -1, &upd, nullptr);
                            if (upd) {
                                sqlite3_bind_text(upd, 1,
                                                  path.toUtf8().constData(),
                                                  -1, SQLITE_TRANSIENT);
                                sqlite3_bind_text(upd, 2, fi.fileName()
                                                          .toUtf8()
                                                          .constData(),
                                                  -1, SQLITE_TRANSIENT);
                                sqlite3_bind_text(upd, 3,
                                                  ext.toUtf8().constData(),
                                                  -1, SQLITE_TRANSIENT);
                                sqlite3_bind_int64(upd, 4, size);
                                sqlite3_bind_int64(upd, 6, modified);
                                sqlite3_step(upd);
                                sqlite3_finalize(upd);
                            }
                        }
                        return true;
                    });

                // ---- Pass 2: prune rows the walk did not see ----
                // The FileWatcher only removes rows for deletions that
                // happen WHILE the app runs. Files deleted or moved while
                // it was closed stayed in the index forever - showing up
                // in search results and skewing the duplicates finder and
                // the "N indexed" badge. Reconcile now: any row under this
                // folder whose case-folded path was not seen is gone.
                // (Hidden/system files are also skipped by the walk; a
                // row for one would be pruned and re-added next scan -
                // accepted churn, far better than permanent ghosts.)
                QString prefix = root;
                while (prefix.endsWith('\\')) prefix.chop(1);
                prefix += QLatin1Char('\\');

                QList<qint64> staleIds;
                sqlite3_stmt* q = nullptr;
                if (sqlite3_prepare_v2(workerDb,
                        "SELECT id, path FROM Files "
                        "WHERE upper(substr(path, 1, ?1)) = upper(?2);",
                        -1, &q, nullptr) == SQLITE_OK) {
                    const QByteArray prefixUtf8 = prefix.toUtf8();
                    sqlite3_bind_int(q, 1, prefix.length());
                    sqlite3_bind_text(q, 2, prefixUtf8.constData(),
                                      -1, SQLITE_TRANSIENT);
                    while (sqlite3_step(q) == SQLITE_ROW) {
                        const qint64 id = sqlite3_column_int64(q, 0);
                        const unsigned char* p = sqlite3_column_text(q, 1);
                        const QString rowPath = p
                            ? QString::fromUtf8(
                                  reinterpret_cast<const char*>(p))
                            : QString();
                        if (seen.contains(rowPath.toLower())) continue;
                        staleIds.append(id);
                    }
                    sqlite3_finalize(q);
                }
                if (!staleIds.isEmpty()) {
                    sqlite3_exec(workerDb, "BEGIN;", nullptr, nullptr,
                                 nullptr);
                    for (const qint64 id : staleIds) {
                        // v1.7.24: the prune now ALSO deletes
                        // EmbeddingChunks - the cascade the comment below
                        // relied on covers Tags/Notes/DocumentText only,
                        // and the missing chunk rows were orphaned on
                        // every scan prune (the chunk-mode AI search
                        // would still scan them until the file's next
                        // extraction overwrote them).
                        static const char* kDelSql[] = {
                            "DELETE FROM Files WHERE id = ?1;",
                            "DELETE FROM SearchIndex WHERE file_id = ?1;",
                            "DELETE FROM BgeEmbeddings WHERE file_id = ?1;",
                            "DELETE FROM EmbeddingChunks WHERE file_id = ?1;",
                        };
                        for (const char* sql : kDelSql) {
                            sqlite3_stmt* d = nullptr;
                            if (sqlite3_prepare_v2(workerDb, sql, -1, &d,
                                                   nullptr) == SQLITE_OK) {
                                sqlite3_bind_int64(d, 1, id);
                                sqlite3_step(d);
                                sqlite3_finalize(d);
                            }
                        }
                        ++stats->removedFiles;
                    }
                    sqlite3_exec(workerDb, "COMMIT;", nullptr, nullptr,
                                 nullptr);
                }
            } catch (...) {}
        }

        sqlite3_close(workerDb);
    });

    // Per-run watcher + generation guard (see startAutoScan's comment):
    // only the CURRENT generation may clear the flag and report.
    auto* watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this,
            [this, watcher, stats, gen]() {
        watcher->deleteLater();
        if (gen != autoScanGen_) return;   // stale watchdog job
        autoScanRunning_ = false;
        emit autoScanFinished(*stats);
    });
    watcher->setFuture(future);
    return true;
}

// ============================================================
// Folder ingest scan (Add-Folder / newly added drive)
// ============================================================

void ScanPipelineController::startFolderScan(const QString& folder,
                                             const ScanParams& params) {
    if (folderScanRunning_.load()) {
        // FIFO: the old synchronous code serialized requests naturally;
        // the queue preserves that without blocking the UI thread.
        pendingFolders_.append(folder);
        pendingParams_.append(params);
        return;
    }
    folderScanRunning_ = true;

    auto stats = std::make_shared<ScanStats>();

    const QStringList excludedFolders   = params.excludedFolders;
    const QSet<QString> userExcludedExts =
        normalizedExtSet(params.excludedExtensions);
    const QString dbPath                = params.dbPath;
    const bool hashEnabled              = params.hashEnabled;

    QFuture<void> future = QtConcurrent::run(
        [folder, excludedFolders, userExcludedExts,
         dbPath, hashEnabled, stats]() {
        sqlite3* workerDb = nullptr;
        if (sqlite3_open_v2(dbPath.toUtf8().constData(), &workerDb,
                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX,
                            nullptr) != SQLITE_OK) {
            return;
        }
        sqlite3_exec(workerDb, "PRAGMA busy_timeout = 5000;",
                     nullptr, nullptr, nullptr);

        // ONLY index file types the user cares about — documents and
        // common images. v1.7.7: this scan consults the SAME central
        // allowlist (Constants::isIndexableExtension) as the hourly
        // scan, the watcher and the full re-index, so every ingest
        // path agrees on what may enter the index.
        //
        // v1.7.10: this scan also computes the CONTENT HASH so the
        // index is duplicates-ready the moment the scan finishes,
        // matching what the hourly walk already does.
        FileUtils::walkDirectory(folder, excludedFolders,
            [&](const QFileInfo& fi) -> bool {
            const QString ext =
                FileUtils::extensionOf(fi.absoluteFilePath()).toLower();
            if (!Constants::isIndexableExtension(ext)) { ++stats->skipped; return true; }
            if (userExcludedExts.contains(ext))         { ++stats->skipped; return true; }

            const QString path = FileUtils::toNative(fi.absoluteFilePath());
            const QString filename = fi.fileName();
            const qint64 size = fi.size();
            const qint64 created = fi.birthTime().toSecsSinceEpoch();
            const qint64 modified = fi.lastModified().toSecsSinceEpoch();
            const char* ocrStat =
                (Constants::kDocumentExtensions.contains(ext) ||
                 Constants::kImageExtensions.contains(ext))
                ? "pending" : "not_needed";

            // Same 64 MB cap as the hourly walk, so both paths store the
            // SAME fingerprint for the SAME file and duplicates group
            // correctly no matter which scanner saw the file first.
            QString hash;
            if (hashEnabled) {
                hash = FileUtils::sha256OfFile(path, 64 * 1024 * 1024);
                if (!hash.isEmpty()) ++stats->hashedFiles;
            }

            sqlite3_stmt* s = nullptr;
            sqlite3_prepare_v2(workerDb,
                "INSERT INTO Files (path, filename, extension, size, "
                "  created_date, modified_date, hash, indexing_status, "
                "  ocr_status) "
                "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, 'metadata_only', ?8) "
                "ON CONFLICT(path) DO UPDATE SET "
                "  filename=excluded.filename, extension=excluded.extension, "
                "  size=excluded.size, modified_date=excluded.modified_date, "
                "  hash=CASE WHEN excluded.hash != '' "
                "            THEN excluded.hash ELSE Files.hash END;",
                -1, &s, nullptr);
            if (s) {
                sqlite3_bind_text(s, 1, path.toUtf8().constData(), -1,
                                  SQLITE_TRANSIENT);
                sqlite3_bind_text(s, 2, filename.toUtf8().constData(), -1,
                                  SQLITE_TRANSIENT);
                sqlite3_bind_text(s, 3, ext.toUtf8().constData(), -1,
                                  SQLITE_TRANSIENT);
                sqlite3_bind_int64(s, 4, size);
                sqlite3_bind_int64(s, 5, created);
                sqlite3_bind_int64(s, 6, modified);
                sqlite3_bind_text(s, 7, hash.toUtf8().constData(), -1,
                                  SQLITE_TRANSIENT);
                sqlite3_bind_text(s, 8, ocrStat, -1, SQLITE_TRANSIENT);
                sqlite3_step(s);
                sqlite3_finalize(s);
            }
            ++stats->newFiles;
            return true;
        });

        sqlite3_close(workerDb);
    });

    auto* watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this,
            [this, watcher, stats]() {
        watcher->deleteLater();
        folderScanRunning_ = false;
        emit folderScanFinished(stats->newFiles, stats->skipped,
                                stats->hashedFiles);
        pumpPendingFolderScan();
    });
    watcher->setFuture(future);
}

void ScanPipelineController::pumpPendingFolderScan() {
    if (pendingFolders_.isEmpty() || folderScanRunning_.load()) return;
    const QString folder = pendingFolders_.takeFirst();
    const ScanParams params = pendingParams_.takeFirst();
    startFolderScan(folder, params);
}

// ============================================================
// Startup integrity pass
// ============================================================

void ScanPipelineController::startIntegrityPass(const ScanParams& params,
                                                bool junkAuditNeeded) {
    const bool expected = false;
    if (!integrityRunning_.compare_exchange_strong(expected, true)) return;

    auto result = std::make_shared<IntegrityResult>();

    const QString dbPath        = params.dbPath;
    const bool hashEnabled      = params.hashEnabled;

    QFuture<void> future = QtConcurrent::run(
        [dbPath, hashEnabled, junkAuditNeeded, result]() {
        sqlite3* workerDb = nullptr;
        if (sqlite3_open_v2(dbPath.toUtf8().constData(), &workerDb,
                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX,
                            nullptr) != SQLITE_OK) {
            return;
        }
        sqlite3_exec(workerDb, "PRAGMA busy_timeout = 5000;",
                     nullptr, nullptr, nullptr);

        // v1.7.9: requeue fake-done rows — scans older than v1.7.3
        // stamped content_done without extracting anything.
        sqlite3_stmt* rq = nullptr;
        if (sqlite3_prepare_v2(workerDb,
                "UPDATE Files SET indexing_status='metadata_only' "
                "WHERE indexing_status='content_done' "
                "AND extension IN ('pdf','doc','docx','xls','xlsx','xlsm',"
                "'ppt','pptx') "
                "AND id NOT IN (SELECT file_id FROM DocumentText);",
                -1, &rq, nullptr) == SQLITE_OK) {
            sqlite3_step(rq);
            result->requeued = sqlite3_changes(workerDb);
            sqlite3_finalize(rq);
        }

        // v1.7.10: give FAILED rows one honest retry per launch.
        // Bounded at 500 per launch so a folder of permanently
        // unreadable files can't stall startup; rows that fail again
        // simply wait for the next launch.
        {
            QList<qint64> failIds;
            sqlite3_stmt* fq = nullptr;
            if (sqlite3_prepare_v2(workerDb,
                    "SELECT id, extension FROM Files "
                    "WHERE indexing_status='failed' "
                    "AND extension IN ('pdf','doc','docx','xls','xlsx',"
                    "'xlsm','ppt','pptx','jpg','jpeg','png','tif','tiff',"
                    "'bmp','gif','webp') "
                    "AND id NOT IN (SELECT file_id FROM DocumentText) "
                    "LIMIT 500;",
                    -1, &fq, nullptr) == SQLITE_OK) {
                while (sqlite3_step(fq) == SQLITE_ROW)
                    failIds.append(sqlite3_column_int64(fq, 0));
                sqlite3_finalize(fq);
            }
            // v1.7.24: one PREPARED statement (reset per row) instead of
            // N interpolated sqlite_exec calls — the 2026-09 audit's
            // string-interpolated-SQL finding.
            sqlite3_stmt* uq = nullptr;
            if (sqlite3_prepare_v2(workerDb,
                    "UPDATE Files SET indexing_status='metadata_only', "
                    "ocr_status='pending' WHERE id=?1;",
                    -1, &uq, nullptr) == SQLITE_OK) {
                for (const qint64 id : failIds) {
                    sqlite3_reset(uq);
                    sqlite3_clear_bindings(uq);
                    sqlite3_bind_int64(uq, 1, id);
                    sqlite3_step(uq);
                    ++result->failedRequeued;
                }
                sqlite3_finalize(uq);
            }
        }

        // v1.7.16: ONE-TIME junk-text audit. Older builds accepted the
        // text layer some scanner drivers embed INTO a PDF at scan time
        // even when it is garbage: a rotated page OCR'd by the scanner
        // itself yields punctuation-soup letter fragments that the old
        // classifier's scope rules never examined. That junk was indexed
        // as real content. The classifier now has a fragment-soup gate,
        // so this pass re-judges every stored text against it: flagged
        // rows lose their text, FTS row and (stale-junk) embeddings and
        // drop back to metadata_only. The classifier is microseconds per
        // row, so every row is examined in one pass - a LIMIT here would
        // silently un-audit large libraries. One-shot via the persisted
        // flag the CALLER owns (the result reports whether the audit
        // actually scanned).
        if (junkAuditNeeded) {
            bool auditScanned = false;
            QList<qint64> junkIds;
            {
                sqlite3_stmt* jq = nullptr;
                if (sqlite3_prepare_v2(workerDb,
                        "SELECT f.id, substr(d.extracted_text, 1, 20000) "
                        "FROM DocumentText d JOIN Files f ON f.id = d.file_id "
                        "WHERE f.indexing_status = 'content_done' "
                        "AND length(d.extracted_text) > 0;",
                        -1, &jq, nullptr) == SQLITE_OK) {
                    while (sqlite3_step(jq) == SQLITE_ROW) {
                        const unsigned char* t = sqlite3_column_text(jq, 1);
                        const QString text = t
                            ? QString::fromUtf8(
                                  reinterpret_cast<const char*>(t))
                            : QString();
                        QString why;
                        if (TextQuality::looksLikeGarbage(text, &why)) {
                            DS_INFO("Index", QString("Junk-text audit: file "
                                                     "%1 flagged (%2)")
                                                     .arg(qint64(
                                                         sqlite3_column_int64(
                                                             jq, 0)))
                                                     .arg(why));
                            junkIds.append(sqlite3_column_int64(jq, 0));
                        }
                    }
                    sqlite3_finalize(jq);
                    auditScanned = true;
                }
            }
            if (auditScanned) {
                static const char* kDelSql[] = {
                    "DELETE FROM DocumentText WHERE file_id=?1;",
                    "DELETE FROM SearchIndex WHERE file_id=?1;",
                    "DELETE FROM BgeEmbeddings WHERE file_id=?1;",
                    "DELETE FROM EmbeddingChunks WHERE file_id=?1;",
                };
                sqlite3_stmt* uq = nullptr;
                if (sqlite3_prepare_v2(workerDb,
                        "UPDATE Files SET indexing_status='metadata_only', "
                        "ocr_status='pending' WHERE id=?1;",
                        -1, &uq, nullptr) == SQLITE_OK) {
                    for (const qint64 id : junkIds) {
                        for (const char* delSql : kDelSql) {
                            sqlite3_stmt* del = nullptr;
                            if (sqlite3_prepare_v2(workerDb, delSql, -1,
                                                   &del,
                                                   nullptr) == SQLITE_OK) {
                                sqlite3_bind_int64(del, 1, id);
                                sqlite3_step(del);
                                sqlite3_finalize(del);
                            }
                        }
                        sqlite3_reset(uq);
                        sqlite3_clear_bindings(uq);
                        sqlite3_bind_int64(uq, 1, id);
                        sqlite3_step(uq);
                        ++result->junkRequeued;
                    }
                    sqlite3_finalize(uq);
                }
            }
            result->junkAuditScanned = auditScanned;
        }

        // Hash backfill for rows the scanners have not fingerprinted
        // yet (bounded at 4000 per launch — the duplicates finder
        // needs something to group).
        if (hashEnabled) {
            QList<qint64> ids;
            QStringList paths;
            sqlite3_stmt* q = nullptr;
            if (sqlite3_prepare_v2(workerDb,
                    "SELECT id, path FROM Files "
                    "WHERE (hash IS NULL OR hash = '') "
                    "AND extension IN ('pdf','doc','docx','xls','xlsx',"
                    "'xlsm','ppt','pptx','jpg','jpeg','png','tif','tiff',"
                    "'bmp','gif','webp') "
                    "LIMIT 4000;",
                    -1, &q, nullptr) == SQLITE_OK) {
                while (sqlite3_step(q) == SQLITE_ROW) {
                    ids.append(sqlite3_column_int64(q, 0));
                    const unsigned char* p = sqlite3_column_text(q, 1);
                    paths.append(p ? QString::fromUtf8(
                        reinterpret_cast<const char*>(p)) : QString());
                }
                sqlite3_finalize(q);
            }
            sqlite3_stmt* u = nullptr;
            if (sqlite3_prepare_v2(workerDb,
                    "UPDATE Files SET hash=?1 WHERE id=?2;",
                    -1, &u, nullptr) == SQLITE_OK) {
                for (int i = 0; i < ids.size(); ++i) {
                    const QString h =
                        FileUtils::sha256OfFile(paths.at(i),
                                                64 * 1024 * 1024);
                    if (h.isEmpty()) continue;  // unreadable now — retry
                    sqlite3_reset(u);
                    sqlite3_clear_bindings(u);
                    sqlite3_bind_text(u, 1, h.toUtf8().constData(), -1,
                                      SQLITE_TRANSIENT);
                    sqlite3_bind_int64(u, 2, ids.at(i));
                    sqlite3_step(u);
                    ++result->hashed;
                }
                sqlite3_finalize(u);
            }
        }

        if (result->requeued > 0 || result->hashed > 0 ||
            result->failedRequeued > 0 || result->junkRequeued > 0) {
            DS_INFO("Index", QString("Startup integrity pass: %1 fake-done "
                                     "rows requeued for extraction, %2 "
                                     "failed rows retried, %3 junk-text "
                                     "rows requeued, %4 hashes backfilled.")
                                 .arg(result->requeued)
                                 .arg(result->failedRequeued)
                                 .arg(result->junkRequeued)
                                 .arg(result->hashed));
        }

        sqlite3_close(workerDb);
    });

    auto* watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this,
            [this, watcher, result]() {
        watcher->deleteLater();
        integrityRunning_ = false;
        emit integrityFinished(*result);
    });
    watcher->setFuture(future);
}

// ============================================================
// One-time non-indexable-row purge (async — was a UI-thread
// batch loop pumping processEvents between 500-row commits)
// ============================================================

void ScanPipelineController::startPurgeNonIndexable(const QString& dbPath) {
    const bool expected = false;
    if (!purgeRunning_.compare_exchange_strong(expected, true)) return;

    auto purged = std::make_shared<std::atomic<int>>(0);

    QFuture<void> future = QtConcurrent::run([dbPath, purged]() {
        sqlite3* workerDb = nullptr;
        if (sqlite3_open_v2(dbPath.toUtf8().constData(), &workerDb,
                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX,
                            nullptr) != SQLITE_OK) {
            return;
        }
        sqlite3_exec(workerDb, "PRAGMA busy_timeout = 5000;",
                     nullptr, nullptr, nullptr);

        QString list;
        for (const QString& ext : Constants::kIndexableExtensions) {
            if (!list.isEmpty()) list += QLatin1Char(',');
            list += QString("'%1'").arg(ext.toLower());
        }

        QList<qint64> ids;
        sqlite3_stmt* q = nullptr;
        // NULL-extension rows (should not exist, but older scans could
        // write them) must purge too — "NOT IN" alone would keep them
        // via 3-valued logic, so the IS NULL case is spelled out.
        const QString sql = QString(
            "SELECT id FROM Files "
            "WHERE extension IS NULL "
            "   OR lower(trim(extension)) NOT IN (%1);").arg(list);
        if (sqlite3_prepare_v2(workerDb, sql.toUtf8().constData(), -1,
                               &q, nullptr) == SQLITE_OK) {
            while (sqlite3_step(q) == SQLITE_ROW)
                ids.append(sqlite3_column_int64(q, 0));
            sqlite3_finalize(q);
        }
        if (ids.isEmpty()) {
            sqlite3_close(workerDb);
            return;
        }

        // v1.7.8: each statement is prepared ONCE, rows are committed
        // in 500-row batches (progress survives a kill). The batch
        // loop no longer pumps any event loop — there is none to pump
        // on a worker thread; the UI stays live by construction.
        static const char* kDelSql[] = {
            "DELETE FROM Files WHERE id = ?1;",
            "DELETE FROM SearchIndex WHERE file_id = ?1;",
            "DELETE FROM BgeEmbeddings WHERE file_id = ?1;",
            "DELETE FROM EmbeddingChunks WHERE file_id = ?1;",
        };
        sqlite3_stmt* del[4] = {nullptr, nullptr, nullptr, nullptr};
        bool prepared = true;
        for (int i = 0; i < 4 && prepared; ++i)
            prepared = sqlite3_prepare_v2(workerDb, kDelSql[i], -1,
                                          &del[i], nullptr) == SQLITE_OK;
        if (prepared) {
            constexpr int kPurgeBatch = 500;
            const qsizetype total = ids.size();
            for (qsizetype start = 0; start < total; start += kPurgeBatch) {
                const qsizetype end = qMin(start + kPurgeBatch, total);
                sqlite3_exec(workerDb, "BEGIN;", nullptr, nullptr, nullptr);
                for (qsizetype i = start; i < end; ++i) {
                    for (sqlite3_stmt* d : del) {
                        sqlite3_reset(d);
                        sqlite3_clear_bindings(d);
                        sqlite3_bind_int64(d, 1, ids.at(i));
                        sqlite3_step(d);
                    }
                }
                sqlite3_exec(workerDb, "COMMIT;", nullptr, nullptr, nullptr);
                purged->store(static_cast<int>(end));
            }
        }
        for (sqlite3_stmt* d : del) if (d) sqlite3_finalize(d);

        DS_INFO("Index", QString("Startup purge: removed %1 non-document "
                                 "index entries (md/txt/exe/archive/...)")
                             .arg(purged->load()));
        sqlite3_close(workerDb);
    });

    auto* watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this,
            [this, watcher, purged]() {
        watcher->deleteLater();
        purgeRunning_ = false;
        emit purgeNonIndexableFinished(purged->load());
    });
    watcher->setFuture(future);
}

// ============================================================
// Synchronous maintenance (caller's thread, caller's Database)
// ============================================================

qint64 ScanPipelineController::purgeFolderFromIndex(Database& db,
                                                    const QString& folder) {
    sqlite3* raw = db.raw();
    if (!raw) return 0;

    QString prefix = FileUtils::toNative(folder);
    while (prefix.endsWith('\\')) prefix.chop(1);
    if (prefix.isEmpty()) return 0;
    prefix += QLatin1Char('\\');

    QList<qint64> ids;
    sqlite3_stmt* q = nullptr;
    if (sqlite3_prepare_v2(raw,
            "SELECT id FROM Files "
            "WHERE upper(substr(path, 1, ?1)) = upper(?2);",
            -1, &q, nullptr) == SQLITE_OK) {
        const QByteArray prefixUtf8 = prefix.toUtf8();
        sqlite3_bind_int(q, 1, prefix.length());
        sqlite3_bind_text(q, 2, prefixUtf8.constData(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(q) == SQLITE_ROW) {
            ids.append(sqlite3_column_int64(q, 0));
        }
        sqlite3_finalize(q);
    }
    if (ids.isEmpty()) return 0;

    // Mirror FileRepository::deleteFile (Files + SearchIndex +
    // BgeEmbeddings + EmbeddingChunks; cascades cover Tags/Notes/Text).
    sqlite3_exec(raw, "BEGIN;", nullptr, nullptr, nullptr);
    static const char* kDelSql[] = {
        "DELETE FROM Files WHERE id = ?1;",
        "DELETE FROM SearchIndex WHERE file_id = ?1;",
        "DELETE FROM BgeEmbeddings WHERE file_id = ?1;",
        "DELETE FROM EmbeddingChunks WHERE file_id = ?1;",
    };
    for (const qint64 id : ids) {
        for (const char* sql : kDelSql) {
            sqlite3_stmt* d = nullptr;
            if (sqlite3_prepare_v2(raw, sql, -1, &d, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(d, 1, id);
                sqlite3_step(d);
                sqlite3_finalize(d);
            }
        }
    }
    sqlite3_exec(raw, "COMMIT;", nullptr, nullptr, nullptr);

    DS_INFO("Settings", QString("Purged %1 index rows under removed folder %2")
                          .arg(ids.size()).arg(folder));
    return ids.size();
}

int ScanPipelineController::purgeNonIndexableRows(Database& db) {
    sqlite3* raw = db.raw();
    if (!raw) return 0;

    QString list;
    for (const QString& ext : Constants::kIndexableExtensions) {
        if (!list.isEmpty()) list += QLatin1Char(',');
        list += QString("'%1'").arg(ext.toLower());
    }

    QList<qint64> ids;
    sqlite3_stmt* q = nullptr;
    const QString sql = QString(
        "SELECT id FROM Files "
        "WHERE extension IS NULL "
        "   OR lower(trim(extension)) NOT IN (%1);").arg(list);
    if (sqlite3_prepare_v2(raw, sql.toUtf8().constData(), -1,
                           &q, nullptr) == SQLITE_OK) {
        while (sqlite3_step(q) == SQLITE_ROW)
            ids.append(sqlite3_column_int64(q, 0));
        sqlite3_finalize(q);
    }
    if (ids.isEmpty()) return 0;

    static const char* kDelSql[] = {
        "DELETE FROM Files WHERE id = ?1;",
        "DELETE FROM SearchIndex WHERE file_id = ?1;",
        "DELETE FROM BgeEmbeddings WHERE file_id = ?1;",
        "DELETE FROM EmbeddingChunks WHERE file_id = ?1;",
    };
    sqlite3_stmt* del[4] = {nullptr, nullptr, nullptr, nullptr};
    bool prepared = true;
    for (int i = 0; i < 4 && prepared; ++i)
        prepared = sqlite3_prepare_v2(raw, kDelSql[i], -1,
                                      &del[i], nullptr) == SQLITE_OK;
    if (!prepared) {
        for (sqlite3_stmt* d : del) if (d) sqlite3_finalize(d);
        return 0;
    }

    constexpr int kPurgeBatch = 500;
    int purged = 0;
    const qsizetype total = ids.size();
    for (qsizetype start = 0; start < total; start += kPurgeBatch) {
        const qsizetype end = qMin(start + kPurgeBatch, total);
        sqlite3_exec(raw, "BEGIN;", nullptr, nullptr, nullptr);
        for (qsizetype i = start; i < end; ++i) {
            for (sqlite3_stmt* d : del) {
                sqlite3_reset(d);
                sqlite3_clear_bindings(d);
                sqlite3_bind_int64(d, 1, ids.at(i));
                sqlite3_step(d);
            }
        }
        sqlite3_exec(raw, "COMMIT;", nullptr, nullptr, nullptr);
        purged = static_cast<int>(end);
    }
    for (sqlite3_stmt* d : del) if (d) sqlite3_finalize(d);

    DS_INFO("Index", QString("Startup purge: removed %1 non-document "
                             "index entries (md/txt/exe/archive/...)")
                         .arg(purged));
    return purged;
}

} // namespace DocuSearch
