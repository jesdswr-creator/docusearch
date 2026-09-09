// ============================================================
// DatabaseTransactionSavepoint.cpp
// ============================================================

#include "DatabaseTransactionSavepoint.h"
#include "../core/Logger.h"
#include <sqlite3.h>
#include <QMutexLocker>

namespace DocuSearch {

DatabaseTransactionManager::DatabaseTransactionManager(sqlite3* db, QMutex& mutex)
    : m_db(db), m_mutex(mutex), m_txnDepth(0)
{
}

DatabaseTransactionManager::~DatabaseTransactionManager() {
    // Rollback any remaining transactions on destruction
    while (m_txnDepth > 0) {
        rollback();
    }
}

QString DatabaseTransactionManager::getSavepointName(int depth) const {
    return QString("sp_level_%1").arg(depth);
}

bool DatabaseTransactionManager::execSQL(const QString& sql, QString* err) {
    QMutexLocker lock(&m_mutex);
    
    char* errMsg = nullptr;
    int rc = sqlite3_exec(m_db, sql.toUtf8().constData(), nullptr, nullptr, &errMsg);
    
    if (rc != SQLITE_OK) {
        QString errStr = QString::fromUtf8(errMsg ? errMsg : "unknown error");
        if (err) *err = errStr;
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }
    
    return true;
}

bool DatabaseTransactionManager::begin(QString* err) {
    if (m_txnDepth == 0) {
        // Start new transaction
        if (!execSQL("BEGIN IMMEDIATE;", err)) {
            return false;
        }
        DS_DEBUG("Transaction", "BEGIN transaction");
    } else {
        // Create savepoint for nested transaction
        QString savepoint = getSavepointName(m_txnDepth);
        QString sql = QString("SAVEPOINT %1;").arg(savepoint);
        if (!execSQL(sql, err)) {
            return false;
        }
        DS_DEBUG("Transaction", QString("SAVEPOINT %1").arg(savepoint));
    }
    
    ++m_txnDepth;
    return true;
}

bool DatabaseTransactionManager::commit(QString* err) {
    if (m_txnDepth == 0) {
        if (err) *err = "No transaction to commit";
        return false;
    }
    
    --m_txnDepth;
    
    if (m_txnDepth == 0) {
        // Commit outermost transaction
        if (!execSQL("COMMIT;", err)) {
            return false;
        }
        DS_DEBUG("Transaction", "COMMIT transaction");
    } else {
        // Release savepoint
        QString savepoint = getSavepointName(m_txnDepth);
        QString sql = QString("RELEASE SAVEPOINT %1;").arg(savepoint);
        if (!execSQL(sql, err)) {
            return false;
        }
        DS_DEBUG("Transaction", QString("RELEASE SAVEPOINT %1").arg(savepoint));
    }
    
    return true;
}

bool DatabaseTransactionManager::rollback(QString* err) {
    if (m_txnDepth == 0) {
        if (err) *err = "No transaction to rollback";
        return false;
    }
    
    if (m_txnDepth == 1) {
        // Rollback entire transaction
        if (!execSQL("ROLLBACK;", err)) {
            return false;
        }
        DS_DEBUG("Transaction", "ROLLBACK transaction");
    } else {
        // Rollback to savepoint
        QString savepoint = getSavepointName(m_txnDepth - 1);
        QString sql = QString("ROLLBACK TO SAVEPOINT %1;").arg(savepoint);
        if (!execSQL(sql, err)) {
            return false;
        }
        DS_DEBUG("Transaction", QString("ROLLBACK TO SAVEPOINT %1").arg(savepoint));
    }
    
    --m_txnDepth;
    return true;
}

} // namespace DocuSearch
