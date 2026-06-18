#pragma once

#include <QDialog>
#include <QListWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include "../core/Types.h"

namespace DocuSearch {

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(const AppSettings& current, QWidget* parent = nullptr);

    AppSettings result() const;

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

private:
    void populateSavedSearches();

    AppSettings current_;

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
