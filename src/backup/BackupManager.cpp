// ============================================================
// BackupManager.cpp
// ============================================================

#include "BackupManager.h"
#include "../core/Logger.h"

#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

namespace DocuSearch {

BackupManager::BackupManager(QObject* parent) : QObject(parent) {}

QString BackupManager::backup(const QString& dbPath, const QString& backupDir) {
    QDir().mkpath(backupDir);
    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const QString outZip = backupDir + "/docusearch_backup_" + stamp + ".zip";

    // Write a small manifest alongside.
    QJsonObject manifest;
    manifest["app"]      = "DocuSearch";
    manifest["version"]  = "1.0.0";
    manifest["timestamp"]= stamp;
    manifest["dbSize"]   = QFileInfo(dbPath).size();
    const QByteArray manifestJson = QJsonDocument(manifest).toJson(QJsonDocument::Indented);
    QFile mf(backupDir + "/manifest.json");
    if (mf.open(QIODevice::WriteOnly)) mf.write(manifestJson);

    // Use PowerShell on Windows, `zip` on Linux.
    QProcess proc;
#ifdef Q_OS_WIN
    const QString script = QString(
        "Compress-Archive -Force -Path '%1','%2' -DestinationPath '%3'")
        .arg(dbPath, backupDir + "/manifest.json", outZip);
    proc.start("powershell", {"-NoProfile", "-Command", script});
#else
    proc.start("zip", {"-j", outZip, dbPath, backupDir + "/manifest.json"});
#endif
    if (!proc.waitForStarted(3000)) {
        DS_ERROR("Backup", "Failed to start compressor");
        return {};
    }
    if (!proc.waitForFinished(30000)) {
        DS_ERROR("Backup", "Compressor timeout");
        return {};
    }
    DS_INFO("Backup", "Created: " + outZip);
    return outZip;
}

bool BackupManager::restore(const QString& backupFile, const QString& dbPath) {
    if (!QFileInfo::exists(backupFile)) return false;
    // Close DB before overwriting (caller is responsible for that).
    QFile::remove(dbPath);
    QProcess proc;
#ifdef Q_OS_WIN
    const QString script = QString("Expand-Archive -Force -Path '%1' -DestinationPath '%2'")
        .arg(backupFile, QFileInfo(dbPath).absolutePath());
    proc.start("powershell", {"-NoProfile", "-Command", script});
#else
    proc.start("unzip", {"-o", backupFile, "-d", QFileInfo(dbPath).absolutePath()});
#endif
    if (!proc.waitForStarted(3000)) return false;
    if (!proc.waitForFinished(60000)) return false;
    DS_INFO("Backup", "Restored: " + backupFile + " -> " + dbPath);
    return QFileInfo::exists(dbPath);
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
