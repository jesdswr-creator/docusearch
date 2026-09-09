// ============================================================
// DuplicateExtractionGuard.cpp
// ============================================================

#include "DuplicateExtractionGuard.h"
#include "Logger.h"
#include <QMutexLocker>

namespace DocuSearch {

bool DuplicateExtractionGuard::tryEnqueue(int fileId, int sessionId) {
    QMutexLocker lock(&m_mutex);
    
    QPair<int, int> key(fileId, sessionId);
    
    if (m_activeExtractions.contains(key)) {
        DS_WARN("Extraction",
            QString("File %1 (session %2) already queued, skipping duplicate").arg(fileId).arg(sessionId));
        return false;
    }
    
    m_activeExtractions.insert(key);
    return true;
}

void DuplicateExtractionGuard::dequeue(int fileId, int sessionId) {
    QMutexLocker lock(&m_mutex);
    m_activeExtractions.remove(QPair<int, int>(fileId, sessionId));
}

bool DuplicateExtractionGuard::isQueued(int fileId, int sessionId) const {
    QMutexLocker lock(&m_mutex);
    return m_activeExtractions.contains(QPair<int, int>(fileId, sessionId));
}

void DuplicateExtractionGuard::clear() {
    QMutexLocker lock(&m_mutex);
    m_activeExtractions.clear();
}

} // namespace DocuSearch
