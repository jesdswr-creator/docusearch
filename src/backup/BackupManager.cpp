// ============================================================
// BackupManager.cpp
// ============================================================
//
// v1.7.23 hardening pass (audit B2, docs/AUDIT-2026-09-09.md):
//  - compressor exit codes and output size are verified (a finished
//    QProcess is not a successful archive);
//  - restore() expands into a same-volume temp dir, validates the
//    SQLite header, then swaps atomically - a failed restore can no
//    longer destroy the live database and reopen a blank, schema-less
//    file;
//  - PowerShell paths are single-quote-escaped (an apostrophe in a
//    user or folder name used to truncate the command);
//  - compressor timeouts scale with payload size (30 s flat failed on
//    multi-GB libraries);
//  - the manifest carries the real app version.

#include "BackupManager.h"
#include "../core/Logger.h"
#include "../core/Constants.h"

#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

namespace DocuSearch {

namespace {

// PowerShell single-quotes a path by doubling embedded quotes
// (' -> '').
QString psQuote(const QString& path) {
    QString quoted = path;
    quoted.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QStringLiteral("'") + quoted + QStringLiteral("'");
}

// Compress-Archive throughput is roughly 5-20 MB/s: 30 s base plus
// 1 s per 5 MB of payload, capped at +10 min.
int compressorTimeoutMs(qint64 payloadBytes) {
    const qint64 scaled = payloadBytes / (5LL * 1024 * 1024);
    return 30000 + static_cast<int>(qMin<qint64>(scaled, 600) * 1000);
}

} // namespace

BackupManager::BackupManager(QObject* parent) : QObject(parent) {}

QString BackupManager::backup(const QString& dbPath, const QString& backupDir) {
    QDir().mkpath(backupDir);
    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const QString outZip = backupDir + "/docusearch_backup_" + stamp + ".zip";

    // Write a small manifest alongside.
    QJsonObject manifest;
    manifest["app"]      = "DocuSearch";
    manifest["version"]  = QString::fromLatin1(Constants::kAppVersion);
    manifest["timestamp"]= stamp;
    manifest["dbSize"]   = QFileInfo(dbPath).size();
    const QByteArray manifestJson = QJsonDocument(manifest).toJson(QJsonDocument::Indented);
    QFile mf(backupDir + "/manifest.json");
    if (mf.open(QIODevice::WriteOnly)) mf.write(manifestJson);

    // Use PowerShell on Windows, `zip` on Linux.
    const qint64 dbBytes = QFileInfo(dbPath).size();
    QProcess proc;
#ifdef Q_OS_WIN
    const QString script = QString(
        "Compress-Archive -Force -Path %1,%2 -DestinationPath %3")
        .arg(psQuote(dbPath), psQuote(backupDir + "/manifest.json"),
             psQuote(outZip));
    proc.start("powershell", {"-NoProfile", "-Command", script});
#else
    proc.start("zip", {"-j", outZip, dbPath, backupDir + "/manifest.json"});
#endif
    if (!proc.waitForStarted(3000)) {
        DS_ERROR("Backup", "Failed to start compressor");
        return {};
    }
    if (!proc.waitForFinished(compressorTimeoutMs(dbBytes))) {
        proc.kill();
        DS_ERROR("Backup", "Compressor timeout");
        return {};
    }
    // waitForFinished() success is not compressor success: a failing
    // Compress-Archive still exits normally with a nonzero exit code
    // and no usable archive. Verify the exit, then gate on the output.
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        const QString errTail = QString::fromUtf8(
            proc.readAllStandardError().left(400));
        DS_ERROR("Backup", QString("Compressor failed (rc=%1): %2")
                               .arg(proc.exitCode()).arg(errTail));
        QFile::remove(outZip);
        return {};
    }
    if (!QFileInfo(outZip).isFile() || QFileInfo(outZip).size() <= 0) {
        DS_ERROR("Backup",
                 "Compressor reported success but produced no archive");
        return {};
    }
    DS_INFO("Backup", "Created: " + outZip);
    return outZip;
}

