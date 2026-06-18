#pragma once

#include <QObject>
#include <QString>

namespace DocuSearch {

// Backup the entire user data (database + tags + notes + saved searches).
// Tags/notes/saved searches are inside the SQLite DB, so backing up the
// .db file covers all of them. We additionally export a JSON manifest.
class BackupManager : public QObject {
    Q_OBJECT
public:
    explicit BackupManager(QObject* parent = nullptr);

    // Create a timestamped backup zip at backupDir(). Returns path.
    QString backup(const QString& dbPath, const QString& backupDir);

    // Restore from a backup file. Returns true on success.
    bool restore(const QString& backupFile, const QString& dbPath);

    // List existing backups in the given directory.
    QStringList listBackups(const QString& backupDir) const;
};

} // namespace DocuSearch
