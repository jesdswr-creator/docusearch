// ============================================================
// SettingsDialog.cpp
// ============================================================

#include "SettingsDialog.h"
#include "../core/Constants.h"
#include "../database/FileRepository.h"
#include "../database/Database.h"
#include "../backup/BackupManager.h"
#include "../core/Config.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QTabWidget>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QLabel>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <sqlite3.h>

namespace DocuSearch {

// Role used to store the SavedSearches row id on each QListWidgetItem.
// We need the id so the Delete button can call repo_->deleteSearch(id).
static constexpr int kSearchIdRole = Qt::UserRole + 1;
// And the query text in a second role for display/edit.
static constexpr int kSearchQueryRole = Qt::UserRole + 2;

SettingsDialog::SettingsDialog(const AppSettings& current,
                               FileRepository* repo,
                               Database* db,
                               QWidget* parent)
    : QDialog(parent), current_(current), repo_(repo), db_(db) {
    setWindowTitle("Settings - DocuSearch");
    setMinimumWidth(640);
    setMinimumHeight(560);

    auto* outer = new QVBoxLayout(this);
    auto* tabs  = new QTabWidget(this);
    outer->addWidget(tabs);

    // -------- Indexing tab --------
    auto* indexingTab = new QWidget(this);
    auto* idxLay = new QVBoxLayout(indexingTab);

    // Drives
    auto* drvBox = new QGroupBox("Indexed Drives / Folders", this);
    auto* drvLay = new QVBoxLayout(drvBox);
    drivesList_ = new QListWidget(this);
    drivesList_->addItems(current_.indexedDrives);
    drvLay->addWidget(drivesList_);
    auto* drvRow = new QHBoxLayout();
    driveInput_ = new QLineEdit(this);
    driveInput_->setPlaceholderText("e.g., D:\\  (or click Browse...)");
    auto* browseBtn = new QPushButton("Browse...", this);
    auto* addDrvBtn = new QPushButton("Add", this);
    auto* rmDrvBtn  = new QPushButton("Remove", this);
    drvRow->addWidget(driveInput_);
    drvRow->addWidget(browseBtn);
    drvRow->addWidget(addDrvBtn);
    drvRow->addWidget(rmDrvBtn);
    drvLay->addLayout(drvRow);
    idxLay->addWidget(drvBox);

    // Excludes
    auto* exBox = new QGroupBox("Excluded Folders", this);
    auto* exLay = new QVBoxLayout(exBox);
    excludesList_ = new QListWidget(this);
    excludesList_->addItems(current_.excludedFolders);
    exLay->addWidget(excludesList_);
    auto* exRow = new QHBoxLayout();
    excludeInput_ = new QLineEdit(this);
    excludeInput_->setPlaceholderText("e.g., D:\\Movies");
    auto* browseExBtn = new QPushButton("Browse...", this);
    auto* addExBtn = new QPushButton("Add", this);
    auto* rmExBtn  = new QPushButton("Remove", this);
    exRow->addWidget(excludeInput_);
    exRow->addWidget(browseExBtn);
    exRow->addWidget(addExBtn);
    exRow->addWidget(rmExBtn);
    exLay->addLayout(exRow);
    idxLay->addWidget(exBox);

    // Excluded extensions
    auto* extBox = new QGroupBox("Excluded Extensions (in addition to defaults)", this);
    auto* extLay = new QVBoxLayout(extBox);
    excludedExtList_ = new QListWidget(this);
    excludedExtList_->addItems(current_.excludedExtensions);
    extLay->addWidget(excludedExtList_);
    auto* extRow = new QHBoxLayout();
    extInput_ = new QLineEdit(this);
    extInput_->setPlaceholderText("e.g., iso");
    auto* addExtBtn = new QPushButton("Add", this);
    auto* rmExtBtn  = new QPushButton("Remove", this);
    extRow->addWidget(extInput_);
    extRow->addWidget(addExtBtn);
    extRow->addWidget(rmExtBtn);
    extLay->addLayout(extRow);
    idxLay->addWidget(extBox);

    idxLay->addStretch();
    tabs->addTab(indexingTab, "Indexing");

    // -------- Performance tab --------
    auto* perfTab = new QWidget(this);
    auto* perfLay = new QFormLayout(perfTab);
    threadsSpin_ = new QSpinBox(this);
    threadsSpin_->setRange(1, 16);
    threadsSpin_->setValue(current_.maxWorkerThreads);
    cpuTargetSpin_ = new QSpinBox(this);
    cpuTargetSpin_->setRange(5, 95);
    cpuTargetSpin_->setValue(current_.cpuTargetPct);
    cpuPauseSpin_ = new QSpinBox(this);
    cpuPauseSpin_->setRange(20, 99);
    cpuPauseSpin_->setValue(current_.cpuPauseThresholdPct);
    pauseOnHeavyCheck_ = new QCheckBox("Pause indexing when system CPU exceeds threshold", this);
    pauseOnHeavyCheck_->setChecked(current_.pauseOnHeavyLoad);
    lazyOcrCheck_ = new QCheckBox("Enable lazy OCR (OCR on-demand at search time)", this);
    lazyOcrCheck_->setChecked(current_.lazyOcrEnabled);
    hashFilesCheck_ = new QCheckBox("Compute file hashes (for duplicate detection)", this);
    hashFilesCheck_->setChecked(current_.hashLargeFiles);
    monitorCheck_ = new QCheckBox("Monitor indexed drives for live changes", this);
    monitorCheck_->setChecked(current_.monitorFileChanges);

    perfLay->addRow("Worker threads", threadsSpin_);
    perfLay->addRow("Target CPU %", cpuTargetSpin_);
    perfLay->addRow("Pause threshold %", cpuPauseSpin_);
    perfLay->addRow("", pauseOnHeavyCheck_);
    perfLay->addRow("", lazyOcrCheck_);
    perfLay->addRow("", hashFilesCheck_);
    perfLay->addRow("", monitorCheck_);
    tabs->addTab(perfTab, "Performance");

    // -------- OCR tab --------
    // OCR is handled by Windows OCR (Windows.Media.Ocr), which is built
    // into Windows 10/11. No tessdata path or language configuration is
    // needed — Windows OCR uses the user's installed language packs.
    auto* ocrTab = new QWidget(this);
    auto* ocrLay = new QFormLayout(ocrTab);
    tessdataEdit_ = new QLineEdit(current_.tessdataPath, this);
    tessdataEdit_->setVisible(false);  // hidden, kept for compatibility
    langCombo_ = new QComboBox(this);
    langCombo_->setVisible(false);  // hidden, kept for compatibility

    auto* ocrInfo = new QLabel(
        "OCR is powered by Windows OCR (Windows.Media.Ocr),\n"
        "which is built into Windows 10/11.\n\n"
        "No additional configuration is needed.\n"
        "OCR uses your installed Windows language packs.\n\n"
        "To add more OCR languages:\n"
        "  Settings -> Time & Language -> Language -> Add a language\n"
        "  Check 'Optical character recognition'\n\n"
        "To OCR a file: select it and click 'OCR This File' in the preview pane.",
        this);
    ocrInfo->setWordWrap(true);
    ocrLay->addRow(ocrInfo);
    tabs->addTab(ocrTab, "OCR");

    // -------- Appearance tab --------
    auto* appTab = new QWidget(this);
    auto* appLay = new QVBoxLayout(appTab);
    darkModeCheck_ = new QCheckBox("Dark mode", this);
    darkModeCheck_->setChecked(current_.darkMode);
    appLay->addWidget(darkModeCheck_);
    appLay->addStretch();
    tabs->addTab(appTab, "Appearance");

    // -------- Saved searches tab --------
    auto* savTab = new QWidget(this);
    auto* savLay = new QVBoxLayout(savTab);
    savedList_ = new QListWidget(this);
    savedList_->setSelectionMode(QAbstractItemView::SingleSelection);
    savLay->addWidget(new QLabel("Existing saved searches:", this));
    savLay->addWidget(savedList_);
    auto* savForm = new QFormLayout();
    savedNameEdit_  = new QLineEdit(this);
    savedNameEdit_->setPlaceholderText("e.g., Invoices 2024");
    savedQueryEdit_ = new QLineEdit(this);
    savedQueryEdit_->setPlaceholderText("e.g., invoice type:pdf date:>2024-01-01");
    savForm->addRow("Name",  savedNameEdit_);
    savForm->addRow("Query", savedQueryEdit_);
    savLay->addLayout(savForm);
    auto* savRow = new QHBoxLayout();
    auto* addSavBtn = new QPushButton("Save", this);
    auto* rmSavBtn  = new QPushButton("Delete Selected", this);
    savRow->addWidget(addSavBtn);
    savRow->addWidget(rmSavBtn);
    savRow->addStretch();
    savLay->addLayout(savRow);
    savLay->addWidget(new QLabel(
        "Tip: saving a search with an existing name will overwrite it.", this));
    tabs->addTab(savTab, "Saved Searches");

    // -------- Backup / Restore tab --------
    auto* bkTab = new QWidget(this);
    auto* bkLay = new QVBoxLayout(bkTab);
    auto* bkHelp = new QLabel(
        "Back up the search index (database file) to a .zip archive.\n"
        "Restore will overwrite your current database - the app must "
        "restart afterwards.", this);
    bkHelp->setWordWrap(true);
    bkLay->addWidget(bkHelp);
    auto* backupBtn = new QPushButton("Backup Now...", this);
    auto* restoreBtn = new QPushButton("Restore from File...", this);
    auto* vacuumBtn  = new QPushButton("Vacuum Database (compact)", this);
    bkLay->addWidget(backupBtn);
    bkLay->addWidget(restoreBtn);
    bkLay->addWidget(vacuumBtn);
    bkLay->addStretch();
    tabs->addTab(bkTab, "Backup & Restore");

    // -------- Buttons --------
    auto* btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel,
        this);
    outer->addWidget(btns);
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    // Apply button: emit settingsApplied so MainWindow can persist +
    // live-apply the new settings without closing the dialog.
    if (auto* applyBtn = btns->button(QDialogButtonBox::Apply)) {
        connect(applyBtn, &QPushButton::clicked, this, &SettingsDialog::onApply);
    }

