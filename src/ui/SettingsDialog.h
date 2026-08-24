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

    // Emitted when the user clicks "Embed All Documents Now" in the
    // Semantic Search tab. MainWindow connects this to a slot that
    // gathers all indexed files without embeddings and runs
    // BgeService::embedDocumentsBatch() in the background.
    void embedAllRequested();

    // Task 3 Fix D: Wire settings sliders to HybridSearchEngine.
    // Emitted in real-time as the user drags the sliders.
    void aiWeightChanged(float weight);      // 0.0 to 1.0
    void aiThresholdChanged(float threshold); // 0.0 to 1.0
    void aiTopKChanged(int topK);             // 5 to 100

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

    // (OCR tab removed — the app uses Windows.Media.Ocr (built into
    // Windows 10/11), which needs no user-tunable path or language
    // combo. OCR status is shown in the status bar; click it for
    // setup instructions.)

    // UI
    QCheckBox*   darkModeCheck_;

    // Saved searches
    QListWidget* savedList_;
    QLineEdit*   savedNameEdit_;
    QLineEdit*   savedQueryEdit_;
};

} // namespace DocuSearch
