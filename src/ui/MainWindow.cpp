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
#include "../core/StringUtils.h"
#include "../database/Database.h"
#include "../database/Schema.h"
#include "../database/FileRepository.h"
#include "../backup/BackupManager.h"
#include "../search/SearchEngine.h"
#include "../indexer/Indexer.h"
#include "../ocr/OcrWorkerPool.h"
#include "../ocr/OcrEngine.h"
#include "../monitoring/FileWatcher.h"
#include "../documents/DocumentExtractorRegistry.h"
#include "../preview/ThumbnailGenerator.h"
#include "../settings/SettingsManager.h"

#ifdef DOCUSEARCH_HAS_POPPLER
#  include <poppler-document.h>
#  include <poppler-page.h>
#  include <poppler-page-renderer.h>
#  include <type_traits>
#endif

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

    // Auto-scan timer: 1 hour interval, runs on MAIN THREAD (no QtConcurrent).
    // Scans indexed folders for new/modified files. processEvents() keeps UI responsive.
    autoScanTimer_ = new QTimer(this);
    autoScanTimer_->setInterval(3600 * 1000);  // 1 hour
    connect(autoScanTimer_, &QTimer::timeout, this, [this]{
        if (contentExtractionRunning_) return;
        autoScanIndexedFolders();
    });
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

    // Left: search bar + results (60% of width, min 300px)
    auto* leftWidget = new QWidget(this);
    leftWidget->setMinimumWidth(300);
    auto* leftLay = new QVBoxLayout(leftWidget);
    leftLay->setContentsMargins(8, 8, 8, 8);
    searchBar_ = new SearchBar(leftWidget);
    resultsPane_ = new ResultsPane(leftWidget);
    leftLay->addWidget(searchBar_);
    leftLay->addWidget(resultsPane_, 1);
    mainSplitter_->addWidget(leftWidget);

    // Middle: preview only (25% of width, min 200px)
    previewPane_ = new PreviewPane(this);
    previewPane_->setMinimumWidth(200);
    mainSplitter_->addWidget(previewPane_);

    // Right: metadata (top) + tags/notes (bottom) - 15% of width
    rightSplitter_ = new QSplitter(Qt::Vertical, this);
    rightSplitter_->setMinimumWidth(180);
    rightSplitter_->setMaximumWidth(280);
    metadataPane_   = new MetadataPane(rightSplitter_);
    metadataPane_->setMinimumWidth(160);
    metadataPane_->setMinimumHeight(150);
    tagsNotesPane_  = new TagsNotesPane(rightSplitter_);
    tagsNotesPane_->setMinimumWidth(160);
    tagsNotesPane_->setMinimumHeight(120);

    rightSplitter_->addWidget(metadataPane_);
    rightSplitter_->addWidget(tagsNotesPane_);
    mainSplitter_->addWidget(rightSplitter_);

    // Indexing widget exists but is NOT added to any layout (invisible).
    // Stats are shown via Index -> Index Statistics menu instead.
    indexingWidget_ = new IndexingProgressWidget(this);
    indexingWidget_->setVisible(false);

    // Stretch factors: left=60, middle=25, right=15.
    mainSplitter_->setStretchFactor(0, 60);
    mainSplitter_->setStretchFactor(1, 25);
    mainSplitter_->setStretchFactor(2, 15);

    rightSplitter_->setStretchFactor(0, 60);
    rightSplitter_->setStretchFactor(1, 40);

    // Explicit initial sizes
    const int w = width() > 0 ? width() : 1280;
    QList<int> hSizes;
    hSizes << qBound(300, int(w * 0.60), w)
           << qBound(200, int(w * 0.25), w)
           << qBound(160, int(w * 0.15), 280);
    mainSplitter_->setSizes(hSizes);

    const int h = height() > 0 ? height() : 720;
    QList<int> vSizes;
    vSizes << qMax(150, int(h * 0.60))
           << qMax(120, int(h * 0.40));
    rightSplitter_->setSizes(vSizes);

    updateIndexStats();
}

