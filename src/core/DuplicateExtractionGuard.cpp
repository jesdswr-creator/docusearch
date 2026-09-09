// ============================================================
// DuplicateExtractionGuard.cpp
// ============================================================

#include "DuplicateExtractionGuard.h"
#include "Logger.h"
#include <QMutexLocker>

namespace DocuSearch {

bool DuplicateExtractionGuard::tryEnqueue(qint64 fileId) {
    if (fileId <= 0) return false;
    QMutexLocker lock(&m_mutex);
    if (m_active.contains(fileId)) {
        DS_WARN("Extraction",
            QString("File %1 already in flight — skipping duplicate extract")
                .arg(fileId));
        return false;
    }
    m_active.insert(fileId);
    return true;
}

void DuplicateExtractionGuard::dequeue(qint64 fileId) {
    if (fileId <= 0) return;
    QMutexLocker lock(&m_mutex);
    m_active.remove(fileId);
}

bool DuplicateExtractionGuard::isQueued(qint64 fileId) const {
    QMutexLocker lock(&m_mutex);
    return m_active.contains(fileId);
}

void DuplicateExtractionGuard::clear() {
    QMutexLocker lock(&m_mutex);
    m_active.clear();
}

int DuplicateExtractionGuard::size() const {
    QMutexLocker lock(&m_mutex);
    return m_active.size();
}

} // namespace DocuSearch
