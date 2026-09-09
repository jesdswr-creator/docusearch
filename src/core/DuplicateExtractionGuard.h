#pragma once

// ============================================================
// DuplicateExtractionGuard.h - Prevent double-extraction
// ============================================================
// CRITICAL: Prevents same file from being extracted twice
// when both FileWatcher and auto-scan trigger simultaneously
// ============================================================

#include <QSet>
#include <QPair>
#include <QMutex>
#include <QString>

namespace DocuSearch {

class DuplicateExtractionGuard {
public:
    // Returns true if file should be extracted, false if already queued
    bool tryEnqueue(int fileId, int sessionId);
    
    // Remove when extraction completes
    void dequeue(int fileId, int sessionId);
    
    // Check if file is currently being extracted
    bool isQueued(int fileId, int sessionId) const;
    
    // Clear all (on shutdown)
    void clear();
    
private:
    mutable QMutex m_mutex;
    QSet<QPair<int, int>> m_activeExtractions;  // (fileId, sessionId)
};

} // namespace DocuSearch
