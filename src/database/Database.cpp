// ============================================================
// Database.cpp
// ============================================================

#include "Database.h"
#include "../core/Logger.h"
#include "../core/TierConfig.h"
#include "../core/SystemProfile.h"

#include <sqlite3.h>
#include <QFileInfo>
#include <QMutexLocker>

#ifdef _WIN32
#  include <windows.h>
#  include <fileapi.h>
#endif

namespace DocuSearch {

namespace {

// Read a scalar PRAGMA text value back from the connection (used to
// verify that the journal_mode we asked for is the one actually in
// effect - see the B1 note in open()).
QString pragmaText(sqlite3* db, const char* pragma) {
    sqlite3_stmt* st = nullptr;
    QString out;
    if (sqlite3_prepare_v2(db, pragma, -1, &st, nullptr) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) {
            const unsigned char* t = sqlite3_column_text(st, 0);
            if (t)
                out = QString::fromUtf8(reinterpret_cast<const char*>(t))
                          .toLower();
        }
        sqlite3_finalize(st);
    }
    return out;
}

} // namespace

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
    QMutexLocker lock(&dbMutex_);
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

    // PHASE 1: Apply tier-specific pragmas
    SystemProfile profile = SystemProfiler::instance()->profile();
    TierConfig tierCfg = TierConfigManager::getConfig(profile.tier);
    
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
        // PHASE 1: Use tier-specific pragmas from TierConfig.
        //
        // B1 (audit 2026-09-09): synchronous=NORMAL is only crash-safe
        // in WAL mode. In rollback-journal modes (DELETE/TRUNCATE/
        // PERSIST) a power loss at the wrong moment has a real chance
        // of corrupting the database, so a non-WAL tier must run at
        // synchronous=FULL - the same pairing the network branch above
        // has always used. The mode is also READ BACK after applying:
        // a failed journal_mode pragma (transient lock, open
        // transaction) used to silently leave the default journal in
        // place while synchronous=NORMAL stayed applied - exactly the
        // corruption window B1 describes. A mismatch now logs DS_ERROR
        // and forces synchronous=FULL regardless of tier.
        const QString jmSql =
            QString("PRAGMA journal_mode = %1;").arg(tierCfg.journalMode);
        if (sqlite3_exec(db_, jmSql.toUtf8().constData(),
                         nullptr, nullptr, nullptr) != SQLITE_OK) {
            DS_WARN("Database", QString("Pragma failed: %1 -> %2")
                                    .arg(jmSql, sqlite3_errmsg(db_)));
        }
        const QString actualMode = pragmaText(db_, "PRAGMA journal_mode;");
        const bool walActive = (actualMode == QLatin1String("wal"));
        if (!actualMode.isEmpty()
            && actualMode != tierCfg.journalMode.toLower()
            && !walActive) {
            DS_ERROR("Database",
                     QString("journal_mode did not take effect "
                             "(asked %1, got %2) - forcing synchronous=FULL")
                         .arg(tierCfg.journalMode, actualMode));
        }
        pragmas = {
            QString("PRAGMA synchronous  = %1;")
                .arg(walActive ? "NORMAL" : "FULL"),
            "PRAGMA temp_store   = MEMORY;",
            QString("PRAGMA cache_size = -%1;").arg(tierCfg.databaseCacheSize / 1024),
            QString("PRAGMA mmap_size = %1;").arg(tierCfg.mmapSize),
            "PRAGMA foreign_keys = ON;",
            QString("PRAGMA busy_timeout = %1;").arg(tierCfg.busyTimeout),
            "PRAGMA encoding     = 'UTF-8';",
            "PRAGMA automatic_index = OFF;",
        };

        if (walActive) {
            pragmas.append("PRAGMA wal_autocheckpoint = 500;");
        }
    }
    
    for (const auto& p : pragmas) {
        if (sqlite3_exec(db_, p.toUtf8().constData(), nullptr, nullptr, nullptr) != SQLITE_OK) {
            DS_WARN("Database", QString("Pragma failed: %1 -> %2").arg(p, sqlite3_errmsg(db_)));
        }
    }
    DS_INFO("Database", QString("Opened: %1 (SQLite %2, %3, %4)")
                .arg(path, sqlite3_libversion(),
                     network ? "network" : "local",
                     SystemProfiler::tierName(profile.tier)));
    return true;
}

