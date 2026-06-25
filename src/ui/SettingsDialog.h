#pragma once

#include <QDialog>
#include <QListWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include "../core/Types.h"

namespace DocuSearch {

class FileRepository;
class Database;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    // repo is used for: saved-searches load/save/delete, VACUUM.
    // db  is used for: WAL checkpoint before backup (so the zipped .db
    //                  file contains all latest writes).
    // Both pointers must outlive the dialog. Pass nullptr only in tests.
    explicit SettingsDialog(const AppSettings& current,
                            FileRepository* repo = nullptr,
                            Database* db = nullptr,
                            QWidget* parent = nullptr);

    AppSettings result() const;

signals:
    // Emitted when the user clicks "Apply". MainWindow connects this
    // to a slot that persists settings and reapplies them live, so the
    // user can keep the dialog open and see changes take effect.
    void settingsApplied(const AppSettings& s);

    // Emitted when the user restores a backup. MainWindow should close
    // the database, let the restore overwrite the .db file, then reopen.
    void restoreRequested(const QString& backupZipPath);

private slots:
    void onAddDrive();
    void onRemoveDrive();
    void onAddExclude();
    void onRemoveExclude();
    void onAddExcludedExt();
    void onRemoveExcludedExt();
    void onSaveSearch();
    void onDeleteSearch();
    void onBackupNow();
    void onRestoreNow();
    void onVacuumDb();
    void onApply();

private:
    void populateSavedSearches();
    void populateLangCombo();

    AppSettings current_;
    FileRepository* repo_;
    Database*       db_;

    // Drives & excludes
    QListWidget* drivesList_;
    QListWidget* excludesList_;
    QListWidget* excludedExtList_;
    QLineEdit*   driveInput_;
    QLineEdit*   excludeInput_;
    QLineEdit*   extInput_;

    // Performance
    QSpinBox*    threadsSpin_;
    QSpinBox*    cpuTargetSpin_;
    QSpinBox*    cpuPauseSpin_;
    QCheckBox*   pauseOnHeavyCheck_;
    QCheckBox*   lazyOcrCheck_;
    QCheckBox*   hashFilesCheck_;
    QCheckBox*   monitorCheck_;

    // OCR
    QLineEdit*   tessdataEdit_;
    QComboBox*   langCombo_;

    // UI
    QCheckBox*   darkModeCheck_;

    // Saved searches
    QListWidget* savedList_;
    QLineEdit*   savedNameEdit_;
    QLineEdit*   savedQueryEdit_;
};

} // namespace DocuSearch
