// ============================================================
// MainWindow.cpp
// ============================================================

#include "MainWindow.h"
#include "Theme.h"
#include "SearchBar.h"
#include "ResultsPane.h"
#include "PreviewPane.h"
#include "MetadataPane.h"
#include "TagsNotesPane.h"
#include "IndexingProgress.h"
#include "SettingsDialog.h"

#include "../core/Config.h"
#include "../core/Constants.h"
#include "../core/Logger.h"
#include "../core/FileUtils.h"
#include "../database/Database.h"
#include "../database/Schema.h"
#include "../database/FileRepository.h"
#include "../search/SearchEngine.h"
#include "../indexer/Indexer.h"
#include "../ocr/OcrWorkerPool.h"
#include "../monitoring/FileWatcher.h"
#include "../documents/DocumentExtractorRegistry.h"
#include "../preview/ThumbnailGenerator.h"
#include "../settings/SettingsManager.h"

#include <QApplication>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QToolBar>
#include <QStatusBar>
#include <QSplitter>
#include <QTabWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QUrl>
#include <QTimer>
#include <QFile>
#include <QTextStream>
#include <QProgressDialog>
#include <QThread>
#include <QStyle>
#include <QLabel>
#include <QPixmap>
#include <QFuture>
#include <QtConcurrent>

#include <memory>

namespace DocuSearch {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {

    setWindowTitle(QString("%1 %2 - Offline Document Search")
                   .arg(Constants::kAppName, Constants::kAppVersion));
    resize(1400, 900);

    // --- Initialize ONLY the database + search (no OCR/indexer/watcher) ---
    // These subsystems are crash-prone without Tesseract/Poppler linked.
    // We keep the app stable by not constructing them.
    db_   = std::make_unique<Database>(this);
    repo_ = std::make_unique<FileRepository>(*db_, this);

    const QString dbPath = Config::instance().dbPath();
    QString err;
    if (!db_->open(dbPath, &err)) {
        QMessageBox::critical(this, "Database Error",
            "Failed to open database:\n" + err);
        return;
    }
    Schema::initialize(*db_);
    Schema::migrate(*db_);

    search_  = std::make_unique<SearchEngine>(*db_, *repo_, this);
    // ocrPool_, indexer_, watcher_, thumbs_ are NOT constructed.
    // Their slots in MainWindow check for null before using them.

    loadSettings();

    // --- UI ------------------------------------------------------------
    buildCentral();
    buildMenus();
    buildToolbar();
    applyTheme();

    // --- Signals (only the ones that don't need crash-prone subsystems) -
    connect(searchBar_, &SearchBar::searchRequested,
            this, &MainWindow::onSearch);
    connect(searchBar_, &SearchBar::savedSearchSelected,
            this, &MainWindow::onSavedSearchSelected);

    connect(resultsPane_, &ResultsPane::fileSelected,
            this, &MainWindow::onFileSelected);
    connect(resultsPane_, &ResultsPane::fileActivated,
            this, &MainWindow::onFileActivated);

    connect(previewPane_, &PreviewPane::openRequested,
            this, &MainWindow::onOpenOriginal);

    connect(tagsNotesPane_, &TagsNotesPane::tagAdded,
            this, &MainWindow::onTagAdded);
    connect(tagsNotesPane_, &TagsNotesPane::tagRemoved,
            this, &MainWindow::onTagRemoved);
    connect(tagsNotesPane_, &TagsNotesPane::noteChanged,
            this, &MainWindow::onNoteChanged);

    // Live search debounce
    liveSearchTimer_ = new QTimer(this);
    liveSearchTimer_->setSingleShot(true);
    liveSearchTimer_->setInterval(Constants::kSearchDebounceMs);
    connect(liveSearchTimer_, &QTimer::timeout, this, &MainWindow::onLiveSearchTick);
    connect(searchBar_, &SearchBar::searchRequested, [this](const QString&){
        liveSearchTimer_->start();
    });