bool BackupManager::restore(const QString& backupFile, const QString& dbPath,
                            QString* err) {
    const auto fail = [err](const QString& why) {
        if (err) *err = why;
        DS_ERROR("Backup", "Restore failed: " + why);
        return false;
    };

    if (!QFileInfo::exists(backupFile))
        return fail("backup file not found");
    const QFileInfo dbInfo(dbPath);
    if (!QFileInfo(dbInfo.absolutePath()).isDir())
        return fail("database directory missing");

    // Expand into a same-volume temp dir first: the live database is
    // not touched until a valid, verified copy exists. Same volume
    // keeps the final move a rename instead of a cross-device copy.
    const QString tmp = dbInfo.absolutePath()
                        + QStringLiteral("/restore_tmp");
    const auto discardTmp = [&tmp]() { QDir(tmp).removeRecursively(); };
    discardTmp();   // stale temp from an older attempt
    if (!QDir().mkpath(tmp))
        return fail("cannot create temporary restore directory");
    const QString expandedDb =
        tmp + "/" + dbInfo.fileName();

    QProcess proc;
#ifdef Q_OS_WIN
    const QString script = QString(
        "Expand-Archive -Force -Path %1 -DestinationPath %2")
        .arg(psQuote(backupFile), psQuote(tmp));
    proc.start("powershell", {"-NoProfile", "-Command", script});
#else
    proc.start("unzip", {"-o", backupFile, "-d", tmp});
#endif
    if (!proc.waitForStarted(3000)) {
        discardTmp();
        return fail("cannot start the archive extractor");
    }
    if (!proc.waitForFinished(
            compressorTimeoutMs(QFileInfo(backupFile).size()))) {
        proc.kill();
        discardTmp();
        return fail("archive extractor timeout");
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        const QString errTail = QString::fromUtf8(
            proc.readAllStandardError().left(400));
        discardTmp();
        return fail(QString("archive extractor failed (rc=%1) %2")
                        .arg(proc.exitCode())
                        .arg(errTail.trimmed()));
    }

    // Verify the expanded file really is a SQLite database before it
    // may replace the live one. A zip of nothing, or a torn copy,
    // must never leave the app with a blank database. (Header check:
    // every SQLite 3 file starts with "SQLite format 3\0".)
    {
        QFile f(expandedDb);
        if (!f.open(QIODevice::ReadOnly)) {
            discardTmp();
            return fail("archive contains no database file");
        }
        const QByteArray header = f.read(16);
        f.close();
        if (header != QByteArrayLiteral("SQLite format 3\x00")) {
            discardTmp();
            return fail("archive database is not a valid SQLite file");
        }
    }

    // Swap: clear the WAL sidecars, move the old file aside, move the
    // new one in. Each step rolls the previous one back on failure, so
    // a sharing violation (file still held open by a background
    // worker) leaves the ORIGINAL database in place.
    const QString dbFile = dbInfo.absoluteFilePath();
    const QString bakFile = dbFile + QStringLiteral(".restore-bak");
    for (const QString& side : { dbFile + QStringLiteral("-wal"),
                                 dbFile + QStringLiteral("-shm") }) {
        if (!QFile::remove(side))
            DS_WARN("Backup", "Could not remove " + side);
    }
    QFile::remove(bakFile);
    if (QFileInfo::exists(dbFile) && !QFile::rename(dbFile, bakFile)) {
        discardTmp();
        return fail("database is still in use - close other windows "
                    "and try again");
    }
    if (!QFile::rename(expandedDb, dbFile)) {
        if (QFileInfo::exists(bakFile)) QFile::rename(bakFile, dbFile);
        discardTmp();
        return fail("could not move the restored database into place");
    }
    QFile::remove(bakFile);
    discardTmp();
    DS_INFO("Backup", "Restored: " + backupFile + " -> " + dbFile);
    return true;
}

QStringList BackupManager::listBackups(const QString& backupDir) const {
    QDir d(backupDir);
    if (!d.exists()) return {};
    QStringList out;
    for (const QFileInfo& fi : d.entryInfoList({"*.zip"}, QDir::Files, QDir::Time))
        out << fi.absoluteFilePath();
    return out;
}

} // namespace DocuSearch
