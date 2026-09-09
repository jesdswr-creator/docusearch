// ============================================================
// DuplicateScanController.cpp — implementation
// ============================================================
// The five passes preserve the v1.7.12/v1.7.13/v1.7.16 semantics
// exactly (they were battle-tested against real duplicate reports);
// only the threading and the progress transport changed:
//   • worker thread + own sqlite connection (was: UI thread on the
//     main connection, pumping processEvents),
//   • queued progress signals + an atomic cancel flag (was: the
//     progress dialog's own event-loop pump),
//   • ghost-row purge is RETURNED to the caller (was: purged
//     mid-scan on the UI thread) — the candidates are excluded from
//     the run either way, so grouping is unchanged.

#include "DuplicateScanController.h"

#include "Constants.h"
#include "FileUtils.h"
#include "Logger.h"
#include "StorageHealth.h"

#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QtConcurrent>

// The worker bodies drive sqlite directly (own connections) — the C
// header is needed in this TU (v1.7.24 CI lesson, same as
// ScanPipelineController.cpp).
#include <sqlite3.h>

#include <algorithm>

namespace DocuSearch {

DuplicateScanController::DuplicateScanController(QObject* parent)
    : QObject(parent) {
    connect(&watcher_, &QFutureWatcher<void>::finished, this, [this]() {
        running_ = false;
        if (result_)
            emit finished(*result_);
        result_.reset();
    });
}

DuplicateScanController::~DuplicateScanController() = default;

// ============================================================
// pathIdentityKey (from MainWindow.cpp, v1.7.13)
// ============================================================

QString DuplicateScanController::pathIdentityKey(const QString& p) {
    QString key = QFileInfo(p).canonicalFilePath();
    if (key.isEmpty()) key = QDir::cleanPath(p);
#ifdef Q_OS_WIN
    if (key.startsWith(QStringLiteral("\\\\?\\")) ||
        key.startsWith(QStringLiteral("//?/"))) {
        key.remove(0, 4);
        if (key.startsWith(QStringLiteral("UNC")) && key.size() > 3 &&
            (key.at(3) == QLatin1Char('\\') || key.at(3) == QLatin1Char('/')))
            key = QStringLiteral("//") + key.mid(4);
    }
    key.replace(QLatin1Char('\\'), QLatin1Char('/'));
    key = key.toLower();
#else
    key = key.toLower();
#endif
    return key;
}

// ============================================================
// moveFileKeepingName (from MainWindow.cpp, v1.7.14)
// ============================================================

bool DuplicateScanController::moveFileKeepingName(const QString& src,
                                                  const QString& destDir) {
    const QFileInfo fi(src);
    if (!fi.exists() || !QFileInfo(destDir).isDir()) return false;
    const QString base   = fi.completeBaseName();
    const QString suffix = fi.suffix();
    QString candidate = destDir + "/" + fi.fileName();
    if (QFileInfo::exists(candidate)) {
        for (int n = 2; ; ++n) {
            if (n > 999) return false;   // pathological — never spin forever
            candidate = destDir + "/" + base + " (" + QString::number(n) + ")" +
                        (suffix.isEmpty() ? QString() : "." + suffix);
            if (!QFileInfo::exists(candidate)) break;
        }
    }
    if (QFile::rename(src, candidate)) return true;
    if (!QFile::copy(src, candidate))  return false;
    if (QFileInfo(candidate).size() != fi.size()) {
        QFile::remove(candidate);        // copy unverifiable — keep original
        return false;
    }
    return QFile::remove(src);
}

// ============================================================
// selectDoomedCopies (from MainWindow::onDeleteDuplicateCopies)
// ============================================================

QList<int> DuplicateScanController::selectDoomedCopies(
        const QList<SearchHit>& hits, const QStringList& groupKeys,
        qint64* reclaimBytes, int* groupsActed) {
    QList<int> doomed;
    if (reclaimBytes) *reclaimBytes = 0;
    if (groupsActed)  *groupsActed  = 0;
    if (hits.isEmpty() || groupKeys.size() != hits.size()) return doomed;

    // Group only by members that still exist on disk.
    QHash<QString, QList<int>> groups;
    for (int i = 0; i < hits.size(); ++i) {
        if (!QFileInfo::exists(hits[i].path)) continue;
        groups[groupKeys[i]].append(i);
    }

    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        const QList<int>& members = it.value();
        if (members.size() < 2) continue;      // never the last copy
        // Keep the NEWEST copy (highest live mtime; tie -> first).
        int keep = members[0];
        qint64 keepMtime = QFileInfo(hits[keep].path)
                               .lastModified().toSecsSinceEpoch();
        for (int m = 1; m < members.size(); ++m) {
            const qint64 mt = QFileInfo(hits[members[m]].path)
                                  .lastModified().toSecsSinceEpoch();
            if (mt > keepMtime) { keep = members[m]; keepMtime = mt; }
        }
        for (int m : members) {
            if (m == keep) continue;
            if (reclaimBytes)
                *reclaimBytes += QFileInfo(hits[m].path).size();
            doomed.append(m);
        }
        if (groupsActed) ++(*groupsActed);
    }
    return doomed;
}