    // Wire up actions
    connect(browseBtn, &QPushButton::clicked, this, [this]{
        const QString d = QFileDialog::getExistingDirectory(this, "Choose folder");
        if (!d.isEmpty()) driveInput_->setText(d);
    });
    connect(browseExBtn, &QPushButton::clicked, this, [this]{
        const QString d = QFileDialog::getExistingDirectory(this, "Choose folder");
        if (!d.isEmpty()) excludeInput_->setText(d);
    });
    connect(addDrvBtn, &QPushButton::clicked, this, &SettingsDialog::onAddDrive);
    connect(rmDrvBtn,  &QPushButton::clicked, this, &SettingsDialog::onRemoveDrive);
    connect(addExBtn,  &QPushButton::clicked, this, &SettingsDialog::onAddExclude);
    connect(rmExBtn,   &QPushButton::clicked, this, &SettingsDialog::onRemoveExclude);
    connect(addExtBtn, &QPushButton::clicked, this, &SettingsDialog::onAddExcludedExt);
    connect(rmExtBtn,  &QPushButton::clicked, this, &SettingsDialog::onRemoveExcludedExt);
    connect(addSavBtn, &QPushButton::clicked, this, &SettingsDialog::onSaveSearch);
    connect(rmSavBtn,  &QPushButton::clicked, this, &SettingsDialog::onDeleteSearch);
    connect(backupBtn, &QPushButton::clicked, this, &SettingsDialog::onBackupNow);
    connect(restoreBtn,&QPushButton::clicked, this, &SettingsDialog::onRestoreNow);
    connect(vacuumBtn, &QPushButton::clicked, this, &SettingsDialog::onVacuumDb);

