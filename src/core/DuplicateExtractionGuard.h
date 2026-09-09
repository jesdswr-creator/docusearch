#pragma once

// ============================================================
// DuplicateExtractionGuard.h - One in-flight extract per file
// ============================================================
// Stops the FileWatcher and the extraction session from extracting
// the same file_id at the same time (the re-entrancy class that
// used to double-write DocumentText).
// ============================================================

#include <QSet>
#include <QMutex>
#include <QtGlobal>

namespace DocuSearch {

class DuplicateExtractionGuard {
public:
    // Returns true if this file should be extracted now.
    bool tryEnqueue(qint64 fileId);

    void dequeue(qint64 fileId);

    bool isQueued(qint64 fileId) const;

    void clear();

    int size() const;

private:
    mutable QMutex m_mutex;
    QSet<qint64> m_active;
};

} // namespace DocuSearch
