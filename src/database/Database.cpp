// ============================================================
// Database.cpp
// ============================================================

#include "Database.h"
#include "../core/Logger.h"

#include <sqlite3.h>

namespace DocuSearch {

Database::Database(QObject* parent) : QObject(parent) {}

Database::~Database() { close(); }

bool Database::open(const QString& path, QString* err) {
    close();
    path_ = path;
    const int rc = sqlite3_open_v2(
        path.toUtf8().constData(), &db_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
        nullptr);
    if (rc != SQLITE_OK) {
        if (err) *err = QString("sqlite3_open failed: %1").arg(sqlite3_errmsg(db_));
        DS_ERROR("Database", QString("Open failed: %1").arg(sqlite3_errmsg(db_)));
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    // Recommended pragmas for performance and durability on office PCs.
    const QStringList pragmas = {
        "PRAGMA journal_mode = WAL;",
        "PRAGMA synchronous  = NORMAL;",
        "PRAGMA temp_store   = MEMORY;",
        "PRAGMA cache_size   = -65536;",   // ~64MB
        "PRAGMA mmap_size    = 268435456;", // 256MB
        "PRAGMA foreign_keys = ON;",
        "PRAGMA busy_timeout = 5000;",
        "PRAGMA encoding     = 'UTF-8';",
        "PRAGMA automatic_index = OFF;",
    };
    for (const auto& p : pragmas) {
        if (sqlite3_exec(db_, p.toUtf8().constData(), nullptr, nullptr, nullptr) != SQLITE_OK) {
            DS_WARN("Database", QString("Pragma failed: %1 -> %2").arg(p, sqlite3_errmsg(db_)));
        }
    }
    DS_INFO("Database", QString("Opened: %1 (SQLite %2)").arg(path, sqlite3_libversion()));
    return true;
}

void Database::close() {
    if (!db_) return;
    while (txnDepth_ > 0) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        --txnDepth_;
    }
    sqlite3_close(db_);
    db_ = nullptr;
    path_.clear();
}

bool Database::exec(const QString& sql, QString* err) {
    if (!db_) {
        if (err) *err = "Database not open";
        return false;
    }
    char* msg = nullptr;
    const int rc = sqlite3_exec(db_, sql.toUtf8().constData(), nullptr, nullptr, &msg);
    if (rc != SQLITE_OK) {
        if (err) *err = QString::fromUtf8(msg ? msg : "(null)");
        DS_ERROR("Database", QString("Exec failed: %1 | SQL: %2").arg(err ? *err : QString::fromUtf8(msg), sql));
        if (msg) sqlite3_free(msg);
        return false;
    }
    if (msg) sqlite3_free(msg);
    return true;
}

bool Database::begin() {
    if (!db_) return false;
    if (txnDepth_ == 0) {
        if (sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) != SQLITE_OK)
            return false;
    }
    ++txnDepth_;
    return true;
}

bool Database::commit() {
    if (!db_ || txnDepth_ == 0) return false;
    --txnDepth_;
    if (txnDepth_ == 0) {
        return sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK;
    }
    return true;
}

bool Database::rollback() {
    if (!db_ || txnDepth_ == 0) return false;
    if (txnDepth_ == 1) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    }
    txnDepth_ = 0;
    return true;
}

// ============================================================
// TransactionGuard
// ============================================================
TransactionGuard::TransactionGuard(Database& db, bool* ok) : db_(db), ok_(ok) {
    const bool b = db_.begin();
    if (ok_) *ok_ = b;
}

TransactionGuard::~TransactionGuard() {
    if (!committed_) {
        db_.rollback();
    }
}

void TransactionGuard::commit() {
    committed_ = db_.commit();
}

void TransactionGuard::rollback() {
    db_.rollback();
    committed_ = true;
}

} // namespace DocuSearch