    // Clicking a saved search loads it into the name/query editors so
    // the user can edit it (saving with the same name overwrites).
    connect(savedList_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item){
        savedNameEdit_->setText(item->text());
        savedQueryEdit_->setText(item->data(kSearchQueryRole).toString());
    });

    populateSavedSearches();
}

void SettingsDialog::populateLangCombo() {
    // Common Tesseract language packs. The combo is editable so the
    // user can also type a custom combination (e.g., "eng+chi_sim+hin").
    langCombo_->clear();
    langCombo_->addItems({"eng", "eng+hin", "eng+chi_sim", "chi_sim",
                          "chi_tra", "fra", "deu", "spa", "ita",
                          "por", "rus", "jpn", "kor", "ara", "hin"});
}

void SettingsDialog::populateSavedSearches() {
    savedList_->clear();
    if (!repo_) {
        // No repository (e.g., running in a unit test). Show nothing.
        return;
    }
    // repo_->savedSearches() returns QList<{id, name}>. We pull the
    // query for each via savedSearchQuery(id) so the user can re-edit.
    auto list = repo_->savedSearches();
    for (const auto& p : list) {
        const qint64 id = p.first;
        const QString& name = p.second;
        const QString query = repo_->savedSearchQuery(id);
        auto* item = new QListWidgetItem(name);
        item->setData(kSearchIdRole,    id);
        item->setData(kSearchQueryRole, query);
        // Tooltip shows the query so the user can preview without clicking.
        item->setToolTip(query.isEmpty() ? QStringLiteral("(empty query)")
                                          : query);
        savedList_->addItem(item);
    }
}

