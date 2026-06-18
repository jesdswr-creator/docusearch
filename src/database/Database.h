#pragma once

// ============================================================
// Database.h — RAII SQLite wrapper with WAL, FTS5, thread-safe
// ============================================================

#include <QObject>
#include <QString>
#include <QMutex>
#include <memory>

struct sqlite3;
struct sqlite3_stmt;

namespace DocuSearch {

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

    // Begin/commit/rollback transaction. Nested transactions flatten.
    bool begin();
    bool commit();
    bool rollback();

    // Returns the raw sqlite3* — use sparingly.
    sqlite3* raw() { return db_; }

    // Path of currently open DB
    QString path() const { return path_; }

signals:
    void logMessage(const QString& msg);

private:
    sqlite3* db_  = nullptr;
    QString  path_;
    int      txnDepth_ = 0;
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
