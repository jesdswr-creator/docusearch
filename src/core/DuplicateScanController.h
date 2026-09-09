// ============================================================
// DuplicateScanController.h — duplicate finder, headless
// ============================================================
//
// v1.7.24: the scan phase of MainWindow::onDetectDuplicates (~350 of
// its 448 lines) moved here. The five passes — DB pull + ghost
// detection, same-file identity collapse, size pre-grouping, content
// fingerprinting (with write-back), survivors-only grouping — plus
// the display-time re-verification and the group ordering now run on
// a QtConcurrent worker with its OWN sqlite connection. The UI thread
// no longer hashes files and no longer pumps processEvents() to keep
// the progress dialog alive: progress arrives as queued signals and
// cancel is a shared atomic flag the worker polls (the QProgressDialog
// wasCanceled pump is gone).
//
// The controller is QtCore-only so the headless test suite can drive
// the REAL scan against temp files + a temp database.
//
// MainWindow keeps what a window should own: the tier gate, the
// single-flight guard, the progress dialog, and rendering the result
// (results pane, summary messages, status bar).

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QFutureWatcher>

#include "../core/Types.h"

#include <atomic>
#include <memory>

namespace DocuSearch {

struct DuplicateScanResult {
    QList<SearchHit> hits;          // grouped, ordered, re-verified
    QStringList      groupKeys;     // size:hash key per hit (same order)

    qsizetype   candidateCount   = 0;  // rows compared (post-collapse)
    int         scanned          = 0;  // rows pulled from the index
    int         skippedMissing   = 0;  // rows whose file is gone
    int         staleRows        = 0;  // one physical file, two rows
    int         droppedSingletons = 0; // partner vanished mid-run
    int         unreadable       = 0;  // could not be read to hash
    int         hashedNow        = 0;  // fingerprints computed this run
    int         hashTotal        = 0;  // files that needed fingerprinting
    int         groupCount       = 0;
    bool        cancelled        = false;

    QStringList stalePaths;         // ghost rows — caller purges via
                                    // FileRepository on the UI thread
};

class DuplicateScanController : public QObject {
    Q_OBJECT
public:
    explicit DuplicateScanController(QObject* parent = nullptr);
    ~DuplicateScanController() override;

    // Runs the full scan on a worker thread. `dbPath` is opened on the
    // worker (FULLMUTEX + busy_timeout 5000) for the candidate pull and
    // the hash write-back.
    void start(const QString& dbPath);
    void cancel();
    bool isRunning() const { return running_.load(); }

    // Live hashing progress for the UI's progress dialog: the worker
    // has no `this` in its capture list (a controller destroyed
    // mid-job must be safe), so instead of cross-thread signals the
    // dialog polls these atomic counters every ~250 ms.
    int hashProgressDone() const;
    int hashProgressTotal() const;

    // v1.7.13: ONE identity function for "which physical file is this?".
    // Used by the same-file collapse (pass 2) AND by the final guard
    // before display, so the two passes can never disagree. Covers the
    // spellings that used to slip through and make one physical file
    // pair with itself ("single file is showing as duplicates"):
    //   - canonical resolution (junctions, symlinks, mapped drives)
    //   - extended-length prefixes: \\?\D:\... and \\?\UNC\server\...
    //   - dot segments and mixed separators (cleanPath on the fallback)
    //   - Windows case-insensitivity
    static QString pathIdentityKey(const QString& p);

    // v1.7.14: move a file into a user-chosen folder, keeping its name
    // ("name (2).ext" on collision). QFile::rename fails across volumes
    // (ERROR_NOT_SAME_DEVICE), so fall back to copy-then-remove — and
    // the copy is size-verified before the original is unlinked, so a
    // partial copy can never destroy the only other copy of the content.
    static bool moveFileKeepingName(const QString& src,
                                    const QString& destDir);

    // v1.7.13/v1.7.15: pick the copies to remove — for every group with
    // >= 2 files that still exist, the NEWEST copy is kept and the rest
    // are doomed. Returns indices into `hits`; never dooms a group's
    // last survivor. *reclaimBytes / *groupsActed are optional outs.
    static QList<int> selectDoomedCopies(const QList<SearchHit>& hits,
                                         const QStringList& groupKeys,
                                         qint64* reclaimBytes = nullptr,
                                         int* groupsActed = nullptr);

signals:
    void finished(const DocuSearch::DuplicateScanResult& result);

private:
    QFutureWatcher<void> watcher_;
    std::shared_ptr<std::atomic<bool>> cancelFlag_;
    std::shared_ptr<DuplicateScanResult> result_;
    std::shared_ptr<std::atomic<int>> hashDone_;
    std::shared_ptr<std::atomic<int>> hashTotal_;
    std::atomic<bool> running_{false};
};

} // namespace DocuSearch

Q_DECLARE_METATYPE(DocuSearch::DuplicateScanResult)
