#pragma once

// ============================================================
// Database.h - RAII SQLite wrapper with WAL, FTS5, thread-safe
// ============================================================

#include <QObject>
#include <QString>
#include <QMutex>
#include <memory>

struct sqlite3;
struct sqlite3_stmt;

namespace DocuSearch {

class DatabaseMutexGuard;

class Database : public QObject {
    Q_OBJECT
public:
    explicit Database(QObject* parent = nullptr);
    ~Database() override;

    // Open or create the database file. Returns true on success.
    bool open(const QString& path, QString* err = nullptr);

    // Close & cleanup. Safe to call multiple times.
    void close();

    // True if currently open.
    bool isOpen() const { return db_ != nullptr; }

    // Execute raw SQL (no result). Returns true on success.
    bool exec(const QString& sql, QString* err = nullptr);

    // Begin/commit/rollback transaction with SAVEPOINT support for nesting
    bool begin();
    bool commit();
    bool rollback();

    // Thread-safe access to raw pointer (DO NOT use directly)
    sqlite3* raw() { return db_; }
    QMutex& getMutex() { return dbMutex_; }

    // Path of currently open DB
    QString path() const { return path_; }

signals:
    void logMessage(const QString& msg);

private:
    QMutex dbMutex_;  // PHASE 1: Mutex for thread-safe access
    sqlite3* db_  = nullptr;
    QString  path_;
    int      txnDepth_ = 0;

    // Close WITHOUT locking. open() and close() both already hold
    // dbMutex_ when they call this — open() must NOT re-enter the
    // public close() (which locks the same non-recursive mutex again
    // on the same thread), or the background database open deadlocks
    // forever: the splash fades, the window is never constructed, and
    // the app keeps running in Task Manager with nothing on screen.
    void closeLocked();

    QString getSavepointName(int depth) const;
};

// Convenience RAII transaction guard.
class TransactionGuard {
public:
    explicit TransactionGuard(Database& db, bool* ok = nullptr);
    ~TransactionGuard();
    void commit();
    void rollback();
private:
    Database& db_;
    bool      committed_ = false;
    bool*     ok_;
};

} // namespace DocuSearch