void MainWindow::buildMenus() {
    // File menu
    auto* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("Open File...", QKeySequence::Open,
        this, [this]{
            if (!repo_ || !db_) return;
            try {
                const QString p = QFileDialog::getOpenFileName(this, "Open file");
                if (!p.isEmpty()) openFile(p);
            } catch (...) {
                statusBar()->showMessage("Open file failed.", 3000);
            }
        });
    fileMenu->addAction("Export Results as CSV...", QKeySequence::Save,
        this, &MainWindow::onExportCsv);
    fileMenu->addAction("Save Current Search...", this, [this]{
        if (!repo_ || !db_) return;
        try {
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
        } catch (...) {
            statusBar()->showMessage("Save search failed.", 3000);
        }
    });
    fileMenu->addSeparator();
    fileMenu->addAction("Quit", QKeySequence::Quit,
        qApp, &QApplication::quit);

    // Index menu
    auto* indexMenu = menuBar()->addMenu("&Index");
    indexMenu->addAction("Add Folder to Index...", this, [this]{
        try { onAddFolder(); } catch (...) { statusBar()->showMessage("Add folder failed.", 3000); }
    });
    indexMenu->addAction("Extract Content Now", this, [this]{
        try { onExtract(); } catch (...) { statusBar()->showMessage("Extract failed.", 3000); }
    });
    indexMenu->addSeparator();
    indexMenu->addAction("Clear Index...", this, [this]{
        if (!repo_ || !db_) return;
        try {
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
        } catch (...) {
            statusBar()->showMessage("Clear index failed.", 3000);
        }
    });
    indexMenu->addAction("Index Statistics", this, [this]{
        if (!repo_ || !db_) return;
        try {
            const qint64 total       = repo_->totalFiles();
            const qint64 contentDone = repo_->countByStatus(Constants::IndexingStatus::kContentDone);
            const qint64 metaOnly    = repo_->countByStatus(Constants::IndexingStatus::kMetadataOnly);
            const qint64 failed      = repo_->countByStatus(Constants::IndexingStatus::kFailed);
            const qint64 tags        = repo_->getAllTags().size();
            const qint64 saved       = repo_->savedSearches().size();
            const QString dbPath     = Config::instance().dbPath();
            qint64 dbSize = 0;
            {
                QFile f(dbPath);
                if (f.exists()) dbSize = f.size();
            }
            QString dbSizeStr = Utils::formatFileSize(dbSize);

            QMessageBox::information(this, "Index Statistics",
                QString("<table cellspacing='4'>"
                        "<tr><td><b>Total files:</b></td><td>%1</td></tr>"
                        "<tr><td><b>Content indexed:</b></td><td>%2</td></tr>"
                        "<tr><td><b>Metadata only:</b></td><td>%3</td></tr>"
                        "<tr><td><b>Failed:</b></td><td>%4</td></tr>"
                        "<tr><td>&nbsp;</td><td></td></tr>"
                        "<tr><td><b>Tags:</b></td><td>%5</td></tr>"
                        "<tr><td><b>Saved searches:</b></td><td>%6</td></tr>"
                        "<tr><td>&nbsp;</td><td></td></tr>"
                        "<tr><td><b>Database:</b></td><td>%7</td></tr>"
                        "<tr><td><b>DB size:</b></td><td>%8</td></tr>"
                        "</table>")
                    .arg(total).arg(contentDone).arg(metaOnly).arg(failed)
                    .arg(tags).arg(saved)
                    .arg(dbPath).arg(dbSizeStr));
        } catch (...) {
            statusBar()->showMessage("Statistics failed.", 3000);
        }
    });
    indexMenu->addSeparator();
    indexMenu->addAction("Detect Duplicates", this, &MainWindow::onDetectDuplicates);

    // View menu
    auto* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction("Toggle Dark/Light Theme", QKeySequence("F11"),
        this, &MainWindow::onToggleTheme);
    viewMenu->addAction("Show Favorites Only", this, [this]{
        if (!repo_ || !db_) return;
        try {
            searchBar_->setText("favorite:1");
            onSearch("favorite:1");
        } catch (...) {
            statusBar()->showMessage("Filter failed.", 3000);
        }
    });

    // Tools menu
    auto* toolsMenu = menuBar()->addMenu("&Tools");
    toolsMenu->addAction("Settings...", this, &MainWindow::onOpenSettings);

    // Help menu
    auto* helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("How to Search", this, [this]{
        if (!repo_ || !db_) return;
        try {
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
        } catch (...) {
            statusBar()->showMessage("Help dialog failed.", 3000);
        }
    });
    helpMenu->addAction("About DocuSearch", this, &MainWindow::onAbout);
}