void Database::close() {
    QMutexLocker lock(&dbMutex_);
    if (!db_) return;
    // A single ROLLBACK unwinds the whole savepoint stack; the old
    // per-depth loop re-issued ROLLBACK against no active transaction
    // (error no-ops after the first). (audit A4)
    if (txnDepth_ > 0) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        txnDepth_ = 0;
    }
    // close_v2 defers deallocation when a cached prepared statement is
    // still outstanding (FileRepository keeps its upsert statements
    // across calls) instead of failing the close silently with
    // SQLITE_BUSY and leaking the handle. (audit A4)
    const int rc = sqlite3_close_v2(db_);
    if (rc != SQLITE_OK)
        DS_WARN("Database", QString("sqlite3_close_v2 rc=%1").arg(rc));
    db_ = nullptr;
    path_.clear();
}

bool Database::exec(const QString& sql, QString* err) {
    QMutexLocker lock(&dbMutex_);
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

QString Database::getSavepointName(int depth) const {
    return QString("sp_level_%1").arg(depth);
}

bool Database::begin() {
    QMutexLocker lock(&dbMutex_);
    if (!db_) return false;
    
    if (txnDepth_ == 0) {
        // PHASE 1: Begin outermost transaction
        if (sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) != SQLITE_OK)
            return false;
        DS_DEBUG("Transaction", "BEGIN transaction");
    } else {
        // PHASE 1: Create SAVEPOINT for nested transaction (fix for nested txn bug)
        QString savepoint = getSavepointName(txnDepth_);
        QString sql = QString("SAVEPOINT %1;").arg(savepoint);
        if (sqlite3_exec(db_, sql.toUtf8().constData(), nullptr, nullptr, nullptr) != SQLITE_OK)
            return false;
        DS_DEBUG("Transaction", QString("SAVEPOINT %1").arg(savepoint));
    }
    ++txnDepth_;
    return true;
}

bool Database::commit() {
    QMutexLocker lock(&dbMutex_);
    if (!db_ || txnDepth_ == 0) return false;
    --txnDepth_;
    if (txnDepth_ == 0) {
        // PHASE 1: Commit outermost transaction
        bool ok = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK;
        if (ok) DS_DEBUG("Transaction", "COMMIT transaction");
        return ok;
    } else {
        // PHASE 1: Release SAVEPOINT
        QString savepoint = getSavepointName(txnDepth_);
        QString sql = QString("RELEASE SAVEPOINT %1;").arg(savepoint);
        bool ok = sqlite3_exec(db_, sql.toUtf8().constData(), nullptr, nullptr, nullptr) == SQLITE_OK;
        if (ok) DS_DEBUG("Transaction", QString("RELEASE SAVEPOINT %1").arg(savepoint));
        return ok;
    }
}

bool Database::rollback() {
    QMutexLocker lock(&dbMutex_);
    if (!db_ || txnDepth_ == 0) return false;
    
    if (txnDepth_ == 1) {
        // PHASE 1: Rollback entire transaction
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        DS_DEBUG("Transaction", "ROLLBACK transaction");
    } else {
        // PHASE 1: Rollback to SAVEPOINT. RELEASE afterwards keeps the
        // savepoint stack exact - ROLLBACK TO alone leaves the
        // savepoint open (audit A5); a later begin() re-creates the
        // same name, and any outermost commit/rollback discards the
        // rest of the stack.
        QString savepoint = getSavepointName(txnDepth_ - 1);
        QString sql = QString("ROLLBACK TO SAVEPOINT %1; "
                              "RELEASE SAVEPOINT %1;").arg(savepoint);
        sqlite3_exec(db_, sql.toUtf8().constData(), nullptr, nullptr, nullptr);
        DS_DEBUG("Transaction", QString("ROLLBACK TO %1 + RELEASE").arg(savepoint));
    }
    --txnDepth_;
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