AppSettings SettingsDialog::result() const {
    AppSettings s = current_;
    s.indexedDrives.clear();
    for (int i = 0; i < drivesList_->count(); ++i)
        s.indexedDrives << drivesList_->item(i)->text();
    s.excludedFolders.clear();
    for (int i = 0; i < excludesList_->count(); ++i)
        s.excludedFolders << excludesList_->item(i)->text();
    s.excludedExtensions.clear();
    for (int i = 0; i < excludedExtList_->count(); ++i)
        s.excludedExtensions << excludedExtList_->item(i)->text();

    s.maxWorkerThreads     = threadsSpin_->value();
    s.cpuTargetPct         = cpuTargetSpin_->value();
    s.cpuPauseThresholdPct = cpuPauseSpin_->value();
    s.pauseOnHeavyLoad     = pauseOnHeavyCheck_->isChecked();
    s.lazyOcrEnabled       = lazyOcrCheck_->isChecked();
    s.hashLargeFiles       = hashFilesCheck_->isChecked();
    s.monitorFileChanges   = monitorCheck_->isChecked();
    s.tessdataPath         = tessdataEdit_->text().trimmed();
    s.ocrLanguage          = langCombo_->currentText().trimmed();
    s.darkMode             = darkModeCheck_->isChecked();
    return s;
}

void SettingsDialog::onAddDrive() {
    const QString t = driveInput_->text().trimmed();
    if (t.isEmpty()) return;
    if (drivesList_->findItems(t, Qt::MatchExactly).isEmpty())
        drivesList_->addItem(t);
    driveInput_->clear();
}

void SettingsDialog::onRemoveDrive() {
    auto* cur = drivesList_->currentItem();
    if (cur) delete cur;
}

void SettingsDialog::onAddExclude() {
    const QString t = excludeInput_->text().trimmed();
    if (t.isEmpty()) return;
    if (excludesList_->findItems(t, Qt::MatchExactly).isEmpty())
        excludesList_->addItem(t);
    excludeInput_->clear();
}

void SettingsDialog::onRemoveExclude() {
    auto* cur = excludesList_->currentItem();
    if (cur) delete cur;
}

void SettingsDialog::onAddExcludedExt() {
    QString t = extInput_->text().trimmed().toLower();
    if (t.startsWith('.')) t = t.mid(1);
    if (t.isEmpty()) return;
    if (excludedExtList_->findItems(t, Qt::MatchExactly).isEmpty())
        excludedExtList_->addItem(t);
    extInput_->clear();
}

void SettingsDialog::onRemoveExcludedExt() {
    auto* cur = excludedExtList_->currentItem();
    if (cur) delete cur;
}

void SettingsDialog::onSaveSearch() {
    const QString name = savedNameEdit_->text().trimmed();
    const QString query = savedQueryEdit_->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, "Save Search",
            "Please enter a name for this saved search.");
        return;
    }
    if (query.isEmpty()) {
        QMessageBox::warning(this, "Save Search",
            "Please enter a search query to save.");
        return;
    }
    if (!repo_) {
        QMessageBox::warning(this, "Save Search",
            "Database is not available - changes cannot be saved.");
        return;
    }
    // saveSearch uses INSERT ... ON CONFLICT(search_name) DO UPDATE,
    // so saving with an existing name will overwrite the previous query.
    const qint64 id = repo_->saveSearch(name, query);
    if (id == 0) {
        QMessageBox::warning(this, "Save Search",
            "Failed to save the search to the database.");
        return;
    }
    // Refresh the list from the DB so the new/updated entry appears
    // with the correct id and tooltip.
    populateSavedSearches();
    // Select the just-saved entry so the user gets visual feedback.
    const auto matches = savedList_->findItems(name, Qt::MatchExactly);
    if (!matches.isEmpty()) {
        savedList_->setCurrentItem(matches.first());
    }
    savedNameEdit_->clear();
    savedQueryEdit_->clear();
}