// ============================================================
// The scan itself
// ============================================================

void DuplicateScanController::start(const QString& dbPath) {
    bool expected = false;   // CAS takes T& — must stay mutable
    if (!running_.compare_exchange_strong(expected, true)) return;

    cancelFlag_ = std::make_shared<std::atomic<bool>>(false);
    auto result = std::make_shared<DuplicateScanResult>();
    result_     = result;
    auto cancelledFlag = cancelFlag_;
    auto hashDone  = std::make_shared<std::atomic<int>>(0);
    auto hashTotal = std::make_shared<std::atomic<int>>(0);
    hashDone_  = hashDone;
    hashTotal_ = hashTotal;

    QFuture<void> future = QtConcurrent::run(
        [dbPath, result, cancelledFlag, hashDone, hashTotal]() {
        sqlite3* workerDb = nullptr;
        if (sqlite3_open_v2(dbPath.toUtf8().constData(), &workerDb,
                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX,
                            nullptr) != SQLITE_OK) {
            return;
        }
        sqlite3_exec(workerDb, "PRAGMA busy_timeout = 5000;",
                     nullptr, nullptr, nullptr);

        // Duplicate detection covers everything the app indexes:
        // documents AND images (identical scanned jpg/png/tif pairs
        // are real duplicates, and are the most common kind users
        // actually have).
        QString typeList;
        for (const QString& t : Constants::kIndexableExtensions) {
            if (!typeList.isEmpty()) typeList += QLatin1Char(',');
            typeList += QString("'%1'").arg(t.toLower());
        }

        // ---- Pass 1: pull every indexable row (hashed or not) ----
        // No `hash != ''` filter any more: an unhashed row is a
        // perfectly good duplicate candidate, we just have to do the
        // work ourselves below.
        struct Cand {
            qint64  id = 0;
            QString path, filename, extension;
            qint64  size = 0;
            qint64  rowMtime = 0;
            QString storedHash;
        };
        QList<Cand> cands;

        sqlite3_stmt* s = nullptr;
        const QString sql = QString(
            "SELECT id, path, filename, extension, size, "
            "       modified_date, COALESCE(hash, '') "
            "FROM Files WHERE lower(extension) IN (%1);").arg(typeList);
        if (sqlite3_prepare_v2(workerDb, sql.toUtf8().constData(),
                               -1, &s, nullptr) == SQLITE_OK) {
            while (sqlite3_step(s) == SQLITE_ROW) {
                Cand c;
                c.id = sqlite3_column_int64(s, 0);
                auto col = [&](int i) {
                    const unsigned char* p = sqlite3_column_text(s, i);
                    return p ? QString::fromUtf8(
                        reinterpret_cast<const char*>(p)) : QString();
                };
                c.path       = col(1);
                c.filename   = col(2);
                c.extension  = col(3);
                c.size       = sqlite3_column_int64(s, 4);
                c.rowMtime   = sqlite3_column_int64(s, 5);
                c.storedHash = col(6);

                // Index rows can outlive their files (deleted after
                // scanning, or MOVED and the old row not yet pruned).
                // A "duplicate" pointing at nothing helps nobody.
                if (!QFileInfo::exists(c.path)) {
                    ++result->skippedMissing;
                    // v1.7.24: purge moved to the caller — collect the
                    // ghost rows whose drive is reachable and let the
                    // UI thread run the FileRepository purge after the
                    // scan (grouping is unaffected: the row is
                    // excluded from the candidates either way).
                    if (storageRootReachable(c.path))
                        result->stalePaths.append(c.path);
                    continue;
                }
                cands.append(c);

                if ((++result->scanned % 500) == 0 &&
                    cancelledFlag->load()) {
                    result->cancelled = true;
                    break;
                }
            }
            sqlite3_finalize(s);
        }

        if (cancelledFlag->load()) {
            result->cancelled = true;
            sqlite3_close(workerDb);
            return;
        }

        // ---- Pass 2: collapse rows that are the SAME physical file ----
        // The same file can sit in Files more than once — after
        // aggressive re-scans, or under two spellings of one path:
        //   • mixed separators   D:\Docs\a.pdf  vs  D:/Docs/a.pdf
        //   • dot segments       D:\Docs\a.pdf  vs  D:\Docs\.\a.pdf
        //   • junctions/symlinks D:\Real\a.pdf  vs  D:\Link\a.pdf
        //   • overlapping roots  (a folder added as root AND as child)
        // Canonicalization resolves all four. One physical file must
        // never pass as a "group of two" with itself.
        int staleRows = 0;
        {
            // v1.7.13: collapse runs through pathIdentityKey(), so the
            // extended-length prefixes, dot segments and separator
            // spellings this pass used to miss cannot self-pair either.
            QSet<QString> seenPaths;
            QList<Cand> unique;
            unique.reserve(cands.size());
            for (const Cand& c : cands) {
                const QString key = pathIdentityKey(c.path);
                if (seenPaths.contains(key)) { ++staleRows; continue; }
                seenPaths.insert(key);
                unique.append(c);
            }
            cands = std::move(unique);
        }
        result->staleRows      = staleRows;
        result->candidateCount = cands.size();

        // ---- Pass 3: size pre-grouping (free, and exact) ----
        // Two files of different sizes cannot be byte-identical, so a
        // size that occurs exactly once needs no I/O at all. On a
        // typical index this removes 90 %+ of the work, which is what
        // makes live hashing affordable.
        // Use the CURRENT on-disk size, not the indexed one: a row
        // whose file changed since scanning must still be grouped
        // (previously such rows were dropped outright).
        QHash<qint64, int> sizeCount;
        QList<qint64> liveSize;
        QList<qint64> liveMtime;
        liveSize.reserve(cands.size());
        liveMtime.reserve(cands.size());
        for (const Cand& c : cands) {
            const QFileInfo fi(c.path);
            const qint64 sz = fi.size();
            liveSize.append(sz);
            liveMtime.append(fi.lastModified().toSecsSinceEpoch());
            ++sizeCount[sz];
        }

        // ---- Pass 4: fingerprint the survivors ----
        QList<SearchHit> hits;
        QStringList hashes;
        int hashedNow = 0, unreadable = 0;

        QList<int> toHash;
        for (int i = 0; i < cands.size(); ++i)
            if (sizeCount.value(liveSize[i], 0) >= 2) toHash.append(i);

        const int totalToHash = toHash.size();
        result->hashTotal     = totalToHash;
        hashTotal->store(totalToHash);   // published for the UI's poll
        for (int n = 0; n < totalToHash; ++n) {
            if (cancelledFlag->load()) { result->cancelled = true; break; }
            const int i = toHash[n];
            const Cand& c = cands[i];

            // Reuse the stored fingerprint ONLY if it still describes
            // the file on disk (2 s mtime tolerance for FAT's coarse
            // timestamps). Otherwise re-hash — a stale hash used to
            // mean "drop this file", which silently hid every freshly
            // copied duplicate.
            QString h;
            // v1.7.13: a row with NO mtime is never trusted either —
            // size alone cannot distinguish an edited file from an
            // untouched one. The write-back below heals such rows on
            // this very run, so the cost is one hashing pass, once.
            const bool storedIsFresh =
                !c.storedHash.isEmpty() &&
                c.rowMtime > 0 &&
                liveSize[i] == c.size &&
                qAbs(liveMtime[i] - c.rowMtime) <= 2;
            if (storedIsFresh) {
                h = c.storedHash;
            } else {
                // Same 64 MB cap as every other hashing path, so a
                // fingerprint computed here is comparable with one
                // written by the scanners.
                h = FileUtils::sha256OfFile(c.path, 64 * 1024 * 1024);
                if (h.isEmpty()) { ++unreadable; continue; }
                ++hashedNow;
                // Write the whole fingerprint triple back — hash AND
                // the live size/mtime it was computed from. Writing
                // only the hash left drifted rows stale (the row still
                // claimed the old size), so storedIsFresh stayed false
                // and every run re-hashed the same files; the row also
                // disagreed with itself for any future consumer.
                sqlite3_stmt* u = nullptr;
                if (sqlite3_prepare_v2(workerDb,
                        "UPDATE Files SET hash = ?1, size = ?2, "
                        "modified_date = ?3 WHERE id = ?4;",
                        -1, &u, nullptr) == SQLITE_OK) {
                    sqlite3_bind_text(u, 1, h.toUtf8().constData(), -1,
                                      SQLITE_TRANSIENT);
                    sqlite3_bind_int64(u, 2, liveSize[i]);
                    sqlite3_bind_int64(u, 3, liveMtime[i]);
                    sqlite3_bind_int64(u, 4, c.id);
                    sqlite3_step(u);
                    sqlite3_finalize(u);
                }
            }

            SearchHit sh;
            sh.fileId       = c.id;
            sh.path         = c.path;
            sh.filename     = c.filename;
            sh.extension    = c.extension;
            sh.size         = liveSize[i];
            sh.modifiedDate = QDateTime::fromSecsSinceEpoch(liveMtime[i]);
            hits.append(sh);
            // Group key = exact size + fingerprint. The fingerprint is
            // capped at 64 MB (same cap every hashing path uses), so
            // two DIFFERENT files larger than the cap that happen to
            // share their first 64 MB — e.g. two long videos-of-scans
            // exported from the same tool, or two PDFs with identical
            // front matter — would otherwise collide into a false
            // "duplicate". Qualifying the key with the byte-exact size
            // makes that impossible without re-reading whole files.
            hashes.append(QStringLiteral("%1:%2")
                              .arg(liveSize[i]).arg(h));

            // The UI-thread progress dialog polls this atomic (a QTimer
            // every ~250 ms) — no cross-thread signal, no `this` in the
            // worker's capture list.
            hashDone->store(n + 1);
        }
        result->hashedNow = hashedNow;
        result->unreadable = unreadable;

        // ---- Pass 5: keep only hashes with >= 2 SURVIVING files ----
        // A lone file whose partner was deleted must never render as
        // a "duplicate" of something that no longer exists.
        int droppedSingletons = 0;
        {
            QHash<QString, int> groupSize;
            for (const QString& hs : hashes) ++groupSize[hs];
            QList<SearchHit> kept;
            QStringList keptHashes;
            for (int i = 0; i < hits.size(); ++i) {
                if (groupSize.value(hashes[i], 0) >= 2) {
                    kept.append(hits[i]);
                    keptHashes.append(hashes[i]);
                } else {
                    ++droppedSingletons;
                }
            }
            hits   = std::move(kept);
            hashes = std::move(keptHashes);
        }

        // Display-time re-verification: files can move or vanish WHILE
        // the walk runs. Re-verify and re-run the survivors-only
        // grouping so a file whose partner vanished mid-walk can never
        // render as a pair whose second file is gone.
        // v1.7.13: the SAME identity check also runs here as a last
        // line of defense — if two surviving rows still resolve to one
        // physical file (a spelling canonicalization cannot unify),
        // the shadow row is dropped and the recount below makes sure
        // a group reduced to one file disappears instead of showing
        // "a duplicate" that has no partner.
        {
            QList<SearchHit> verified;
            QStringList verifiedHashes;
            verified.reserve(hits.size());
            QSet<QString> seenIdentity;
            for (int i = 0; i < hits.size(); ++i) {
                if (!QFileInfo::exists(hits[i].path)) {
                    ++droppedSingletons;   // keep the summary honest
                    continue;
                }
                const QString ident = pathIdentityKey(hits[i].path);
                if (seenIdentity.contains(ident)) {
                    ++staleRows;           // one file, two rows: shadow
                    continue;
                }
                seenIdentity.insert(ident);
                verified.append(hits[i]);
                verifiedHashes.append(hashes[i]);
            }
            QHash<QString, int> aliveSize;
            for (const QString& hs : verifiedHashes) ++aliveSize[hs];
            QList<SearchHit> paired;
            QStringList pairedHashes;
            for (int i = 0; i < verified.size(); ++i) {
                if (aliveSize.value(verifiedHashes[i], 0) >= 2) {
                    paired.append(verified[i]);
                    pairedHashes.append(verifiedHashes[i]);
                }
            }
            hits   = std::move(paired);
            hashes = std::move(pairedHashes);
        }
        result->droppedSingletons = droppedSingletons;
        result->staleRows         = staleRows;

        // Order the output so members of a group sit together.
        {
            QList<int> idx;
            idx.reserve(hits.size());
            for (int i = 0; i < hits.size(); ++i) idx.append(i);
            std::sort(idx.begin(), idx.end(), [&](int a, int b) {
                if (hashes[a] != hashes[b]) return hashes[a] < hashes[b];
                return hits[a].filename.localeAwareCompare(
                           hits[b].filename) < 0;
            });
            QList<SearchHit> sortedHits;
            QStringList sortedHashes;
            sortedHits.reserve(hits.size());
            for (const int i : idx) {
                sortedHits.append(hits[i]);
                sortedHashes.append(hashes[i]);
            }
            hits   = std::move(sortedHits);
            hashes = std::move(sortedHashes);
        }

        int groupCount = 0;
        QString lastHash;
        for (const QString& hs : hashes) {
            if (hs != lastHash) { ++groupCount; lastHash = hs; }
        }

        result->hits      = std::move(hits);
        result->groupKeys = std::move(hashes);
        result->groupCount = groupCount;

        sqlite3_close(workerDb);
    });

    watcher_.setFuture(future);
}

void DuplicateScanController::cancel() {
    if (cancelFlag_) cancelFlag_->store(true);
}

int DuplicateScanController::hashProgressDone() const {
    return hashDone_ ? hashDone_->load() : 0;
}

int DuplicateScanController::hashProgressTotal() const {
    return hashTotal_ ? hashTotal_->load() : 0;
}

} // namespace DocuSearch
