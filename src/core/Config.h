#pragma once

// ============================================================
// Config.h - Persistent application configuration (QSettings wrapper)
// ============================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include "../core/Types.h"

class QSettings;

namespace DocuSearch {

class Config : public QObject {
    Q_OBJECT
public:
    static Config& instance();

    // Load from QSettings. Safe to call multiple times.
    AppSettings load() const;
    void save(const AppSettings& s);

    // Convenience accessors
    QString dataDir() const;          // %APPDATA%/DocuSearch
    QString logDir() const;           // dataDir + "/logs"
    QString dbPath() const;           // dataDir + "/docusearch.db"
    QString thumbnailCacheDir() const;
    QString backupDir() const;

signals:
    void settingsChanged(const AppSettings& s);

private:
    Config();
    ~Config() override;
    Config(const Config&)            = delete;
    Config& operator=(const Config&) = delete;

    std::unique_ptr<QSettings> settings_;
};

} // namespace DocuSearch