    refreshSavedSearches();
    statusBar()->showMessage("Ready. Add files via Index -> Add Folder to Index to begin.");
}

MainWindow::~MainWindow() {
    if (indexer_) indexer_->stopIndexing();
    if (ocrPool_) ocrPool_->shutdown();
    if (watcher_) watcher_->stop();
    if (db_)      db_->close();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    saveSettings();
    if (indexer_ && indexer_->isRunning()) {
        const auto rc = QMessageBox::question(
            this, "Indexing in progress",
            "Indexing is still running. Quit anyway?",
            QMessageBox::Yes | QMessageBox::No);
        if (rc != QMessageBox::Yes) { e->ignore(); return; }
    }
    QMainWindow::closeEvent(e);
}

// ============================================================
// UI construction
// ============================================================
void MainWindow::buildCentral() {
    mainSplitter_ = new QSplitter(Qt::Horizontal, this);
    setCentralWidget(mainSplitter_);

    // Left: search bar + results
    auto* leftWidget = new QWidget(this);
    auto* leftLay = new QVBoxLayout(leftWidget);
    leftLay->setContentsMargins(8, 8, 8, 8);
    searchBar_ = new SearchBar(leftWidget);
    resultsPane_ = new ResultsPane(leftWidget);
    leftLay->addWidget(searchBar_);
    leftLay->addWidget(resultsPane_, 1);
    mainSplitter_->addWidget(leftWidget);

    // Middle: preview (top) + tags/notes (bottom)
    auto* middleWidget = new QWidget(this);
    middleWidget->setMinimumWidth(200);
    auto* middleLay = new QVBoxLayout(middleWidget);
    middleLay->setContentsMargins(4, 4, 4, 4);
    middleLay->setSpacing(4);
    previewPane_ = new PreviewPane(middleWidget);
    middleLay->addWidget(previewPane_, 3);
    tagsNotesPane_ = new TagsNotesPane(middleWidget);
    tagsNotesPane_->setMaximumHeight(200);
    tagsNotesPane_->setMinimumHeight(120);
    middleLay->addWidget(tagsNotesPane_, 1);
    mainSplitter_->addWidget(middleWidget);

    // Right: metadata + indexing (narrow)
    rightSplitter_ = new QSplitter(Qt::Vertical, this);
    rightSplitter_->setMinimumWidth(160);
    rightSplitter_->setMaximumWidth(260);
    metadataPane_   = new MetadataPane(rightSplitter_);
    metadataPane_->setMinimumHeight(140);
    indexingWidget_ = new IndexingProgressWidget(rightSplitter_);
    indexingWidget_->setMinimumHeight(80);

    rightSplitter_->addWidget(metadataPane_);
    rightSplitter_->addWidget(indexingWidget_);
    mainSplitter_->addWidget(rightSplitter_);

    // Results 60%, middle 25%, right 15%
    mainSplitter_->setStretchFactor(0, 60);
    mainSplitter_->setStretchFactor(1, 25);
    mainSplitter_->setStretchFactor(2, 15);

    rightSplitter_->setStretchFactor(0, 55);
    rightSplitter_->setStretchFactor(1, 45);

    updateIndexStats();
}

void MainWindow::buildMenus() {
    // File menu
    auto* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("Open File…", QKeySequence::Open,
        this, [this]{
            const QString p = QFileDialog::getOpenFileName(this, "Open file");
            if (!p.isEmpty()) openFile(p);
        });
    fileMenu->addAction("Export Results as CSV…", QKeySequence::Save,
        this, &MainWindow::onExportCsv);
    fileMenu->addSeparator();
    fileMenu->addAction("Quit", QKeySequence::Quit,
        qApp, &QApplication::quit);

    // Index menu
    auto* indexMenu = menuBar()->addMenu("&Index");
    indexMenu->addAction("Add Folder to Index...", this, [this]{
        // Manually scan a folder and add files to the database.
        // Also extracts text content from DOCX/XLSX/PPTX/TXT/CSV/MD
        // so FTS5 content search works.
        const QString folder = QFileDialog::getExistingDirectory(
            this, "Select Folder to Index");
        if (folder.isEmpty()) return;

        statusBar()->showMessage("Scanning " + folder + " ...");
        QApplication::processEvents();

        // Get the document extractor registry for content extraction
        auto& registry = DocumentExtractorRegistry::instance();

        int count = 0;
        int contentCount = 0;
        QStringList emptyExcludes;
        FileUtils::walkDirectory(folder, emptyExcludes, [&](const QFileInfo& fi) -> bool {
            FileRecord r;
            r.path         = FileUtils::toNative(fi.absoluteFilePath());
            r.filename     = fi.fileName();
            r.extension    = FileUtils::extensionOf(fi.absoluteFilePath());
            r.size         = fi.size();
            r.createdDate  = fi.birthTime();
            r.modifiedDate = fi.lastModified();
            r.indexingStatus = Constants::IndexingStatus::kMetadataOnly;
            r.ocrStatus      = Constants::OcrStatus::kNotNeeded;
            qint64 fileId = repo_->upsertFile(r);

            // Extract text content for supported document types
            if (fileId > 0 && registry.extractorFor(r.extension)) {
                try {
                    auto result = registry.extractByExtension(r.path, r.extension);
                    if (!result.text.isEmpty()) {
                        repo_->updateContent(fileId, result.text, result.source,
                                             Constants::IndexingStatus::kContentDone,
                                             Constants::OcrStatus::kNotNeeded);
                        ++contentCount;
                    }
                } catch (...) {
                    // Content extraction failed - file is still indexed by filename
                }
            }

            ++count;
            if (count % 200 == 0) {
                statusBar()->showMessage(QString("Scanned %1 files (%2 with content)...")
                    .arg(count).arg(contentCount));
                QApplication::processEvents();
            }
            return true;
        });

        statusBar()->showMessage(QString("Indexed %1 files (%2 with content) from %3")
            .arg(count).arg(contentCount).arg(folder));
        QMessageBox::information(this, "Indexing Complete",
            QString("Added %1 files to the search index.\n"
                    "%2 files have extracted text content.\n\n"
                    "You can now search by filename AND content.").arg(count).arg(contentCount));
    });
    indexMenu->addSeparator();
    indexMenu->addAction("Start Indexing", this, &MainWindow::onStartIndexing);
    indexMenu->addAction("Pause",  this, &MainWindow::onPauseIndexing);
    indexMenu->addAction("Resume", this, &MainWindow::onResumeIndexing);
    indexMenu->addAction("Stop",   this, &MainWindow::onStopIndexing);
    indexMenu->addSeparator();
    indexMenu->addAction("Detect Duplicates", this, &MainWindow::onDetectDuplicates);

    // View menu
    auto* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction("Toggle Dark/Light Theme", QKeySequence("F11"),
        this, &MainWindow::onToggleTheme);

    // Tools menu
    auto* toolsMenu = menuBar()->addMenu("&Tools");
    toolsMenu->addAction("Settings…", this, &MainWindow::onOpenSettings);

    // Help menu
    auto* helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("About DocuSearch", this, &MainWindow::onAbout);
}

void MainWindow::buildToolbar() {
    auto* tb = addToolBar("Main");
    tb->setMovable(false);
    tb->setIconSize(QSize(20, 20));

    auto* startAct = new QAction(style()->standardIcon(QStyle::SP_MediaPlay), "Index", this);
    connect(startAct, &QAction::triggered, this, &MainWindow::onStartIndexing);
    tb->addAction(startAct);

    auto* pauseAct = new QAction(style()->standardIcon(QStyle::SP_MediaPause), "Pause", this);
    connect(pauseAct, &QAction::triggered, this, &MainWindow::onPauseIndexing);
    tb->addAction(pauseAct);

    auto* stopAct = new QAction(style()->standardIcon(QStyle::SP_MediaStop), "Stop", this);
    connect(stopAct, &QAction::triggered, this, &MainWindow::onStopIndexing);
    tb->addAction(stopAct);

    tb->addSeparator();

    auto* settingsAct = new QAction(style()->standardIcon(QStyle::SP_FileDialogListView), "Settings", this);
    connect(settingsAct, &QAction::triggered, this, &MainWindow::onOpenSettings);
    tb->addAction(settingsAct);

    auto* themeAct = new QAction(style()->standardIcon(QStyle::SP_DesktopIcon), "Theme", this);
    connect(themeAct, &QAction::triggered, this, &MainWindow::onToggleTheme);
    tb->addAction(themeAct);
}

void MainWindow::applyTheme() {
    // Theme disabled for now — the QSS was causing a blank white window.
    // Using native Windows styling instead. Re-enable once the QSS is fixed.
    // Theme::apply(settings_.darkMode ? Theme::Mode::Dark : Theme::Mode::Light);
}

void MainWindow::loadSettings() {
    settings_ = Config::instance().load();
    darkMode_ = settings_.darkMode;
}

void MainWindow::saveSettings() {
    Config::instance().save(settings_);
}

void MainWindow::refreshSavedSearches() {
    auto list = repo_->savedSearches();
    QStringList names;
    for (const auto& p : list) names << p.second;
    searchBar_->setSavedSearches(names);
}

// ============================================================
// Search & results
// ============================================================
void MainWindow::onSearch(const QString& query) {
    if (query.isEmpty()) {
        resultsPane_->clear();
        return;
    }
    try {
        QElapsedTimer t; t.start();
        auto hits = search_->search(query, 500);
        resultsPane_->setResults(hits);
        statusBar()->showMessage(QString("%1 result%2 in %3 ms")
                                 .arg(hits.size())
                                 .arg(hits.size() == 1 ? "" : "s")
                                 .arg(t.elapsed()));
    } catch (...) {
        statusBar()->showMessage("Search error - try a different query");
    }
}

void MainWindow::onLiveSearchTick() {
    onSearch(searchBar_->text());
}

void MainWindow::onFileSelected(qint64 fileId, const QString& path) {
    try {
        FileRecord r;
        if (fileId != 0 && repo_ && repo_->getById(fileId, r)) {
            metadataPane_->setRecord(r);
            tagsNotesPane_->setFileId(fileId);
            tagsNotesPane_->setTags(r.tags);
            tagsNotesPane_->setNote(r.note);
        }
        previewPane_->setFilePath(path);

        // Load extracted text from DB for preview
        if (fileId != 0 && db_) {
            auto* raw = db_->raw();
            if (raw) {
                sqlite3_stmt* s = nullptr;
                if (sqlite3_prepare_v2(raw, "SELECT extracted_text FROM DocumentText WHERE file_id = ?;",
                                       -1, &s, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int64(s, 1, fileId);
                    if (sqlite3_step(s) == SQLITE_ROW) {
                        const unsigned char* t = sqlite3_column_text(s, 0);
                        if (t) {
                            QString text = QString::fromUtf8(reinterpret_cast<const char*>(t));
                            if (text.size() > 50000) text = text.left(50000) + "\n\n... (truncated)";
                            previewPane_->setExtractedText(text);
                        } else {
                            previewPane_->setExtractedText("No content extracted. Use Index -> Extract Content.");
                        }
                    } else {
                        previewPane_->setExtractedText("No content extracted. Use Index -> Extract Content.");
                    }
                    sqlite3_finalize(s);
                }
            }
        }
        previewPane_->setThumbnail(QPixmap());
    } catch (...) {
        // Silently ignore errors from file selection
    }
}

void MainWindow::onFileActivated(qint64 fileId, const QString& path) {
    Q_UNUSED(fileId);
    openFile(path);
    if (fileId != 0) repo_->incrementOpenCount(fileId);
}

void MainWindow::onOpenOriginal(const QString& path) {
    openFile(path);
}

void MainWindow::openFile(const QString& path) {
    if (path.isEmpty()) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

// ============================================================
// Indexing
// ============================================================
void MainWindow::onStartIndexing() {
    if (!indexer_) {
        QMessageBox::information(this, "Indexing Unavailable",
            "The indexing subsystem is disabled in this build.\n\n"
            "To add files to the search index, use:\n"
            "  Index -> Add Folder to Index\n\n"
            "This manually scans a folder and adds files to the database.");
        return;
    }
    if (settings_.indexedDrives.isEmpty()) {
        QMessageBox::information(this, "No Drives Configured",
            "Please add drives in Settings -> Indexing first.");
        onOpenSettings();
        return;
    }
    if (indexer_->isRunning()) {
        statusBar()->showMessage("Indexing already running.");
        return;
    }
    indexer_->startIndexing(settings_);
}

void MainWindow::onStopIndexing() {
    if (!indexer_) return;
    indexer_->stopIndexing();
    statusBar()->showMessage("Indexing stopped.");
}

void MainWindow::onPauseIndexing() {
    if (!indexer_) return;
    indexer_->pause();
    statusBar()->showMessage("Indexing paused.");
}

void MainWindow::onResumeIndexing() {
    if (!indexer_) return;
    indexer_->resume();
    statusBar()->showMessage("Indexing resumed.");
}

void MainWindow::onIndexingProgress(const DocuSearch::IndexingProgress& p) {
    indexingWidget_->update(p);
}

void MainWindow::onPhaseChanged(const QString& phase) {
    indexingWidget_->setPhase(phase);
    statusBar()->showMessage(phase);
}

void MainWindow::onIndexingStarted() {
    statusBar()->showMessage("Indexing started…");
}

void MainWindow::onIndexingFinished() {
    statusBar()->showMessage("Indexing finished.", 5000);
}

// ============================================================
// File watcher
// ============================================================
void MainWindow::onFileAdded(const QString& path) {
    if (FileUtils::isUnderAny(path, settings_.excludedFolders)) return;
    const QFileInfo fi(path);
    FileRecord r;
    r.path         = FileUtils::toNative(path);
    r.filename     = fi.fileName();
    r.extension    = FileUtils::extensionOf(path);
    r.size         = fi.size();
    r.createdDate  = fi.birthTime();
    r.modifiedDate = fi.lastModified();
    r.indexingStatus = Constants::IndexingStatus::kPending;
    r.ocrStatus      = Constants::OcrStatus::kPending;
    repo_->upsertFile(r);
    DS_INFO("Watcher", "Added: " + path);
}

void MainWindow::onFileModified(const QString& path) {
    if (!indexer_) return;  // indexer disabled in this build
    FileRecord r;
    if (repo_->getByPath(path, r)) {
        indexer_->reindexFile(path);
        DS_INFO("Watcher", "Reindexing modified: " + path);
    } else {
        onFileAdded(path);  // might be a fresh file
    }
}

void MainWindow::onFileRenamed(const QString& oldPath, const QString& newPath) {
    repo_->deleteByPath(oldPath);
    onFileAdded(newPath);
    DS_INFO("Watcher", QString("Renamed: %1 -> %2").arg(oldPath, newPath));
}

void MainWindow::onFileDeleted(const QString& path) {
    repo_->deleteByPath(path);
    DS_INFO("Watcher", "Deleted: " + path);
}

// ============================================================
// Saved searches
// ============================================================
void MainWindow::onSavedSearchSelected(const QString& name) {
    auto list = repo_->savedSearches();
    for (const auto& p : list) {
        if (p.second == name) {
            const QString q = repo_->savedSearchQuery(p.first);
            searchBar_->setText(q);
            onSearch(q);
            return;
        }
    }
}

// ============================================================
// Tags & notes
// ============================================================
void MainWindow::onTagAdded(qint64 fileId, const QString& tag)   { repo_->addTag(fileId, tag); }
void MainWindow::onTagRemoved(qint64 fileId, const QString& tag) { repo_->removeTag(fileId, tag); }
void MainWindow::onNoteChanged(qint64 fileId, const QString& note) { repo_->setNote(fileId, note); }

// ============================================================
// Settings & theme
// ============================================================
void MainWindow::onOpenSettings() {
    SettingsDialog dlg(settings_, this);
    if (dlg.exec() == QDialog::Accepted) {
        settings_ = dlg.result();
        saveSettings();
        applyTheme();
        ocrPool_->setAppSettings(settings_);
        if (settings_.monitorFileChanges && !settings_.indexedDrives.isEmpty()) {
            watcher_->stop();
            watcher_->addWatches(settings_.indexedDrives);
        }
        // Restart indexing with new settings if currently running
        if (indexer_->isRunning()) {
            indexer_->stopIndexing();
            QTimer::singleShot(500, this, &MainWindow::onStartIndexing);
        }
    }
}

void MainWindow::onToggleTheme() {
    settings_.darkMode = !settings_.darkMode;
    saveSettings();
    applyTheme();
}

void MainWindow::onAbout() {
    QMessageBox::about(this, "About DocuSearch",
        QString("<h3>%1 %2</h3>"
                "<p>Offline Intelligent Document Search &amp; OCR System.</p>"
                "<p>Built with C++20, Qt 6, SQLite + FTS5, Tesseract OCR, and Poppler.</p>"
                "<p>Completely offline. No cloud. No telemetry.</p>")
        .arg(Constants::kAppName, Constants::kAppVersion));
}

void MainWindow::onExportCsv() {
    const QString path = QFileDialog::getSaveFileName(
        this, "Export results as CSV", "docusearch_results.csv", "CSV (*.csv)");
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Export", "Cannot write to file.");
        return;
    }
    QTextStream s(&f);
    s.setEncoding(QStringConverter::Utf8);
    s << "filename,path,extension,size,modified_date\n";
    // Pull current results via re-running search
    auto hits = search_->search(searchBar_->text(), 10000);
    for (const auto& h : hits) {
        s << "\"" << h.filename << "\","
          << "\"" << h.path << "\","
          << h.extension << ","
          << h.size << ","
          << h.modifiedDate.toString(Qt::ISODate) << "\n";
    }
    s.flush();
    QMessageBox::information(this, "Export",
        QString("Exported %1 rows to %2").arg(hits.size()).arg(path));
}

void MainWindow::onDetectDuplicates() {
    auto groups = repo_->duplicatesByHash();
    if (groups.isEmpty()) {
        QMessageBox::information(this, "Duplicates", "No duplicates found by hash.");
        return;
    }
    // Build a search-like results list
    QList<SearchHit> hits;
    for (const auto& g : groups) {
        for (const auto id : g) {
            FileRecord r;
            if (repo_->getById(id, r)) {
                SearchHit h;
                h.fileId      = r.id;
                h.path        = r.path;
                h.filename    = r.filename;
                h.extension   = r.extension;
                h.size        = r.size;
                h.modifiedDate= r.modifiedDate;
                hits.append(h);
            }
        }
    }
    resultsPane_->setResults(hits);
    statusBar()->showMessage(
        QString("Found %1 duplicate groups (%2 files)").arg(groups.size()).arg(hits.size()));
}

} // namespace DocuSearch
