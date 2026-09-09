#pragma once

// ============================================================
// DatabaseMutex.h - Thread-safe database access guard
// ============================================================
// CRITICAL: Prevents race conditions when sqlite3* is accessed
// from multiple threads simultaneously
// ============================================================

#include <QMutex>
#include <QMutexLocker>

struct sqlite3;

namespace DocuSearch {

class DatabaseMutexGuard {
public:
    DatabaseMutexGuard(QMutex& mutex, sqlite3*& db)
        : m_lock(mutex), m_db(db)
    {
    }
    
    sqlite3* get() const { return m_db; }
    sqlite3* operator->() const { return m_db; }
    
private:
    QMutexLocker m_lock;
    sqlite3*& m_db;
};

} // namespace DocuSearch
