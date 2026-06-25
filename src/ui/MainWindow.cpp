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
#include <QGuiApplication>
#include <QScreen>
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
#include <QInputDialog>
#include <QPalette>
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QElapsedTimer>

#include <sqlite3.h>

#include <memory>

namespace DocuSearch {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {

    setWindowTitle(QString("%1 %2 - Offline Document Search  |  by MinZ")
                   .arg(Constants::kAppName, Constants::kAppVersion));

    // --- Window sizing: 80% of available screen, capped at 1280x720, centered.
    QScreen* screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect avail = screen->availableGeometry();
        int w = qMin<int>(1280, int(avail.width()  * 0.8));
        int h = qMin<int>(720,  int(avail.height() * 0.8));
        resize(w, h);
        move(avail.x() + (avail.width()  - w) / 2,
             avail.y() + (avail.height() - h) / 2);
    } else {
        resize(1280, 720);
    }
    setMinimumSize(800, 500);

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

    // Auto-scan timer — fires every 60 seconds, rescans indexed folders.
    autoScanTimer_ = new QTimer(this);
    autoScanTimer_->setInterval(60 * 1000);
    connect(autoScanTimer_, &QTimer::timeout, this, &MainWindow::autoScanIndexedFolders);
    autoScanTimer_->start();

    refreshSavedSearches();
    statusBar()->showMessage("Ready. Add files via Index -> Add Folder to Index to begin.");
}