void MainWindow::buildToolbar() {
    auto* tb = addToolBar("Main");
    tb->setMovable(false);
    tb->setIconSize(QSize(20, 20));

    auto* addFolderAct = new QAction(style()->standardIcon(QStyle::SP_DirOpenIcon), "Add Folder", this);
    connect(addFolderAct, &QAction::triggered, this, [this]{
        try { onAddFolder(); } catch (...) { statusBar()->showMessage("Add folder failed.", 3000); }
    });
    tb->addAction(addFolderAct);

    auto* extractAct = new QAction(style()->standardIcon(QStyle::SP_DialogSaveButton), "Extract", this);
    connect(extractAct, &QAction::triggered, this, [this]{
        try { onExtract(); } catch (...) { statusBar()->showMessage("Extract failed.", 3000); }
    });
    tb->addAction(extractAct);

    tb->addSeparator();

    auto* settingsAct = new QAction(style()->standardIcon(QStyle::SP_FileDialogListView), "Settings", this);
    connect(settingsAct, &QAction::triggered, this, [this]{
        try { onOpenSettings(); } catch (...) { statusBar()->showMessage("Settings failed.", 3000); }
    });
    tb->addAction(settingsAct);

    auto* themeAct = new QAction(style()->standardIcon(QStyle::SP_DesktopIcon), "Theme", this);
    connect(themeAct, &QAction::triggered, this, [this]{
        try { onToggleTheme(); } catch (...) { statusBar()->showMessage("Theme toggle failed.", 3000); }
    });
    tb->addAction(themeAct);

    auto* dupesAct = new QAction(style()->standardIcon(QStyle::SP_BrowserReload), "Duplicates", this);
    connect(dupesAct, &QAction::triggered, this, [this]{
        try { onDetectDuplicates(); } catch (...) { statusBar()->showMessage("Duplicate detection failed.", 3000); }
    });
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

    // Apply to QApplication so ALL widgets (including tags/notes/preview) get it
    QApplication::setPalette(pal);

    // Force-update all child widgets to pick up the new palette immediately.
    // Some widgets (QTextEdit, QListWidget, QGroupBox) cache their palette
    // and don't repaint unless explicitly told.
    const auto widgets = findChildren<QWidget*>();
    for (QWidget* w : widgets) {
        w->setPalette(pal);
        w->update();
    }
    update();
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
    if (!repo_ || !db_ || !search_) return;
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
    try {
        onSearch(searchBar_->text());
    } catch (...) {
        // Ignore - the live search tick must never crash the UI.
    }
}

void MainWindow::onFileSelected(qint64 fileId, const QString& path) {
    if (!repo_ || !db_) return;
    // Track the selection so we can refresh the preview after a
    // background extraction run finishes.
    selectedFileId_ = fileId;
    selectedPath_   = path;
    try {
        FileRecord r;
        if (fileId != 0 && repo_->getById(fileId, r)) {
            metadataPane_->setRecord(r);
            tagsNotesPane_->setFileId(fileId);
            tagsNotesPane_->setTags(r.tags);
            tagsNotesPane_->setNote(r.note);
        }
        previewPane_->setFilePath(path);

        // Thumbnail generator (thumbs_) is not constructed in this build - just
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
    } catch (...) {
        statusBar()->showMessage("Failed to load file details.", 3000);
    }
}

void MainWindow::onFileActivated(qint64 fileId, const QString& path) {
    if (!repo_ || !db_) return;
    try {
        openFile(path);
        if (fileId != 0) repo_->incrementOpenCount(fileId);
    } catch (...) {
        statusBar()->showMessage("Failed to open file.", 3000);
    }
}

void MainWindow::onOpenOriginal(const QString& path) {
    try {
        openFile(path);
    } catch (...) {
        statusBar()->showMessage("Failed to open file.", 3000);
    }
}

