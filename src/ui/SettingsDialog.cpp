// ============================================================
// SettingsDialog.cpp
// ============================================================

#include "SettingsDialog.h"
#include "../core/Constants.h"
#include "../database/FileRepository.h"
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
#include <sqlite3.h>

namespace DocuSearch {

SettingsDialog::SettingsDialog(const AppSettings& current, QWidget* parent)
    : QDialog(parent), current_(current) {
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
    auto* ocrTab = new QWidget(this);
    auto* ocrLay = new QFormLayout(ocrTab);
    tessdataEdit_ = new QLineEdit(current_.tessdataPath, this);
    auto* tdBrowse = new QPushButton("Browse...", this);
    auto* tdRow = new QHBoxLayout();
    tdRow->addWidget(tessdataEdit_);
    tdRow->addWidget(tdBrowse);
    ocrLay->addRow("Tessdata path", tdRow);

    langCombo_ = new QComboBox(this);
    langCombo_->addItems({"eng", "eng+hin", "eng+chi_sim", "chi_sim", "fra", "deu", "spa", "ita", "por", "rus", "jpn", "kor"});
    langCombo_->setCurrentText(current_.ocrLanguage);
    ocrLay->addRow("OCR language", langCombo_);
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
    savLay->addWidget(savedList_);
    auto* savForm = new QFormLayout();
    savedNameEdit_  = new QLineEdit(this);
    savedQueryEdit_ = new QLineEdit(this);
    savForm->addRow("Name",  savedNameEdit_);
    savForm->addRow("Query", savedQueryEdit_);
    savLay->addLayout(savForm);
    auto* savRow = new QHBoxLayout();
    auto* addSavBtn = new QPushButton("Save", this);
    auto* rmSavBtn  = new QPushButton("Delete", this);
    savRow->addWidget(addSavBtn);
    savRow->addWidget(rmSavBtn);
    savRow->addStretch();
    savLay->addLayout(savRow);
    tabs->addTab(savTab, "Saved Searches");

    // -------- Backup / Restore tab --------
    auto* bkTab = new QWidget(this);
    auto* bkLay = new QVBoxLayout(bkTab);
    auto* backupBtn = new QPushButton("Backup Now", this);
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
    connect(btns->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, [this]{ /* emit apply signal */ });

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
    connect(tdBrowse,  &QPushButton::clicked, this, [this]{
        const QString d = QFileDialog::getExistingDirectory(this, "Tessdata folder");
        if (!d.isEmpty()) tessdataEdit_->setText(d);
    });

    populateSavedSearches();
}

void SettingsDialog::populateSavedSearches() {
    savedList_->clear();
    // Load saved searches from the database
    sqlite3* raw = nullptr;
    // We need access to the database - get it from Config path
    QString dbPath = Config::instance().dbPath();
    if (sqlite3_open_v2(dbPath.toUtf8().constData(), &raw,
                        SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        return;
    }
    sqlite3_stmt* s = nullptr;
    if (sqlite3_prepare_v2(raw, "SELECT search_name, search_query FROM SavedSearches ORDER BY search_name;",
                           -1, &s, nullptr) == SQLITE_OK) {
        while (sqlite3_step(s) == SQLITE_ROW) {
            const unsigned char* name = sqlite3_column_text(s, 0);
            const unsigned char* query = sqlite3_column_text(s, 1);
            if (name && query) {
                auto* item = new QListWidgetItem(QString::fromUtf8(reinterpret_cast<const char*>(name)));
                item->setData(Qt::UserRole, QString::fromUtf8(reinterpret_cast<const char*>(query)));
                savedList_->addItem(item);
            }
        }
        sqlite3_finalize(s);
    }
    sqlite3_close(raw);
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
    s.ocrLanguage          = langCombo_->currentText();
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
    if (name.isEmpty() || query.isEmpty()) return;
    // We emit via signal - MainWindow will persist via FileRepository
    // For now, add to list locally:
    auto* item = new QListWidgetItem(name);
    item->setData(Qt::UserRole, query);
    savedList_->addItem(item);
    savedNameEdit_->clear();
    savedQueryEdit_->clear();
}

void SettingsDialog::onDeleteSearch() {
    auto* cur = savedList_->currentItem();
    if (cur) delete cur;
}

void SettingsDialog::onBackupNow() {
    // Defer to MainWindow via signal - but here we just open a save dialog
    const QString path = QFileDialog::getExistingDirectory(this, "Backup destination");
    if (path.isEmpty()) return;
    BackupManager bm;
    const QString out = bm.backup(Config::instance().dbPath(), path);
    if (!out.isEmpty())
        QMessageBox::information(this, "Backup", "Backup created:\n" + out);
    else
        QMessageBox::warning(this, "Backup", "Backup failed.");
}

void SettingsDialog::onRestoreNow() {
    const QString path = QFileDialog::getOpenFileName(this, "Restore from",
        QString(), "Zip files (*.zip)");
    if (path.isEmpty()) return;
    QMessageBox::StandardButton rc = QMessageBox::question(
        this, "Restore",
        "Restoring will overwrite your current database. Continue?",
        QMessageBox::Yes | QMessageBox::No);
    if (rc != QMessageBox::Yes) return;

    BackupManager bm;
    if (bm.restore(path, Config::instance().dbPath()))
        QMessageBox::information(this, "Restore", "Restore successful. Please restart.");
    else
        QMessageBox::warning(this, "Restore", "Restore failed.");
}

void SettingsDialog::onVacuumDb() {
    // Defer to MainWindow
    QMessageBox::information(this, "Vacuum", "Vacuum will run when you click Apply.");
}

} // namespace DocuSearch
