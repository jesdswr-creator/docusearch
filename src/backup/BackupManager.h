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
    // Empty string on failure (exit code + output-size verified - the
    // compressor "finishing" is not the same as the compressor
    // "succeeding").
    QString backup(const QString& dbPath, const QString& backupDir);

    // Restore from a backup file. The archive is expanded into a
    // same-volume temp dir, the expanded database is validated (SQLite
    // header) and only then atomically swapped in - on ANY failure the
    // previous database file is left untouched (audit B2: the old flow
    // deleted the live database first and re-opened a blank file when
    // the expand failed). On success the caller must run
    // Schema::initialize after reopening so backups taken by older
    // builds migrate forward.
    bool restore(const QString& backupFile, const QString& dbPath,
                 QString* err = nullptr);

    // List existing backups in the given directory.
    QStringList listBackups(const QString& backupDir) const;
};

} // namespace DocuSearch
