// ============================================================
// Config.cpp
// ============================================================

#include "Config.h"
#include "Constants.h"

#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QVariant>

namespace DocuSearch {

Config& Config::instance() {
    static Config inst;
    return inst;
}

Config::Config() : QObject(nullptr) {
    settings_ = std::make_unique<QSettings>(
        QSettings::IniFormat, QSettings::UserScope,
        Constants::kOrgName, Constants::kAppName);
}

Config::~Config() = default;

QString Config::dataDir() const {
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (path.isEmpty())
        path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QDir().mkpath(path);
    return path;
}

QString Config::logDir() const        { QDir().mkpath(dataDir() + "/logs");    return dataDir() + "/logs";    }
QString Config::dbPath() const        { return dataDir() + "/" + Constants::kDbFileName; }
QString Config::thumbnailCacheDir() const { QDir().mkpath(dataDir() + "/thumbs"); return dataDir() + "/thumbs"; }
QString Config::backupDir() const     { QDir().mkpath(dataDir() + "/backups"); return dataDir() + "/backups"; }

AppSettings Config::load() const {
    AppSettings s;
    s.indexedDrives          = settings_->value("indexedDrives").toStringList();
    s.excludedFolders        = settings_->value("excludedFolders").toStringList();
    s.excludedExtensions     = settings_->value("excludedExtensions").toStringList();
    s.includedExtensions     = settings_->value("includedExtensions").toStringList();
    s.maxWorkerThreads       = settings_->value("maxWorkerThreads", s.maxWorkerThreads).toInt();
    s.cpuTargetPct           = settings_->value("cpuTargetPct", s.cpuTargetPct).toInt();
    s.cpuPauseThresholdPct   = settings_->value("cpuPauseThresholdPct", s.cpuPauseThresholdPct).toInt();
    s.pauseOnHeavyLoad       = settings_->value("pauseOnHeavyLoad", s.pauseOnHeavyLoad).toBool();
    s.lazyOcrEnabled         = settings_->value("lazyOcrEnabled", s.lazyOcrEnabled).toBool();
    s.hashLargeFiles         = settings_->value("hashLargeFiles", s.hashLargeFiles).toBool();
    s.darkMode               = settings_->value("darkMode", s.darkMode).toBool();
    s.monitorFileChanges     = settings_->value("monitorFileChanges", s.monitorFileChanges).toBool();
    s.tessdataPath           = settings_->value("tessdataPath").toString();
    s.ocrLanguage            = settings_->value("ocrLanguage", s.ocrLanguage).toString();
    s.thumbnailSize          = settings_->value("thumbnailSize", s.thumbnailSize).toInt();
    s.lastBackupPath         = settings_->value("lastBackupPath").toString();
    return s;
}

void Config::save(const AppSettings& s) {
    settings_->setValue("indexedDrives",        s.indexedDrives);
    settings_->setValue("excludedFolders",      s.excludedFolders);
    settings_->setValue("excludedExtensions",   s.excludedExtensions);
    settings_->setValue("includedExtensions",   s.includedExtensions);
    settings_->setValue("maxWorkerThreads",     s.maxWorkerThreads);
    settings_->setValue("cpuTargetPct",         s.cpuTargetPct);
    settings_->setValue("cpuPauseThresholdPct", s.cpuPauseThresholdPct);
    settings_->setValue("pauseOnHeavyLoad",     s.pauseOnHeavyLoad);
    settings_->setValue("lazyOcrEnabled",       s.lazyOcrEnabled);
    settings_->setValue("hashLargeFiles",       s.hashLargeFiles);
    settings_->setValue("darkMode",             s.darkMode);
    settings_->setValue("monitorFileChanges",   s.monitorFileChanges);
    settings_->setValue("tessdataPath",         s.tessdataPath);
    settings_->setValue("ocrLanguage",          s.ocrLanguage);
    settings_->setValue("thumbnailSize",        s.thumbnailSize);
    settings_->setValue("lastBackupPath",       s.lastBackupPath);
    settings_->sync();
    emit settingsChanged(s);
}

} // namespace DocuSearch
