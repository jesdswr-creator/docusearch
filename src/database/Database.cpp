// ============================================================
// Database.cpp
// ============================================================

#include "Database.h"
#include "../core/Logger.h"

#include <sqlite3.h>
#include <QFileInfo>

#ifdef _WIN32
#  include <windows.h>
#  include <fileapi.h>
#endif

namespace DocuSearch {

Database::Database(QObject* parent) : QObject(parent) {}

Database::~Database() { close(); }

// Detect if the database path is on a network drive (SMB/CIFS).
// On network shares, mmap and WAL are unreliable — Windows doesn't
// support memory-mapped I/O over SMB reliably, and WAL mode can
// corrupt over network filesystems. We fall back to conservative
// settings (DELETE journal, no mmap, FULL synchronous) for safety.
// See HIGH-3 in the review report.
static bool isNetworkPath(const QString& path) {
#ifdef _WIN32
    // Get the drive root (e.g., "C:\\" or "\\\\server\\share\\")
    const QString dir = QFileInfo(path).absolutePath();
    const WCHAR driveRoot[MAX_PATH] = {0};
    // QFileInfo(path).absolutePath() returns the directory; we need the drive root.
    // Use PathStripToRoot to get "C:\" or "\\server\share\"
    WCHAR buf[MAX_PATH] = {0};
    const int n = dir.toWCharArray(buf);
    buf[n] = 0;
    // PathStripToRoot is in shlwapi.h — simpler to just check the first 2 chars.
    // If path starts with "\\" it's a UNC path → network.
    if (n >= 2 && buf[0] == L'\\' && buf[1] == L'\\') return true;
    // Otherwise check drive letter via GetDriveTypeW.
    if (n >= 3 && buf[1] == L':') {
        WCHAR root[4] = {buf[0], L':', L'\\', 0};
        const UINT type = GetDriveTypeW(root);
        return type == DRIVE_REMOTE;
    }
    return false;
#else
    (void)path;
    return false;
#endif
}

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

    // Pragmas for performance on low-end systems (4GB RAM).
    // For NETWORK drives (SMB/CIFS), use conservative settings — WAL
    // mode and mmap can corrupt over network filesystems. See HIGH-3.
    const bool network = isNetworkPath(path);
    QStringList pragmas;
    if (network) {
        DS_WARN("Database", "Database is on a network drive — using conservative "
                            "pragmas (no WAL, no mmap, FULL synchronous).");
        pragmas = {
            "PRAGMA journal_mode = DELETE;",   // WAL unsafe over SMB
            "PRAGMA synchronous  = FULL;",      // Safety first
            "PRAGMA temp_store   = MEMORY;",
            "PRAGMA cache_size   = -16384;",    // 16MB (smaller for network)
            "PRAGMA mmap_size    = 0;",         // Disable mmap entirely
            "PRAGMA foreign_keys = ON;",
            "PRAGMA busy_timeout = 10000;",     // Longer timeout for network latency
            "PRAGMA encoding     = 'UTF-8';",
            "PRAGMA automatic_index = OFF;",
        };
    } else {
        // Local disk: aggressive performance pragmas.
        // cache_size 32MB, mmap 128MB (reduced from 256MB for 4GB RAM systems).
        pragmas = {
            "PRAGMA journal_mode = WAL;",
            "PRAGMA synchronous  = NORMAL;",
            "PRAGMA temp_store   = MEMORY;",
            "PRAGMA cache_size   = -32768;",   // ~32MB
            "PRAGMA mmap_size    = 134217728;", // 128MB
            "PRAGMA foreign_keys = ON;",
            "PRAGMA busy_timeout = 5000;",
            "PRAGMA encoding     = 'UTF-8';",
            "PRAGMA automatic_index = OFF;",
            "PRAGMA wal_autocheckpoint = 500;",
        };
    }
    for (const auto& p : pragmas) {
        if (sqlite3_exec(db_, p.toUtf8().constData(), nullptr, nullptr, nullptr) != SQLITE_OK) {
            DS_WARN("Database", QString("Pragma failed: %1 -> %2").arg(p, sqlite3_errmsg(db_)));
        }
    }
    DS_INFO("Database", QString("Opened: %1 (SQLite %2, %3)")
                .arg(path, sqlite3_libversion(),
                     network ? "network" : "local"));
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