void SettingsDialog::onDeleteSearch() {
    auto* cur = savedList_->currentItem();
    if (!cur) {
        QMessageBox::information(this, "Delete Search",
            "Select a saved search to delete first.");
        return;
    }
    const QString name = cur->text();
    const auto rc = QMessageBox::question(
        this, "Delete Search",
        QStringLiteral("Delete saved search \"%1\"?").arg(name),
        QMessageBox::Yes | QMessageBox::No);
    if (rc != QMessageBox::Yes) return;

    if (repo_) {
        const qint64 id = cur->data(kSearchIdRole).toLongLong();
        if (id > 0) {
            repo_->deleteSearch(id);
        }
    }
    populateSavedSearches();
}

void SettingsDialog::onApply() {
    // Emit the signal so MainWindow can persist + live-apply.
    // MainWindow is responsible for calling saveSettings(), applyTheme(),
    // updateIndexStats(), etc., and for showing a status-bar message.
    emit settingsApplied(result());
}

void SettingsDialog::onBackupNow() {
    const QString path = QFileDialog::getExistingDirectory(this, "Backup destination");
    if (path.isEmpty()) return;

    // Force SQLite to flush the WAL into the main .db file before zipping.
    // Otherwise the zipped .db would be missing the most recent writes.
    if (db_) {
        db_->exec("PRAGMA wal_checkpoint(TRUNCATE);");
    }

    BackupManager bm;
    const QString out = bm.backup(Config::instance().dbPath(), path);
    if (!out.isEmpty())
        QMessageBox::information(this, "Backup",
            "Backup created:\n" + out);
    else
        QMessageBox::warning(this, "Backup",
            "Backup failed. Check that the database is not in use and "
            "that you have write permission to the destination folder.");
}

void SettingsDialog::onRestoreNow() {
    const QString path = QFileDialog::getOpenFileName(this, "Restore from",
        QString(), "Zip files (*.zip)");
    if (path.isEmpty()) return;
    QMessageBox::StandardButton rc = QMessageBox::question(
        this, "Restore",
        "Restoring will overwrite your current database.\n"
        "The application must be restarted after restore.\n\n"
        "Continue?",
        QMessageBox::Yes | QMessageBox::No);
    if (rc != QMessageBox::Yes) return;

    // We can't safely overwrite the .db file while Database holds it
    // open. Emit a signal so MainWindow can:
    //   1. Close the database
    //   2. Let BackupManager overwrite the file
    //   3. Reopen the database
    //   4. Refresh the UI
    // MainWindow will show the success/failure message.
    emit restoreRequested(path);
}

void SettingsDialog::onVacuumDb() {
    if (!repo_) {
        QMessageBox::warning(this, "Vacuum",
            "Database is not available - cannot vacuum.");
        return;
    }
    // VACUUM can take a few seconds on large databases. We don't run it
    // in a background thread here because:
    //   1. The SQLite C API requires that VACUUM runs on the same
    //      connection that owns the database, and our Database object
    //      is not thread-safe for DDL.
    //   2. For a typical user database (<100 MB) VACUUM completes in
    //      under 2 seconds, which is acceptable for a modal dialog.
    setEnabled(false);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = repo_->vacuum();
    QApplication::restoreOverrideCursor();
    setEnabled(true);
    if (ok) {
        QMessageBox::information(this, "Vacuum",
            "Database vacuumed successfully.\n"
            "Unused space has been reclaimed.");
    } else {
        QMessageBox::warning(this, "Vacuum",
            "Vacuum failed. The database may be in use by another "
            "operation - try again later.");
    }
}

} // namespace DocuSearch
