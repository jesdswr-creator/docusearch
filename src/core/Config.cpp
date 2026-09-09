// ============================================================
// Config.cpp
// ============================================================

#include "Config.h"
#include "Constants.h"
#include "TierConfig.h"
#include "Logger.h"

#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QVariant>
#include <QThread>
#include <QThreadPool>

namespace DocuSearch {

Config& Config::instance() {
    static Config inst;
    return inst;
}

Config::Config() : QObject(nullptr) {
    settings_ = std::make_unique<QSettings>(
        QSettings::IniFormat, QSettings::UserScope,
        Constants::kOrgName, Constants::kAppName);
    
    // PHASE 1: Detect system profile on startup
    m_systemProfile = SystemProfiler::instance()->profile();
    DS_INFO("Config", QString("Initialized for tier: %1").arg(SystemProfiler::tierName(m_systemProfile.tier)));
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
    s.firstRunDone           = settings_->value("firstRunDone", s.firstRunDone).toBool();
    s.junkTextAuditDone      = settings_->value("junkTextAuditDone", s.junkTextAuditDone).toBool();
    s.welcomeDone            = settings_->value("welcomeDone", s.welcomeDone).toBool();
    s.closeConfirmAsk        = settings_->value("closeConfirmAsk", s.closeConfirmAsk).toBool();
    return s;
}

void Config::save(const AppSettings& s) {
    settings_->setValue("indexedDrives",        s.indexedDrives);
    settings_->setValue("excludedFolders",      s.excludedFolders);
    settings_->setValue("excludedExtensions",   s.excludedExtensions);
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
    settings_->setValue("firstRunDone",         s.firstRunDone);
    settings_->setValue("junkTextAuditDone",    s.junkTextAuditDone);
    settings_->setValue("welcomeDone",          s.welcomeDone);
    settings_->setValue("closeConfirmAsk",      s.closeConfirmAsk);
    settings_->sync();
    emit settingsChanged(s);
}

void Config::initThreadPoolsForTier() {
    TierConfig tierCfg = TierConfigManager::getConfig(m_systemProfile.tier);
    
    // Global thread pool stack size
    QThreadPool::globalInstance()->setStackSize(tierCfg.threadStackSize * 1024);
    
    DS_INFO("Config/ThreadPool",
        QString("Extraction: %1w | OCR: %2w | Stack: %3KB")
            .arg(tierCfg.extractionWorkers)
            .arg(tierCfg.ocrWorkers)
            .arg(tierCfg.threadStackSize));
}

void Config::applyDatabasePragmasForTier(const QString& path) {
    TierConfig tierCfg = TierConfigManager::getConfig(m_systemProfile.tier);
    
    DS_INFO("Config/Database",
        QString("Tier pragmas: %1 | Cache: %2MB | Mmap: %3MB | Timeout: %4ms")
            .arg(tierCfg.journalMode)
            .arg(tierCfg.databaseCacheSize / (1024 * 1024))
            .arg(tierCfg.mmapSize / (1024 * 1024))
            .arg(tierCfg.busyTimeout));
}

} // namespace DocuSearch
