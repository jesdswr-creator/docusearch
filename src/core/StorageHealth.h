// ============================================================
// StorageHealth.h — shared filesystem/storage predicates
// ============================================================
//
// v1.7.24: these helpers used to live in MainWindow.cpp's anonymous
// namespace, where every pipeline extracted out of the god-object had
// to re-implement or forward to them. They are pure QtCore, used by
// MainWindow (search-result hygiene, watcher path gate), the scan
// pipeline (unavailable-folder detection) and the duplicate scanner
// (ghost-row detection) — so they live here, header-only, with ONE
// definition for every consumer.
//
// Header-only (inline): no .cpp, no link surface to maintain.

#pragma once

#include <QString>
#include <QStringList>
#include <QSet>
#include <QDir>
#include <QFileInfo>
#include <QList>

#include "Types.h"

namespace DocuSearch {

// v1.7.11: Normalize the user's "Excluded Extensions" list into a fast
// lookup set - trimmed, lowercased, leading dot stripped (".ISO", "iso"
// and " Iso " all mean the same thing). Shared by every ingest gate
// (Add-Folder scan, hourly scan, live watcher) so the setting finally
// takes effect: before this, the list was saved and round-tripped but
// consulted by NOTHING.
inline QSet<QString> normalizedExtSet(const QStringList& exts) {
    QSet<QString> out;
    out.reserve(exts.size());
    for (QString e : exts) {
        e = e.trimmed().toLower();
        while (e.startsWith('.')) e.remove(0, 1);
        if (!e.isEmpty()) out.insert(e);
    }
    return out;
}

// v1.7.4: True when the STORAGE ROOT of an absolute path is reachable.
// Used to tell "this file was deleted" apart from "the whole drive is
// offline": an unplugged USB drive or disconnected network share must
// NEVER cause index purges (the hourly scan skips unavailable folders
// for exactly the same reason). Only hide results for offline roots.
inline bool storageRootReachable(const QString& path) {
    if (path.isEmpty()) return false;
    const QString abs = QDir::toNativeSeparators(
        QFileInfo(path).absoluteFilePath());
    if (abs.startsWith(QLatin1String("\\\\"))) {
        // UNC: \\server\share\... — the share is the storage root.
        const QStringList parts = abs.split('\\', Qt::SkipEmptyParts);
        if (parts.size() < 2) return false;
        const QString root = QLatin1String("\\\\") + parts.at(0)
                           + QLatin1Char('\\') + parts.at(1);
        return QFileInfo::exists(root);
    }
    if (abs.size() >= 3 && abs.at(1) == QLatin1Char(':')) {
        return QFileInfo::exists(abs.left(3));   // e.g. "D:\"
    }
    return QFileInfo::exists(abs);
}

// v1.7.3: hide results whose file no longer exists (deleted or moved while
// the app was closed — the FileWatcher only catches changes while we run).
// The hourly scan prunes them from the index; this covers the gap until
// the next scan so users never click a result that opens to nothing.
// v1.7.4: returns the hidden paths so the caller can PURGE the rows whose
// drive is still reachable (self-healing index); rows on offline roots are
// only hidden — purging those would wipe live data.
inline QStringList hideStaleResults(QList<SearchHit>& hits) {
    QList<SearchHit> kept;
    kept.reserve(hits.size());
    QStringList removedPaths;
    for (const SearchHit& h : hits) {
        if (!h.path.isEmpty() && !QFileInfo::exists(h.path)) {
            removedPaths.append(h.path);
            continue;
        }
        kept.append(h);
    }
    hits = kept;
    return removedPaths;
}

} // namespace DocuSearch
