// ============================================================
// ScanPipelineController.h — the index-scan pipeline, headless
// ============================================================
//
// v1.7.24: the third pipeline cut out of the MainWindow god-object
// (after ExtractionController and EmbeddingController, v1.7.21). It
// owns every path that WALKS the filesystem and writes index rows:
//
//   • startAutoScan          — the hourly / startup diff scan (was
//                              MainWindow::autoScanIndexedFolders, 418
//                              lines): two passes per folder (walk +
//                              upsert, then prune rows the walk did
//                              not see), the >30-min watchdog, and
//                              the unavailable-folder skip.
//   • startFolderScan        — the Add-Folder / new-drive ingest scan
//                              (was MainWindow::scanFolderFast, a
//                              SYNCHRONOUS UI-thread walk pumping
//                              processEvents every 10 files — the
//                              last real re-entrancy surface in the
//                              app).
//   • startIntegrityPass     — the t+6.5 s startup repair (was
//                              MainWindow::runStartupIntegrityPass):
//                              fake-done requeue, failed-row retry,
//                              one-time junk-text audit, hash
//                              backfill.
//   • startPurgeNonIndexable — the one-time legacy-row cleanup (was
//                              MainWindow::purgeNonIndexableRows,
//                              500-row batched commits pumping the UI
//                              event loop between batches).
//
// Every async job runs on QtConcurrent with its OWN sqlite connection
// (FULLMUTEX + busy_timeout, the same contract the v1.7.11 auto-scan
// worker established) — the UI thread never walks, never hashes,
// never pumps an event loop for any of this. All statements are
// prepared/bound: the two `QString("...WHERE id=%1").arg(id)` string
// interpolations the 2026-09 audit flagged are gone.
//
// Worker lambdas capture NOTHING but values (params, a shared result
// struct, a generation number) — a controller destroyed mid-job
// leaves the worker touching only dead-value copies, never members.
// Each run gets its own QFutureWatcher; its finished handler carries
// that run's generation, so a watchdog-restarted scan (v1.7.3) can
// never be torn down by the STALE job's completion.
//
// The controller is QtCore-only (no QtWidgets, no MainWindow include)
// so the headless test suite can drive the REAL pipeline against a
// temp database — the same wiring-test standard tst_Wiring set for
// the extraction/embedding pipelines.
//
// Synchronous folder purge (a Settings action, small and bounded)
// stays static + explicit: it takes the caller's Database& and runs
// on the CALLER'S thread.

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QFutureWatcher>

#include <atomic>
#include <memory>

namespace DocuSearch {

class Database;

struct ScanStats {
    int newFiles     = 0;   // rows inserted (folderScan: files indexed)
    int updatedFiles = 0;   // rows re-queued (changed files)
    int removedFiles = 0;   // rows pruned (auto-scan pass 2)
    int unavailable  = 0;   // folders skipped (offline roots)
    int skipped      = 0;   // folderScan: files rejected by the ext gates
    int hashedFiles  = 0;   // folderScan: fingerprints computed now
};

struct IntegrityResult {
    int  requeued         = 0;   // fake-done rows dropped to metadata_only
    int  failedRequeued   = 0;   // failed rows given their per-launch retry
    int  junkRequeued     = 0;   // junk-text audit rows requeued
    int  hashed           = 0;   // hashes backfilled this pass
    bool junkAuditScanned = false;  // the one-time audit actually ran
};

// Everything a scan needs, by value — a worker lambda captures this
// and NOTHING else (no `this`, no UI pointers).
struct ScanParams {
    QStringList folders;             // roots to walk (auto-scan)
    QStringList excludedFolders;     // user's Settings → excluded roots
    QStringList excludedExtensions;  // raw user list (normalized inside)
    QString     dbPath;              // own worker connection opens this
    bool        hashEnabled = false; // Settings → compute file hashes
};

class ScanPipelineController : public QObject {
    Q_OBJECT
public:
    explicit ScanPipelineController(QObject* parent = nullptr);
    ~ScanPipelineController() override;

    // ---- async jobs (worker thread, own sqlite connection) ----

    // Hourly / startup diff scan. Returns false when a previous scan
    // is still healthy (< 30 min old, the v1.7.3 watchdog window);
    // a scan stuck LONGER than that is re-armed here (with a fresh
    // generation) and true is returned.
    bool startAutoScan(const ScanParams& params);
    bool isAutoScanRunning() const { return autoScanRunning_.load(); }
    qint64 autoScanElapsedMs() const;

    // Add-Folder / new-drive ingest. Single-flight with an FIFO queue:
    // requests landing while a scan runs start in order when it ends
    // (the old code was synchronous, so requests serialized naturally —
    // the queue preserves that without blocking the UI thread).
    void startFolderScan(const QString& folder, const ScanParams& params);
    int  queuedFolderScans() const { return pendingFolders_.size(); }

    // Startup integrity pass. junkAuditNeeded gates the ONE-TIME
    // junk-text audit (the caller persists its flag when the result
    // says the audit actually scanned).
    void startIntegrityPass(const ScanParams& params, bool junkAuditNeeded);
    bool isIntegrityRunning() const { return integrityRunning_.load(); }

    void startPurgeNonIndexable(const QString& dbPath);

    // True while ANY async job is on a worker thread. The database
    // reset flow waits (bounded) on this before deleting the file —
    // each job holds its own sqlite connection, and on Windows an
    // open connection blocks the delete (sharing violation).
    bool isBusy() const {
        return autoScanRunning_.load() || folderScanRunning_.load()
            || integrityRunning_.load() || purgeRunning_.load();
    }

    // ---- synchronous maintenance (CALLER'S thread, caller's Database) ----

    // v1.7.4: purge every index row under a folder (Settings removal).
    // Returns the number of rows removed.
    static qint64 purgeFolderFromIndex(Database& db, const QString& folder);

    // v1.7.7: one-time cleanup of every row whose extension is not in
    // kIndexableExtensions. Prepared-once, 500-row batched commits
    // (kill-proofing kept from v1.7.8).
    static int purgeNonIndexableRows(Database& db);

signals:
    void autoScanFinished(const DocuSearch::ScanStats& stats);
    void folderScanFinished(int indexed, int skipped, int hashed);
    void integrityFinished(const DocuSearch::IntegrityResult& result);
    void purgeNonIndexableFinished(int purged);

private:
    void pumpPendingFolderScan();

    std::atomic<bool> autoScanRunning_{false};
    std::atomic<bool> folderScanRunning_{false};
    std::atomic<bool> integrityRunning_{false};
    std::atomic<bool> purgeRunning_{false};

    // Watchdog clock + generation — UI thread only (start/finish).
    // The generation guard: a watchdog-restarted scan increments the
    // generation, so the STALE job's finished handler (still armed on
    // its own per-run watcher) can never clear the flag or emit a
    // result that belongs to the past.
    qint64 autoScanStartedMs_ = 0;
    qint64 autoScanGen_       = 0;

    // FIFO of folder scans waiting for the current one (UI thread only).
    QStringList pendingFolders_;
    QList<ScanParams> pendingParams_;
};

} // namespace DocuSearch

Q_DECLARE_METATYPE(DocuSearch::ScanStats)
Q_DECLARE_METATYPE(DocuSearch::IntegrityResult)