void MainWindow::openFile(const QString& path) {
    if (path.isEmpty()) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

// ============================================================
// Folder scan + content extraction
// ============================================================
void MainWindow::onAddFolder() {
    if (!repo_ || !db_) return;
    QString folder;
    try {
        folder = QFileDialog::getExistingDirectory(
            this, "Select Folder to Index");
    } catch (...) {
        return;
    }
    if (folder.isEmpty()) return;

    try {
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
            // Compute SHA-256 hash for duplicate detection. We cap at
            // 64 MB so huge files (ISOs, videos) don't stall the scan
            // for seconds each. Files larger than the cap get a hash
            // of their first 64 MB, which is still good enough for
            // dedup in practice.
            if (settings_.hashLargeFiles) {
                r.hash = FileUtils::sha256OfFile(r.path, 64 * 1024 * 1024);
            }
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
                    // Failed extraction - mark file so it can be re-tried later.
                    try { repo_->updateStatus(fileId, Constants::IndexingStatus::kFailed); } catch (...) {}
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
    } catch (...) {
        statusBar()->showMessage("Folder scan failed.", 5000);
    }
}

void MainWindow::refreshPreviewForSelectedFile() {
    if (!db_ || selectedFileId_ == 0) return;
    try {
        sqlite3* raw = db_->raw();
        if (!raw) return;
        sqlite3_stmt* s = nullptr;
        if (sqlite3_prepare_v2(raw,
                "SELECT extracted_text FROM DocumentText WHERE file_id = ?1;",
                -1, &s, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(s, 1, selectedFileId_);
            QString extracted;
            if (sqlite3_step(s) == SQLITE_ROW) {
                const unsigned char* t = sqlite3_column_text(s, 0);
                if (t) extracted = QString::fromUtf8(
                                    reinterpret_cast<const char*>(t));
            }
            sqlite3_finalize(s);
            previewPane_->setExtractedText(extracted.isEmpty()
                ? "No content extracted for this file."
                : extracted);
        }
    } catch (...) {
        // Silently ignore - this is just a convenience refresh.
    }
}

void MainWindow::onExtract() {
    if (!repo_ || !db_) return;
    try {
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
            statusBar()->showMessage("No files need content extraction. "
                                     "All files are already indexed.", 3000);
            return;
        }

        contentExtractionRunning_ = true;
        // Disable the Extract button so the user can't click it again
        // and start a duplicate run. We re-enable it in the cleanup lambda.
        statusBar()->showMessage(
            QString("Extracting content from %1 files...").arg(todo.size()));
        QApplication::processEvents();

        // Run extraction in a BACKGROUND THREAD to avoid freezing the UI.
        // Previously this ran on the main thread with only
        // QApplication::processEvents() every 20 files, which made the
        // app appear unresponsive (frozen window, "Not Responding" in
        // the title bar) during extraction of large PDFs.
        //
        // We capture copies of the data we need (todo list, settings,
        // db path) so the worker thread doesn't touch any Qt widget
        // or the main thread's Database object directly. Each write
        // to the database goes through a SEPARATE sqlite3 connection
        // opened on the worker thread (SQLite handles concurrent
        // connections fine as long as we don't share connections
        // across threads).
        QList<TodoItem> todoCopy = todo;
        QString dbPath = Config::instance().dbPath();
        QString tessdataPath = settings_.tessdataPath;
        QString ocrLanguage = settings_.ocrLanguage;

        // Use QtConcurrent::run to schedule the work on a thread pool
        // worker, then dispatch the result back to the main thread.
        QFuture<void> future = QtConcurrent::run([todoCopy, dbPath, tessdataPath, ocrLanguage]() {
            // Open a SEPARATE sqlite3 connection for this worker thread.
            // The main thread's Database object uses SQLITE_OPEN_NOMUTEX
            // (single-threaded mode), so we MUST NOT use it from here.
            sqlite3* workerDb = nullptr;
            if (sqlite3_open_v2(dbPath.toUtf8().constData(), &workerDb,
                                SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX,
                                nullptr) != SQLITE_OK) {
                return;
            }

            auto& registry = DocumentExtractorRegistry::instance();
            int done = 0, failed = 0;
            const int total = todoCopy.size();

            for (const auto& item : todoCopy) {
                try {
                    auto result = registry.extractByExtension(item.path, item.ext);

                    // If Poppler returned empty text (scanned PDF) or the
                    // file is an image, run OCR via Tesseract to extract
                    // text from the image content.
                    bool needOcr = result.needsOcr ||
                                   (result.text.trimmed().isEmpty() &&
                                    (item.ext == "pdf" || item.ext == "png" ||
                                     item.ext == "jpg" || item.ext == "jpeg" ||
                                     item.ext == "bmp" || item.ext == "tiff" ||
                                     item.ext == "tif" || item.ext == "webp"));
                    if (needOcr) {
#ifdef DOCUSEARCH_HAS_TESSERACT
                        OcrEngine engine(tessdataPath, ocrLanguage);
                        if (engine.init()) {
                            // For PDFs, we need to render each page to an
                            // image first, then OCR. For images, OCR directly.
                            QString ocrText;
                            if (item.ext == "pdf") {
                                // Render each PDF page to an image and OCR it.
                                // Use Poppler's page_renderer if available.
#ifdef DOCUSEARCH_HAS_POPPLER
                                try {
                                    auto doc = poppler::document::load_from_file(
                                        item.path.toStdString());
                                    if (doc) {
                                        poppler::page_renderer renderer;
                                        renderer.set_render_hint(
                                            poppler::page_renderer::text_antialiasing);
                                        const int dpi = 200;  // higher DPI for better OCR
                                        const int maxPages = std::min(doc->pages(), 50);
                                        for (int i = 0; i < maxPages; ++i) {
                                            auto page = doc->create_page(i);
                                            if (!page) continue;
                                            auto getPagePtr = [](auto& p) -> poppler::page* {
                                                if constexpr (std::is_same_v<std::decay_t<decltype(p)>,
                                                                             poppler::page*>) {
                                                    return p;
                                                } else {
                                                    return p.get();
                                                }
                                            };
                                            poppler::page* pagePtr = getPagePtr(page);
                                            auto img_data = renderer.render_page(pagePtr, dpi, dpi);
                                            if (!img_data.is_valid()) continue;
                                            char* dataPtr = const_cast<char*>(img_data.data());
                                            QImage qimg(reinterpret_cast<const uchar*>(dataPtr),
                                                        img_data.width(), img_data.height(),
                                                        img_data.bytes_per_row(),
                                                        QImage::Format_ARGB32);
                                            QString pageText = engine.ocrImage(qimg);
                                            if (!pageText.isEmpty()) {
                                                ocrText += pageText + "\n";
                                            }
                                        }
                                    }
                                } catch (...) {}
#endif // DOCUSEARCH_HAS_POPPLER
                            } else {
                                // Image file - OCR directly.
                                ocrText = engine.ocrFile(item.path);
                            }
                            if (!ocrText.isEmpty()) {
                                if (result.text.isEmpty()) {
                                    result.text = ocrText;
                                    result.source = "ocr";
                                } else {
                                    result.text += "\n" + ocrText;
                                    result.source = "native+ocr";
                                }
                            }
                        }
#endif // DOCUSEARCH_HAS_TESSERACT
                    }

                    if (!result.text.isEmpty()) {
                        // Write to the worker thread's DB connection.
                        sqlite3_stmt* upd = nullptr;
                        sqlite3_prepare_v2(workerDb,
                            "UPDATE DocumentText SET extracted_text = ?1, source = ?2 "
                            "WHERE file_id = ?3;", -1, &upd, nullptr);
                        if (upd) {
                            sqlite3_bind_text(upd, 1, result.text.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                            sqlite3_bind_text(upd, 2, result.source.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                            sqlite3_bind_int64(upd, 3, item.fileId);
                            sqlite3_step(upd);
                            sqlite3_finalize(upd);
                        }
                        sqlite3_stmt* upd2 = nullptr;
                        sqlite3_prepare_v2(workerDb,
                            "UPDATE Files SET indexing_status = 'content_done', "
                            "ocr_status = ?2 WHERE id = ?1;", -1, &upd2, nullptr);
                        if (upd2) {
                            sqlite3_bind_int64(upd2, 1, item.fileId);
                            const char* ocrStat = needOcr ? "done" : "not_needed";
                            sqlite3_bind_text(upd2, 2, ocrStat, -1, SQLITE_TRANSIENT);
                            sqlite3_step(upd2);
                            sqlite3_finalize(upd2);
                        }
                        ++done;
                    } else {
                        sqlite3_exec(workerDb,
                            QString("UPDATE Files SET indexing_status = 'failed' WHERE id = %1;")
                                .arg(item.fileId).toUtf8().constData(),
                            nullptr, nullptr, nullptr);
                        ++failed;
                    }
                } catch (...) {
                    try {
                        sqlite3_exec(workerDb,
                            QString("UPDATE Files SET indexing_status = 'failed' WHERE id = %1;")
                                .arg(item.fileId).toUtf8().constData(),
                            nullptr, nullptr, nullptr);
                    } catch (...) {}
                    ++failed;
                }
            }

            sqlite3_close(workerDb);
        });

        // Watcher to dispatch completion back to the main thread.
        auto* watcher = new QFutureWatcher<void>(this);
        connect(watcher, &QFutureWatcher<void>::finished, this, [this, todo, watcher]() {
            contentExtractionRunning_ = false;
            updateIndexStats();
            statusBar()->showMessage(
                QString("Content extraction complete. %1 files processed.")
                    .arg(todo.size()), 5000);
            // Refresh the preview pane if a file is selected, so the
            // newly-extracted text appears immediately.
            refreshPreviewForSelectedFile();
            watcher->deleteLater();
        });
        watcher->setFuture(future);
    } catch (...) {
        contentExtractionRunning_ = false;
        statusBar()->showMessage("Content extraction failed.", 5000);
    }
}

void MainWindow::autoScanIndexedFolders() {
    if (!repo_ || !db_) return;
    if (contentExtractionRunning_) return;

    try {
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
        statusBar()->showMessage("Auto-scanning for new files...");

        // Run on MAIN THREAD (no QtConcurrent) to avoid SQLite thread-safety issues.
        int newFiles = 0, updatedFiles = 0;
        int processed = 0;
        for (const auto& folder : folderList) {
            try {
                QStringList emptyExcludes;
                FileUtils::walkDirectory(folder, emptyExcludes,
                    [&](const QFileInfo& fi) -> bool {
                        const QString path = FileUtils::toNative(fi.absoluteFilePath());
                        FileRecord existing;
                        const bool isNew = repo_->getByPath(path, existing);
                        FileRecord r;
                        r.path         = path;
                        r.filename     = fi.fileName();
                        r.extension    = FileUtils::extensionOf(fi.absoluteFilePath());
                        r.size         = fi.size();
                        r.createdDate  = fi.birthTime();
                        r.modifiedDate = fi.lastModified();
                        r.indexingStatus = Constants::IndexingStatus::kMetadataOnly;
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
                        ++processed;
                        if (processed % 200 == 0) {
                            statusBar()->showMessage(
                                QString("Auto-scan: %1 new, %2 updated...").arg(newFiles).arg(updatedFiles));
                            QApplication::processEvents();
                        }
                        return true;
                    });
            } catch (...) {}
        }

        statusBar()->showMessage(
            QString("Auto-scan: %1 new, %2 updated").arg(newFiles).arg(updatedFiles), 5000);
    } catch (...) {}
}

void MainWindow::updateIndexStats() {
    if (!repo_ || !db_ || !indexingWidget_) return;
    try {
        const qint64 total       = repo_->totalFiles();
        const qint64 contentDone = repo_->countByStatus(Constants::IndexingStatus::kContentDone);
        const qint64 metaOnly    = repo_->countByStatus(Constants::IndexingStatus::kMetadataOnly);
        // Update the form labels inside the indexing widget. The phase
        // header is now a static "INDEX STATUS" label (see issue #4).
        DocuSearch::IndexingProgress p;
        p.filesScanned.store(total);
        p.documentsIndexed.store(contentDone);
        p.queueRemaining.store(metaOnly);
        indexingWidget_->update(p);
        statusBar()->showMessage(
            QString("Files: %1 | Content: %2 | Metadata only: %3")
                .arg(total).arg(contentDone).arg(metaOnly), 5000);
    } catch (...) {
        // Stats update is best-effort - never crash the UI from a timer.
    }
}

// ============================================================
// Indexing (legacy slots - indexer disabled in this build)
// ============================================================
void MainWindow::onStartIndexing() {
    if (!repo_ || !db_) return;
    try {
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
        if (!indexer_) {
            statusBar()->showMessage("Indexer not available. Use Index -> Add Folder.");
            return;
        }
        if (indexer_->isRunning()) {
            statusBar()->showMessage("Indexing already running.");
            return;
        }
        indexer_->startIndexing(settings_);
    } catch (...) {
        statusBar()->showMessage("Start indexing failed.", 3000);
    }
}

void MainWindow::onStopIndexing() {
    if (!repo_ || !db_) return;
    try {
        if (!indexer_) return;
        indexer_->stopIndexing();
        statusBar()->showMessage("Indexing stopped.");
    } catch (...) {
        statusBar()->showMessage("Stop indexing failed.", 3000);
    }
}

void MainWindow::onPauseIndexing() {
    if (!repo_ || !db_) return;
    try {
        if (!indexer_) return;
        indexer_->pause();
        statusBar()->showMessage("Indexing paused.");
    } catch (...) {
        statusBar()->showMessage("Pause indexing failed.", 3000);
    }
}

void MainWindow::onResumeIndexing() {
    if (!repo_ || !db_) return;
    try {
        if (!indexer_) return;
        indexer_->resume();
        statusBar()->showMessage("Indexing resumed.");
    } catch (...) {
        statusBar()->showMessage("Resume indexing failed.", 3000);
    }
}

void MainWindow::onIndexingProgress(const DocuSearch::IndexingProgress& p) {
    try {
        if (indexingWidget_) indexingWidget_->update(p);
    } catch (...) {}
}

void MainWindow::onPhaseChanged(const QString& phase) {
    try {
        if (indexingWidget_) indexingWidget_->setPhase(phase);
        statusBar()->showMessage(phase);
    } catch (...) {}
}

void MainWindow::onIndexingStarted() {
    try { statusBar()->showMessage("Indexing started..."); } catch (...) {}
}

void MainWindow::onIndexingFinished() {
    try { statusBar()->showMessage("Indexing finished.", 5000); } catch (...) {}
}

// ============================================================
// File watcher
// ============================================================
void MainWindow::onFileAdded(const QString& path) {
    if (!repo_ || !db_) return;
    try {
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
    } catch (...) {
        DS_INFO("Watcher", "Failed to add: " + path);
    }
}

void MainWindow::onFileModified(const QString& path) {
    if (!repo_ || !db_) return;
    try {
        if (!indexer_) return;  // indexer disabled in this build
        FileRecord r;
        if (repo_->getByPath(path, r)) {
            indexer_->reindexFile(path);
            DS_INFO("Watcher", "Reindexing modified: " + path);
        } else {
            onFileAdded(path);  // might be a fresh file
        }
    } catch (...) {
        DS_INFO("Watcher", "Failed to handle modify: " + path);
    }
}

void MainWindow::onFileRenamed(const QString& oldPath, const QString& newPath) {
    if (!repo_ || !db_) return;
    try {
        repo_->deleteByPath(oldPath);
        onFileAdded(newPath);
        DS_INFO("Watcher", QString("Renamed: %1 -> %2").arg(oldPath, newPath));
    } catch (...) {
        DS_INFO("Watcher", "Failed to handle rename");
    }
}

void MainWindow::onFileDeleted(const QString& path) {
    if (!repo_ || !db_) return;
    try {
        repo_->deleteByPath(path);
        DS_INFO("Watcher", "Deleted: " + path);
    } catch (...) {
        DS_INFO("Watcher", "Failed to delete: " + path);
    }
}

// ============================================================
// Saved searches
// ============================================================
void MainWindow::onSavedSearchSelected(const QString& name) {
    if (!repo_ || !db_) return;
    try {
        auto list = repo_->savedSearches();
        for (const auto& p : list) {
            if (p.second == name) {
                const QString q = repo_->savedSearchQuery(p.first);
                searchBar_->setText(q);
                onSearch(q);
                return;
            }
        }
    } catch (...) {
        statusBar()->showMessage("Failed to load saved search.", 3000);
    }
}

// ============================================================
// Tags & notes
// ============================================================
void MainWindow::onTagAdded(qint64 fileId, const QString& tag) {
    if (!repo_ || !db_) return;
    try { repo_->addTag(fileId, tag); }
    catch (...) { statusBar()->showMessage("Failed to add tag.", 3000); }
}
void MainWindow::onTagRemoved(qint64 fileId, const QString& tag) {
    if (!repo_ || !db_) return;
    try { repo_->removeTag(fileId, tag); }
    catch (...) { statusBar()->showMessage("Failed to remove tag.", 3000); }
}
void MainWindow::onNoteChanged(qint64 fileId, const QString& note) {
    if (!repo_ || !db_) return;
    try { repo_->setNote(fileId, note); }
    catch (...) { statusBar()->showMessage("Failed to save note.", 3000); }
}

// ============================================================
// Settings & theme
// ============================================================
void MainWindow::onOpenSettings() {
    if (!repo_ || !db_) return;
    try {
        SettingsDialog dlg(settings_, repo_.get(), db_.get(), this);

        // Apply button: persist + live-apply without closing the dialog.
        // This lets the user change theme/threads and see the effect
        // immediately, then keep tweaking.
        QObject::connect(&dlg, &SettingsDialog::settingsApplied,
            this, [this](const AppSettings& s){
                settings_ = s;
                darkMode_ = settings_.darkMode;
                saveSettings();
                applyTheme();
                updateIndexStats();
                statusBar()->showMessage("Settings applied.", 3000);
            });

        // Restore: close DB, let BackupManager overwrite the .db file,
        // reopen DB, refresh UI. We do this here (not in the dialog)
        // because the dialog doesn't own the Database object.
        QObject::connect(&dlg, &SettingsDialog::restoreRequested,
            this, [this](const QString& zipPath){
                statusBar()->showMessage("Restoring database...", 0);
                db_->close();
                BackupManager bm;
                const bool ok = bm.restore(zipPath, Config::instance().dbPath());
                if (ok) {
                    QString err;
                    if (db_->open(Config::instance().dbPath(), &err)) {
                        statusBar()->showMessage(
                            "Database restored. Please restart DocuSearch.", 8000);
                        updateIndexStats();
                        refreshSavedSearches();
                    } else {
                        statusBar()->showMessage(
                            "Restore succeeded but reopen failed: " + err, 0);
                    }
                } else {
                    QString err;
                    db_->open(Config::instance().dbPath(), &err);
                    statusBar()->showMessage("Restore failed.", 5000);
                }
            });

        // Saved searches are persisted immediately by the dialog (via
        // repo_->saveSearch / deleteSearch), so regardless of whether
        // the user clicks OK or Cancel, refresh the dropdown in case
        // they added/removed entries.
        const int rc = dlg.exec();
        refreshSavedSearches();
        if (rc == QDialog::Accepted) {
            settings_ = dlg.result();
            darkMode_ = settings_.darkMode;
            saveSettings();
            applyTheme();
            updateIndexStats();
        }
    } catch (...) {
        statusBar()->showMessage("Settings dialog failed.", 3000);
    }
}

void MainWindow::onToggleTheme() {
    try {
        darkMode_ = !darkMode_;
        settings_.darkMode = darkMode_;
        saveSettings();
        applyTheme();
    } catch (...) {
        statusBar()->showMessage("Theme toggle failed.", 3000);
    }
}

void MainWindow::onAbout() {
    try {
        QMessageBox::about(this, "About DocuSearch",
            QString("<div style='text-align:center;'>"
                    "<h2 style='color:#0078D4;'>DocuSearch %1</h2>"
                    "<p>Offline Intelligent Document Search &amp; OCR System</p>"
                    "<p>Completely offline. No cloud. No telemetry.</p>"
                    "<hr>"
                    "<p style='font-size:14px; color:#666;'>&#10084; Made with love by <b>MinZ</b></p>"
                    "</div>")
            .arg(Constants::kAppVersion));
    } catch (...) {
        // Best-effort - never crash on about.
    }
}

void MainWindow::onExportCsv() {
    if (!repo_ || !db_ || !search_) return;
    QString path;
    try {
        path = QFileDialog::getSaveFileName(
            this, "Export results as CSV", "docusearch_results.csv", "CSV (*.csv)");
    } catch (...) {
        return;
    }
    if (path.isEmpty()) return;
    try {
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
    } catch (...) {
        statusBar()->showMessage("CSV export failed.", 3000);
    }
}

void MainWindow::onDetectDuplicates() {
    if (!repo_ || !db_) return;
    try {
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
    } catch (...) {
        statusBar()->showMessage("Duplicate detection failed.", 3000);
    }
}

} // namespace DocuSearch