MainWindow::~MainWindow() {
    if (autoScanTimer_) autoScanTimer_->stop();
    if (indexer_) indexer_->stopIndexing();
    if (ocrPool_) ocrPool_->shutdown();
    if (watcher_) watcher_->stop();
    if (db_)      db_->close();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    saveSettings();
    if (autoScanTimer_) autoScanTimer_->stop();
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
    leftWidget->setMinimumWidth(250);
    auto* leftLay = new QVBoxLayout(leftWidget);
    leftLay->setContentsMargins(8, 8, 8, 8);
    searchBar_ = new SearchBar(leftWidget);
    resultsPane_ = new ResultsPane(leftWidget);
    leftLay->addWidget(searchBar_);
    leftLay->addWidget(resultsPane_, 1);
    mainSplitter_->addWidget(leftWidget);

    // Middle: preview (top) + tags/notes (bottom)
    auto* middleWidget = new QWidget(this);
    middleWidget->setMinimumWidth(220);
    auto* middleLay = new QVBoxLayout(middleWidget);
    middleLay->setContentsMargins(4, 4, 4, 4);
    middleLay->setSpacing(4);
    previewPane_ = new PreviewPane(middleWidget);
    middleLay->addWidget(previewPane_, 1);
    tagsNotesPane_ = new TagsNotesPane(middleWidget);
    tagsNotesPane_->setMaximumHeight(160);
    middleLay->addWidget(tagsNotesPane_);
    mainSplitter_->addWidget(middleWidget);

    // Right: metadata + indexing status (narrower)
    rightSplitter_ = new QSplitter(Qt::Vertical, this);
    rightSplitter_->setMinimumWidth(180);
    rightSplitter_->setMaximumWidth(300);
    metadataPane_   = new MetadataPane(rightSplitter_);
    metadataPane_->setMinimumHeight(150);
    indexingWidget_ = new IndexingProgressWidget(rightSplitter_);
    indexingWidget_->setMinimumHeight(100);

    rightSplitter_->addWidget(metadataPane_);
    rightSplitter_->addWidget(indexingWidget_);
    mainSplitter_->addWidget(rightSplitter_);

    // Stretch factors: left=45, middle=35, right=20.
    mainSplitter_->setStretchFactor(0, 45);
    mainSplitter_->setStretchFactor(1, 35);
    mainSplitter_->setStretchFactor(2, 20);

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
    fileMenu->addAction("Save Current Search...", this, [this]{
        const QString q = searchBar_->text().trimmed();
        if (q.isEmpty()) {
            QMessageBox::information(this, "Save Search",
                "Type a search query first.");
            return;
        }
        bool ok = false;
        const QString name = QInputDialog::getText(this,
            "Save Current Search", "Name:",
            QLineEdit::Normal, QString(), &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        repo_->saveSearch(name, q);
        refreshSavedSearches();
        statusBar()->showMessage("Saved search: " + name, 3000);
    });
    fileMenu->addSeparator();
    fileMenu->addAction("Quit", QKeySequence::Quit,
        qApp, &QApplication::quit);

    // Index menu
    auto* indexMenu = menuBar()->addMenu("&Index");
    indexMenu->addAction("Add Folder to Index...", this, [this]{ onAddFolder(); });
    indexMenu->addAction("Extract Content Now", this, [this]{ onExtract(); });
    indexMenu->addSeparator();
    indexMenu->addAction("Clear Index...", this, [this]{
        const auto rc = QMessageBox::question(this, "Clear Index",
            "This will permanently delete ALL files, content, tags, and notes "
            "from the database.\n\nAre you sure?",
            QMessageBox::Yes | QMessageBox::No);
        if (rc != QMessageBox::Yes) return;
        db_->exec("DELETE FROM Files;");
        db_->exec("DELETE FROM DocumentText;");
        db_->exec("DELETE FROM Tags;");
        db_->exec("DELETE FROM Notes;");
        db_->exec("DELETE FROM SearchIndex;");
        db_->exec("DELETE FROM IndexingLog;");
        db_->exec("VACUUM;");
        resultsPane_->clear();
        updateIndexStats();
        statusBar()->showMessage("Index cleared.", 5000);
    });
    indexMenu->addAction("Index Statistics", this, [this]{
        const qint64 total       = repo_->totalFiles();
        const qint64 contentDone = repo_->countByStatus(Constants::IndexingStatus::kContentDone);
        const qint64 metaOnly    = repo_->countByStatus(Constants::IndexingStatus::kMetadataOnly);
        const qint64 failed      = repo_->countByStatus(Constants::IndexingStatus::kFailed);
        QMessageBox::information(this, "Index Statistics",
            QString("Files: %1\n"
                    "Content indexed: %2\n"
                    "Metadata only: %3\n"
                    "Failed: %4\n\n"
                    "Database: %5")
                .arg(total).arg(contentDone).arg(metaOnly).arg(failed)
                .arg(Config::instance().dbPath()));
    });
    indexMenu->addSeparator();
    indexMenu->addAction("Detect Duplicates", this, &MainWindow::onDetectDuplicates);

    // View menu
    auto* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction("Toggle Dark/Light Theme", QKeySequence("F11"),
        this, &MainWindow::onToggleTheme);
    viewMenu->addAction("Show Favorites Only", this, [this]{
        searchBar_->setText("favorite:1");
        onSearch("favorite:1");
    });

    // Tools menu
    auto* toolsMenu = menuBar()->addMenu("&Tools");
    toolsMenu->addAction("Settings…", this, &MainWindow::onOpenSettings);

    // Help menu
    auto* helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("How to Search", this, [this]{
        QMessageBox::information(this, "How to Search",
            "<h3>Search Syntax</h3>"
            "<table cellspacing='6'>"
            "<tr><td><b>gold bin</b></td><td>Files containing BOTH 'gold' AND 'bin' (any order)</td></tr>"
            "<tr><td><b>gold+bin</b></td><td>Same as above (+ = space)</td></tr>"
            "<tr><td><b>bin gold</b></td><td>Same results as 'gold bin' (order doesn't matter)</td></tr>"
            "<tr><td><b>\"gold bin\"</b></td><td>Exact phrase 'gold bin' in filename or content</td></tr>"
            "<tr><td><b>gold -draft</b></td><td>Files with 'gold' but NOT 'draft'</td></tr>"
            "<tr><td><b>rail*</b></td><td>Prefix wildcard: railway, railroad, rails, etc.</td></tr>"
            "</table>"
            "<h3>Filters</h3>"
            "<table cellspacing='6'>"
            "<tr><td><b>type:pdf</b></td><td>Only PDF files</td></tr>"
            "<tr><td><b>type:docx</b></td><td>Only Word documents</td></tr>"
            "<tr><td><b>type:xlsx</b></td><td>Only Excel files</td></tr>"
            "<tr><td><b>folder:Railway</b></td><td>Only files in folders containing 'Railway'</td></tr>"
            "<tr><td><b>date:2026</b></td><td>Only files modified in 2026</td></tr>"
            "<tr><td><b>tag:Urgent</b></td><td>Only files tagged 'Urgent'</td></tr>"
            "<tr><td><b>favorite:1</b></td><td>Only favorite files</td></tr>"
            "</table>"
            "<h3>Examples</h3>"
            "<table cellspacing='6'>"
            "<tr><td><b>type:pdf NOC</b></td><td>PDF files with 'NOC' in name or content</td></tr>"
            "<tr><td><b>folder:Railway type:docx</b></td><td>Word docs in Railway folder</td></tr>"
            "<tr><td><b>date:2026 report -draft</b></td><td>2026 files with 'report' but not 'draft'</td></tr>"
            "</table>"
            "<p><i>Tip: Search finds matches in both filenames AND content (after extraction).</i></p>");
    });
    helpMenu->addAction("About DocuSearch", this, &MainWindow::onAbout);
}

void MainWindow::buildToolbar() {
    auto* tb = addToolBar("Main");
    tb->setMovable(false);
    tb->setIconSize(QSize(20, 20));

    auto* addFolderAct = new QAction(style()->standardIcon(QStyle::SP_DirOpenIcon), "Add Folder", this);
    connect(addFolderAct, &QAction::triggered, this, [this]{ onAddFolder(); });
    tb->addAction(addFolderAct);

    auto* extractAct = new QAction(style()->standardIcon(QStyle::SP_DialogSaveButton), "Extract", this);
    connect(extractAct, &QAction::triggered, this, [this]{ onExtract(); });
    tb->addAction(extractAct);

    tb->addSeparator();

    auto* settingsAct = new QAction(style()->standardIcon(QStyle::SP_FileDialogListView), "Settings", this);
    connect(settingsAct, &QAction::triggered, this, &MainWindow::onOpenSettings);
    tb->addAction(settingsAct);

    auto* themeAct = new QAction(style()->standardIcon(QStyle::SP_DesktopIcon), "Theme", this);
    connect(themeAct, &QAction::triggered, this, &MainWindow::onToggleTheme);
    tb->addAction(themeAct);

    auto* dupesAct = new QAction(style()->standardIcon(QStyle::SP_BrowserReload), "Duplicates", this);
    connect(dupesAct, &QAction::triggered, this, &MainWindow::onDetectDuplicates);
    tb->addAction(dupesAct);
}

void MainWindow::applyTheme() {
    // Apply the initial palette based on saved darkMode_.
    QPalette pal;
    if (darkMode_) {
        pal.setColor(QPalette::Window,          QColor(0x20, 0x20, 0x20));
        pal.setColor(QPalette::Base,            QColor(0x2A, 0x2A, 0x2A));
        pal.setColor(QPalette::AlternateBase,   QColor(0x26, 0x26, 0x26));
        pal.setColor(QPalette::WindowText,      QColor(0xE0, 0xE0, 0xE0));
        pal.setColor(QPalette::Text,            QColor(0xE0, 0xE0, 0xE0));
        pal.setColor(QPalette::ButtonText,      QColor(0xE0, 0xE0, 0xE0));
        pal.setColor(QPalette::Button,          QColor(0x2A, 0x2A, 0x2A));
        pal.setColor(QPalette::Highlight,       QColor(0x00, 0x78, 0xD4));
        pal.setColor(QPalette::HighlightedText, QColor(0xFF, 0xFF, 0xFF));
        pal.setColor(QPalette::ToolTipBase,     QColor(0x40, 0x40, 0x40));
        pal.setColor(QPalette::ToolTipText,     QColor(0xE0, 0xE0, 0xE0));
    } else {
        pal.setColor(QPalette::Window,          QColor(0xF3, 0xF3, 0xF3));
        pal.setColor(QPalette::Base,            QColor(0xFF, 0xFF, 0xFF));
        pal.setColor(QPalette::AlternateBase,   QColor(0xF9, 0xF9, 0xF9));
        pal.setColor(QPalette::WindowText,      QColor(0x20, 0x20, 0x20));
        pal.setColor(QPalette::Text,            QColor(0x20, 0x20, 0x20));
        pal.setColor(QPalette::ButtonText,      QColor(0x20, 0x20, 0x20));
        pal.setColor(QPalette::Button,          QColor(0xF3, 0xF3, 0xF3));
        pal.setColor(QPalette::Highlight,       QColor(0x00, 0x78, 0xD4));
        pal.setColor(QPalette::HighlightedText, QColor(0xFF, 0xFF, 0xFF));
        pal.setColor(QPalette::ToolTipBase,     QColor(0xFF, 0xFF, 0xFF));
        pal.setColor(QPalette::ToolTipText,     QColor(0x20, 0x20, 0x20));
    }
    pal.setColor(QPalette::Disabled, QPalette::WindowText,  QColor(160, 160, 160));
    pal.setColor(QPalette::Disabled, QPalette::Text,        QColor(160, 160, 160));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText,  QColor(160, 160, 160));
    QApplication::setPalette(pal);
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
    FileRecord r;
    if (fileId != 0 && repo_->getById(fileId, r)) {
        metadataPane_->setRecord(r);
        tagsNotesPane_->setFileId(fileId);
        tagsNotesPane_->setTags(r.tags);
        tagsNotesPane_->setNote(r.note);
    }
    previewPane_->setFilePath(path);

    // Thumbnail generator (thumbs_) is not constructed in this build — just
    // clear any existing preview rather than crashing on a null pointer.
    previewPane_->setThumbnail(QPixmap());

    // Load extracted text from the DocumentText table via a direct
    // SQLite query (avoids depending on the null indexer_).
    QString extracted;
    if (fileId != 0) {
        sqlite3* raw = db_->raw();
        if (raw) {
            sqlite3_stmt* s = nullptr;
            if (sqlite3_prepare_v2(raw,
                    "SELECT extracted_text FROM DocumentText WHERE file_id = ?1;",
                    -1, &s, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(s, 1, fileId);
                if (sqlite3_step(s) == SQLITE_ROW) {
                    const unsigned char* t = sqlite3_column_text(s, 0);
                    if (t) extracted = QString::fromUtf8(
                                          reinterpret_cast<const char*>(t));
                }
                sqlite3_finalize(s);
            }
        }
    }
    previewPane_->setExtractedText(extracted.isEmpty()
        ? "No content extracted for this file."
        : extracted);
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
// Folder scan + content extraction
// ============================================================
void MainWindow::onAddFolder() {
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
        // Quick scan: pending for documents/images, not_needed otherwise.
        if (Constants::kDocumentExtensions.contains(r.extension) ||
            Constants::kImageExtensions.contains(r.extension)) {
            r.ocrStatus = Constants::OcrStatus::kPending;
        } else {
            r.ocrStatus = Constants::OcrStatus::kNotNeeded;
        }
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
                // Failed extraction — mark file so it can be re-tried later.
                repo_->updateStatus(fileId, Constants::IndexingStatus::kFailed);
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

    updateIndexStats();
    statusBar()->showMessage(QString("Indexed %1 files (%2 with content) from %3")
        .arg(count).arg(contentCount).arg(folder));
    QMessageBox::information(this, "Indexing Complete",
        QString("Added %1 files to the search index.\n"
                "%2 files have extracted text content.\n\n"
                "You can now search by filename AND content.").arg(count).arg(contentCount));
}

void MainWindow::onExtract() {
    if (contentExtractionRunning_) {
        statusBar()->showMessage("Content extraction already running.");
        return;
    }

    // Gather files needing content extraction on the main thread.
    struct TodoItem { qint64 fileId; QString path; QString ext; };
    QList<TodoItem> todo;
    sqlite3* raw = db_->raw();
    if (raw) {
        sqlite3_stmt* s = nullptr;
        // Re-extract any file that isn't content_done (this re-tries failed).
        // Include PDF/DOC/XLS/PPT (plus the other supported extensions).
        const char* sql =
            "SELECT id, path, extension FROM Files "
            "WHERE indexing_status != 'content_done' "
            "AND extension IN ("
            "'pdf','doc','docx',"
            "'xls','xlsx','xlsm',"
            "'ppt','pptx',"
            "'txt','csv','md','rtf') "
            "ORDER BY id;";
        if (sqlite3_prepare_v2(raw, sql, -1, &s, nullptr) == SQLITE_OK) {
            while (sqlite3_step(s) == SQLITE_ROW) {
                TodoItem it;
                it.fileId = sqlite3_column_int64(s, 0);
                const unsigned char* p = sqlite3_column_text(s, 1);
                const unsigned char* e = sqlite3_column_text(s, 2);
                it.path = p ? QString::fromUtf8(reinterpret_cast<const char*>(p)) : QString();
                it.ext  = e ? QString::fromUtf8(reinterpret_cast<const char*>(e)) : QString();
                todo.append(it);
            }
            sqlite3_finalize(s);
        }
    }

    if (todo.isEmpty()) {
        statusBar()->showMessage("No files need content extraction.", 3000);
        return;
    }

    contentExtractionRunning_ = true;
    statusBar()->showMessage(
        QString("Extracting content from %1 files...").arg(todo.size()));

    (void)QtConcurrent::run([this, todo]{
        auto& registry = DocumentExtractorRegistry::instance();
        int done = 0, failed = 0;
        for (const auto& item : todo) {
            try {
                auto result = registry.extractByExtension(item.path, item.ext);
                if (!result.text.isEmpty()) {
                    repo_->updateContent(item.fileId, result.text, result.source,
                                         Constants::IndexingStatus::kContentDone,
                                         Constants::OcrStatus::kNotNeeded);
                    ++done;
                } else {
                    repo_->updateStatus(item.fileId,
                                        Constants::IndexingStatus::kFailed);
                    ++failed;
                }
            } catch (...) {
                // Failed extraction — mark file as failed so it can be re-tried.
                repo_->updateStatus(item.fileId,
                                    Constants::IndexingStatus::kFailed);
                ++failed;
            }
        }

        QMetaObject::invokeMethod(this, [this, done, failed]{
            contentExtractionRunning_ = false;
            updateIndexStats();
            statusBar()->showMessage(
                QString("Extracted content for %1 files (%2 failed).")
                    .arg(done).arg(failed), 5000);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::autoScanIndexedFolders() {
    // Crash guard: skip if content extraction is running.
    if (contentExtractionRunning_) return;

    // Gather unique top-level folders from indexed file paths.
    QSet<QString> folders;
    sqlite3* raw = db_->raw();
    if (raw) {
        sqlite3_stmt* s = nullptr;
        if (sqlite3_prepare_v2(raw, "SELECT path FROM Files;",
                               -1, &s, nullptr) == SQLITE_OK) {
            while (sqlite3_step(s) == SQLITE_ROW) {
                const unsigned char* t = sqlite3_column_text(s, 0);
                if (!t) continue;
                const QString path = QString::fromUtf8(
                    reinterpret_cast<const char*>(t));
                const QFileInfo fi(path);
                const QString absPath = fi.absolutePath();
                // Top-level folder: first 2 path segments.
                // Windows: "D:/Documents/sub" -> "D:/Documents"
                // Unix:    "/home/user/docs"  -> "/home/user"
                QStringList parts = absPath.split('/', Qt::SkipEmptyParts);
                QString top;
                if (parts.size() >= 2) {
                    if (absPath.startsWith('/'))
                        top = "/" + parts[0] + "/" + parts[1];
                    else
                        top = parts[0] + "/" + parts[1];
                } else if (!absPath.isEmpty()) {
                    top = absPath;
                }
                if (!top.isEmpty())
                    folders.insert(QDir::toNativeSeparators(top));
            }
            sqlite3_finalize(s);
        }
    }
    if (folders.isEmpty()) return;

    const QStringList folderList(folders.begin(), folders.end());

    (void)QtConcurrent::run([this, folderList]{
        int newFiles = 0, updatedFiles = 0;
        for (const auto& folder : folderList) {
            QStringList emptyExcludes;
            FileUtils::walkDirectory(folder, emptyExcludes,
                [&](const QFileInfo& fi) -> bool {
                    const QString path = FileUtils::toNative(fi.absoluteFilePath());
                    FileRecord existing;
                    const bool isNew = !repo_->getByPath(path, existing);
                    FileRecord r;
                    r.path         = path;
                    r.filename     = fi.fileName();
                    r.extension    = FileUtils::extensionOf(fi.absoluteFilePath());
                    r.size         = fi.size();
                    r.createdDate  = fi.birthTime();
                    r.modifiedDate = fi.lastModified();
                    r.indexingStatus = Constants::IndexingStatus::kMetadataOnly;
                    // Quick scan: pending for documents/images, not_needed otherwise.
                    if (Constants::kDocumentExtensions.contains(r.extension) ||
                        Constants::kImageExtensions.contains(r.extension)) {
                        r.ocrStatus = Constants::OcrStatus::kPending;
                    } else {
                        r.ocrStatus = Constants::OcrStatus::kNotNeeded;
                    }
                    qint64 fileId = repo_->upsertFile(r);
                    if (fileId > 0) {
                        if (isNew) ++newFiles;
                        else       ++updatedFiles;
                    }
                    return true;
                });
        }

        QMetaObject::invokeMethod(this, [this, newFiles, updatedFiles]{
            statusBar()->showMessage(
                QString("Auto-scan: %1 new, %2 updated")
                    .arg(newFiles).arg(updatedFiles), 5000);
            updateIndexStats();
        }, Qt::QueuedConnection);
    });
}

void MainWindow::updateIndexStats() {
    if (!repo_ || !indexingWidget_) return;
    const qint64 total       = repo_->totalFiles();
    const qint64 contentDone = repo_->countByStatus(Constants::IndexingStatus::kContentDone);
    const qint64 metaOnly    = repo_->countByStatus(Constants::IndexingStatus::kMetadataOnly);
    const QString msg = QString("Files: %1 | Content: %2 | Metadata only: %3")
                            .arg(total).arg(contentDone).arg(metaOnly);
    indexingWidget_->setPhase(msg);
    // Also update the form labels inside the indexing widget
    DocuSearch::IndexingProgress p;
    p.filesScanned.store(total);
    p.documentsIndexed.store(contentDone);
    p.queueRemaining.store(metaOnly);
    indexingWidget_->update(p);
}

// ============================================================
// Indexing (legacy slots — indexer disabled in this build)
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
        darkMode_ = settings_.darkMode;
        saveSettings();
        applyTheme();
        // ocrPool_/watcher_/indexer_ are not constructed in this build —
        // skip calling them.
        updateIndexStats();
    }
}

void MainWindow::onToggleTheme() {
    darkMode_ = !darkMode_;
    settings_.darkMode = darkMode_;
    saveSettings();
    applyTheme();
}

void MainWindow::onAbout() {
    QMessageBox::about(this, "About DocuSearch",
        QString("<div style='text-align:center;'>"
                "<h2 style='color:#0078D4;'>DocuSearch %1</h2>"
                "<p>Offline Intelligent Document Search &amp; OCR System</p>"
                "<p>Completely offline. No cloud. No telemetry.</p>"
                "<hr>"
                "<p style='font-size:14px; color:#666;'>&#10084; Made with love by <b>MinZ</b></p>"
                "</div>")
        .arg(Constants::kAppVersion));
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
