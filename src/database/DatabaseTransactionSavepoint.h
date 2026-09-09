#pragma once

// ============================================================
// DatabaseTransactionSavepoint.h - Fix for nested transactions
// ============================================================
// CRITICAL: Replaces nested BEGIN with SAVEPOINT to allow
// partial rollback. This fixes the v1.7.20 stale-capture bug.
// ============================================================

#include <QString>
#include <QMutex>

struct sqlite3;

namespace DocuSearch {

class DatabaseTransactionManager {
public:
    explicit DatabaseTransactionManager(sqlite3* db, QMutex& mutex);
    ~DatabaseTransactionManager();
    
    // Begin a transaction (or savepoint if nested)
    bool begin(QString* err = nullptr);
    
    // Commit (only commits outermost, others are no-ops)
    bool commit(QString* err = nullptr);
    
    // Rollback (rolls back to nearest savepoint or transaction)
    bool rollback(QString* err = nullptr);
    
    // Current nesting depth
    int depth() const { return m_txnDepth; }
    
private:
    sqlite3* m_db;
    QMutex& m_mutex;
    int m_txnDepth = 0;
    
    QString getSavepointName(int depth) const;
    bool execSQL(const QString& sql, QString* err);
};

} // namespace DocuSearch
