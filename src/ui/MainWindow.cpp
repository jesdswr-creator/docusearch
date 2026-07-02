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
#include "../ocr/WindowsOcrEngine.h"
#include "../monitoring/FileWatcher.h"
#include "../documents/DocumentExtractorRegistry.h"
#include "../preview/ThumbnailGenerator.h"
#include "../settings/SettingsManager.h"

#ifdef DOCUSEARCH_HAS_POPPLER
#  include <poppler-document.h>
#  include <poppler-page.h>
#  include <poppler-page-renderer.h>
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
#include <QSvgRenderer>
#include <QPainter>
#include <QFile>
#include <QListWidget>
#include <QListWidgetItem>
#include <QVBoxLayout>
#include <QHBoxLayout>

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
    connect(previewPane_, &PreviewPane::ocrRequested,
            this, &MainWindow::onOcrThisFile);

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
    // ---- New 4-area layout (sidebar | center | rightPanel) ----
    //
    //   +-----------+--------------------------+------------+
    //   | sidebar_  | toolbar_ + searchBar_    | metadataPane_ |
    //   | (180px)   | ----------------------   | (280px)       |
    //   |           | results | preview        +---------------+
    //   | nav items | (40%)  | (60%)           | tagsNotesPane_|
    //   |           |        |                 |               |
    //   | status    |        |                 |               |
    //   +-----------+--------+-----------------+---------------+
    //
    // - Sidebar: fixed 180px QListWidget with nav items + status section
    // - Center:  QVBoxLayout (toolbar, searchBar, centerSplitter)
    // - Right:   fixed 280px QVBoxLayout (metadata, tags/notes)
    //
    // All widgets are given objectNames so Theme QSS can target them
    // without any inline setStyleSheet() calls.

    auto* centralWidget = new QWidget(this);
    centralWidget->setObjectName("centralWidget");
    auto* mainLay = new QHBoxLayout(centralWidget);
    mainLay->setContentsMargins(0, 0, 0, 0);
    mainLay->setSpacing(0);
    setCentralWidget(centralWidget);

    // ============================================================
    // 1) LEFT SIDEBAR (fixed 180px)
    // ============================================================
    auto* sidebarContainer = new QWidget(centralWidget);
    sidebarContainer->setObjectName("sidebar");
    sidebarContainer->setFixedWidth(200);

    auto* sidebarLay = new QVBoxLayout(sidebarContainer);
    sidebarLay->setContentsMargins(0, 0, 0, 0);
    sidebarLay->setSpacing(0);

    // Navigation list with Lucide icons.
    sidebar_ = new QListWidget(sidebarContainer);
    sidebar_->setObjectName("sidebar");
    const QStringList navLabels = {
        "Search", "Saved", "Tags", "Notes",
        "Stats", "Recent", "Settings", "Help", "About"
    };
    const QStringList navIcons = {
        ":/icons/lucide/search.svg",
        ":/icons/lucide/clock.svg",
        ":/icons/lucide/tag.svg",
        ":/icons/lucide/sticky-note.svg",
        ":/icons/lucide/bar-chart-3.svg",
        ":/icons/lucide/clock.svg",
        ":/icons/lucide/settings.svg",
        ":/icons/lucide/help-circle.svg",
        ":/icons/lucide/info.svg"
    };

    // Icon renderer: load SVG, replace currentColor with palette text.
    auto makeSidebarIcon = [](const QString& svgPath) -> QIcon {
        QFile f(svgPath);
        if (!f.open(QIODevice::ReadOnly)) return QIcon();
        QString svg = QString::fromUtf8(f.readAll());
        f.close();
        QColor textColor = qApp->palette().color(QPalette::Text);
        svg.replace("currentColor", textColor.name());
        QSvgRenderer renderer(svg.toUtf8());
        QPixmap pm(20, 20);
        pm.fill(Qt::transparent);
        QPainter painter(&pm);
        renderer.render(&painter);
        return QIcon(pm);
    };

    for (int i = 0; i < navLabels.size(); ++i) {
        auto* item = new QListWidgetItem(makeSidebarIcon(navIcons[i]), navLabels[i], sidebar_);
        item->setData(Qt::UserRole, navLabels[i]);
        item->setSizeHint(QSize(180, 38));
    }
    if (sidebar_->count() > 0) {
        sidebar_->setCurrentRow(0);
        // Visual hint: the "Search" row is the active page.
    }
    sidebarLay->addWidget(sidebar_, 1);

    // Status section pinned to the bottom of the sidebar.
    auto* statusSection = new QWidget(sidebarContainer);
    statusSection->setObjectName("statusSection");
    auto* statusLay = new QVBoxLayout(statusSection);
    statusLay->setContentsMargins(12, 8, 12, 10);
    statusLay->setSpacing(2);

    auto* statusHeader = new QLabel("Indexed", statusSection);
    statusHeader->setObjectName("statusHeader");
    statusLay->addWidget(statusHeader);

    sidebarFileCountLabel_ = new QLabel("0 files", statusSection);
    sidebarFileCountLabel_->setObjectName("statusFileCount");
    sidebarDbSizeLabel_    = new QLabel("0 B", statusSection);
    sidebarDbSizeLabel_->setObjectName("statusDbSize");
    statusLay->addWidget(sidebarFileCountLabel_);
    statusLay->addWidget(sidebarDbSizeLabel_);

    sidebarLay->addWidget(statusSection);

    mainLay->addWidget(sidebarContainer);

    // ============================================================
    // 2) CENTER PANEL (toolbar + search bar + results|preview splitter)
    // ============================================================
    auto* centerWidget = new QWidget(centralWidget);
    centerWidget->setObjectName("centerPanel");
    auto* centerLay = new QVBoxLayout(centerWidget);
    centerLay->setContentsMargins(8, 8, 8, 8);
    centerLay->setSpacing(6);

    // Toolbar lives at the top of the center panel, above the search bar.
    // buildToolbar() (called next in the ctor) populates this widget with
    // the Add Folder / Extract / Settings / Theme / Duplicates actions.
    if (!toolbar_) {
        toolbar_ = new QToolBar(centerWidget);
        toolbar_->setObjectName("mainToolbar");
        toolbar_->setMovable(false);
        toolbar_->setIconSize(QSize(18, 18));
        toolbar_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    }
    centerLay->addWidget(toolbar_);

    // Search bar with Search button + Saved Searches dropdown + Add Folder.
    searchBar_ = new SearchBar(centerWidget);
    searchBar_->setObjectName("searchBar");
    centerLay->addWidget(searchBar_);

    // Center splitter: results (left ~40%) | preview (right ~60%).
    mainSplitter_ = new QSplitter(Qt::Horizontal, centerWidget);
    mainSplitter_->setObjectName("centerSplitter");
    centerLay->addWidget(mainSplitter_, 1);

    // Left half: results pane (ResultsPane already contains its own
    // count label internally, so we just embed it directly).
    resultsPane_ = new ResultsPane(mainSplitter_);
    resultsPane_->setObjectName("resultsPane");
    resultsPane_->setMinimumWidth(280);
    mainSplitter_->addWidget(resultsPane_);

    // Right half: preview pane.
    previewPane_ = new PreviewPane(mainSplitter_);
    previewPane_->setObjectName("previewPane");
    previewPane_->setMinimumWidth(240);
    mainSplitter_->addWidget(previewPane_);

    // Stretch factors: results=40, preview=60.
    mainSplitter_->setStretchFactor(0, 40);
    mainSplitter_->setStretchFactor(1, 60);

    // Reasonable initial sizes for the center splitter, accounting for
    // the 180px sidebar and 280px right panel already deducted.
    const int availWidth = qMax(400, width() - 180 - 280 - 16);
    QList<int> hSizes;
    hSizes << qMax(280, int(availWidth * 0.40))
           << qMax(300, int(availWidth * 0.60));
    mainSplitter_->setSizes(hSizes);

    mainLay->addWidget(centerWidget, 1);

    // ============================================================
    // 3) RIGHT PANEL (fixed 280px): metadata + tags/notes
    // ============================================================
    auto* rightPanel = new QWidget(centralWidget);
    rightPanel->setObjectName("rightPanel");
    rightPanel->setFixedWidth(300);

    auto* rightLay = new QVBoxLayout(rightPanel);
    rightLay->setContentsMargins(8, 8, 8, 8);
    rightLay->setSpacing(6);

    metadataPane_ = new MetadataPane(rightPanel);
    metadataPane_->setObjectName("metadataPane");
    metadataPane_->setMinimumHeight(150);
    rightLay->addWidget(metadataPane_, 1);

    tagsNotesPane_ = new TagsNotesPane(rightPanel);
    tagsNotesPane_->setObjectName("tagsNotesPane");
    tagsNotesPane_->setMinimumHeight(120);
    rightLay->addWidget(tagsNotesPane_, 1);

    mainLay->addWidget(rightPanel);

    // ============================================================
    // Hidden indexing widget (kept for stats plumbing; not visible
    // in the new layout — stats are surfaced via the sidebar status
    // section and the Index -> Index Statistics menu).
    // ============================================================
    indexingWidget_ = new IndexingProgressWidget(this);
    indexingWidget_->setVisible(false);

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
    // The toolbar widget itself is created in buildCentral() and lives
    // at the top of the center panel. This function only (re)populates
    // its actions, so applyTheme() can call it again to refresh icons
    // after a palette swap without recreating the widget.
    if (!toolbar_) return;

    // Delete any previously-added actions so re-populating (e.g. on
    // theme toggle) doesn't accumulate orphaned QActions.
    const auto oldActions = toolbar_->actions();
    for (QAction* act : oldActions) {
        toolbar_->removeAction(act);
        act->deleteLater();
    }

    // Modern icon + text toolbar. No inline stylesheet — Theme QSS
    // handles QToolBar and QToolButton styling.
    toolbar_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    // Create palette-aware icons. Qt's SVG renderer doesn't honor
    // 'currentColor', so we load the SVG, replace the stroke color
    // with the palette's text color, and render to a QPixmap. This
    // ensures icons are visible in both light and dark mode.
    auto makeIcon = [](const QString& svgPath) -> QIcon {
        QFile f(svgPath);
        if (!f.open(QIODevice::ReadOnly)) return QIcon();
        QString svg = QString::fromUtf8(f.readAll());
        f.close();
        // Replace 'currentColor' with the actual text color from the
        // application palette. We use the palette at icon-creation time;
        // when the theme changes, applyTheme() re-calls buildToolbar()
        // which re-renders the icons with the new palette.
        QColor textColor = qApp->palette().color(QPalette::Text);
        svg.replace("currentColor", textColor.name());
        QSvgRenderer renderer(svg.toUtf8());
        QPixmap pm(24, 24);
        pm.fill(Qt::transparent);
        QPainter painter(&pm);
        renderer.render(&painter);
        return QIcon(pm);
    };

    // Parent actions to the toolbar so they're cleaned up with it.
    auto* addFolderAct = new QAction(makeIcon(":/icons/lucide/folder-plus.svg"), "Add Folder", toolbar_);
    connect(addFolderAct, &QAction::triggered, this, [this]{
        try { onAddFolder(); } catch (...) { statusBar()->showMessage("Add folder failed.", 3000); }
    });
    toolbar_->addAction(addFolderAct);

    auto* extractAct = new QAction(makeIcon(":/icons/lucide/file-text.svg"), "Extract", toolbar_);
    connect(extractAct, &QAction::triggered, this, [this]{
        try { onExtract(); } catch (...) { statusBar()->showMessage("Extract failed.", 3000); }
    });
    toolbar_->addAction(extractAct);

    toolbar_->addSeparator();

    auto* settingsAct = new QAction(makeIcon(":/icons/lucide/settings.svg"), "Settings", toolbar_);
    connect(settingsAct, &QAction::triggered, this, [this]{
        try { onOpenSettings(); } catch (...) { statusBar()->showMessage("Settings failed.", 3000); }
    });
    toolbar_->addAction(settingsAct);

    auto* themeAct = new QAction(makeIcon(darkMode_ ? ":/icons/lucide/sun.svg" : ":/icons/lucide/moon.svg"), "Theme", toolbar_);
    connect(themeAct, &QAction::triggered, this, [this]{
        try { onToggleTheme(); } catch (...) { statusBar()->showMessage("Theme toggle failed.", 3000); }
    });
    toolbar_->addAction(themeAct);

    auto* dupesAct = new QAction(makeIcon(":/icons/lucide/duplicate.svg"), "Duplicates", toolbar_);
    connect(dupesAct, &QAction::triggered, this, [this]{
        try { onDetectDuplicates(); } catch (...) { statusBar()->showMessage("Duplicate detection failed.", 3000); }
    });
    toolbar_->addAction(dupesAct);
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

    // Apply to QApplication so ALL widgets get it
    QApplication::setPalette(pal);

    // CRITICAL: Apply the Theme QSS stylesheet. Without this, the 800+
    // lines of modern QSS in Theme.cpp (border radius, padding, hover
    // states, sidebar styling, button styles, etc.) are never applied.
    // Only QPalette is set above, which gives basic colors but NOT the
    // modern Fluent Design appearance.
    Theme::apply(darkMode_ ? Theme::Mode::Dark : Theme::Mode::Light);

    // Force-update all child widgets to pick up the new palette immediately.
    const auto widgets = findChildren<QWidget*>();
    for (QWidget* w : widgets) {
        w->setPalette(pal);
        w->update();
    }
    update();

    // Rebuild the toolbar's actions so SVG icons get re-rendered with
    // the new palette's text color (dark icons for light mode, light
    // icons for dark mode). Without this, icons stay the old color
    // after a theme toggle.
    //
    // The toolbar widget itself lives inside the center panel and is
    // reused — buildToolbar() now just clears + re-adds actions
    // rather than creating a fresh QToolBar, so we don't need to
    // delete/detach anything here.
    buildToolbar();
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
void MainWindow::scanFolderFast(const QString& folder) {
    if (!repo_ || !db_ || folder.isEmpty()) return;
    sqlite3* raw = db_->raw();
    if (!raw) return;

    int count = 0;
    QStringList emptyExcludes;
    FileUtils::walkDirectory(folder, emptyExcludes, [&](const QFileInfo& fi) -> bool {
        const QString path = FileUtils::toNative(fi.absoluteFilePath());
        const QString ext = FileUtils::extensionOf(fi.absoluteFilePath());
        const QString filename = fi.fileName();
        const qint64 size = fi.size();
        const qint64 created = fi.birthTime().toSecsSinceEpoch();
        const qint64 modified = fi.lastModified().toSecsSinceEpoch();
        const char* ocrStat = (Constants::kDocumentExtensions.contains(ext) ||
                               Constants::kImageExtensions.contains(ext))
                              ? "pending" : "not_needed";

        sqlite3_stmt* s = nullptr;
        sqlite3_prepare_v2(raw,
            "INSERT INTO Files (path, filename, extension, size, "
            "  created_date, modified_date, indexing_status, ocr_status) "
            "VALUES (?1, ?2, ?3, ?4, ?5, ?6, 'metadata_only', ?7) "
            "ON CONFLICT(path) DO UPDATE SET "
            "  filename=excluded.filename, extension=excluded.extension, "
            "  size=excluded.size, modified_date=excluded.modified_date;",
            -1, &s, nullptr);
        if (s) {
            sqlite3_bind_text(s, 1, path.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(s, 2, filename.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(s, 3, ext.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(s, 4, size);
            sqlite3_bind_int64(s, 5, created);
            sqlite3_bind_int64(s, 6, modified);
            sqlite3_bind_text(s, 7, ocrStat, -1, SQLITE_TRANSIENT);
            sqlite3_step(s);
            sqlite3_finalize(s);
        }

        ++count;
        if (count % 10 == 0) {
            statusBar()->showMessage(
                QString("Scanning... %1 files found").arg(count));
            QApplication::processEvents();
        }
        return true;
    });
    updateIndexStats();
    statusBar()->showMessage(
        QString("Scan complete: %1 files from %2").arg(count).arg(folder), 5000);
}

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
        scanFolderFast(folder);
        QMessageBox::information(this, "Scan Complete",
            QString("Files from %1 have been added to the index.\n\n"
                    "Click the Extract button to extract text content from "
                    "documents (PDF, DOCX, XLSX, etc.) so you can search "
                    "by content.").arg(folder));
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
    if (contentExtractionRunning_) {
        statusBar()->showMessage("Content extraction already running.", 3000);
        return;
    }

    // Gather files needing content extraction.
    struct TodoItem { qint64 fileId; QString path; QString ext; };
    QList<TodoItem> todo;
    {
        sqlite3* raw = db_->raw();
        if (!raw) return;
        sqlite3_stmt* s = nullptr;
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
    const int total = todo.size();
    statusBar()->showMessage(
        QString("Extracting content from %1 files...").arg(total));

    // CRITICAL: Use QSharedPointer to heap-allocate the extraction state
    // so it survives after onExtract() returns. The QTimer fires
    // asynchronously and the lambda needs access to todo/idx/done/failed.
    // Capturing by reference (&todo, &idx) would crash because those
    // stack variables are destroyed when onExtract() returns.
    struct ExtractState {
        QList<TodoItem> todo;
        int idx = 0;
        int done = 0;
        int failed = 0;
    };
    auto state = QSharedPointer<ExtractState>::create();
    state->todo = std::move(todo);

    auto* timer = new QTimer(this);
    timer->setInterval(0);

    connect(timer, &QTimer::timeout, this, [this, timer, total, state]() {
        auto& registry = DocumentExtractorRegistry::instance();
        sqlite3* raw = db_->raw();

        if (state->idx >= total) {
            timer->stop();
            timer->deleteLater();
            contentExtractionRunning_ = false;
            updateIndexStats();
            refreshPreviewForSelectedFile();
            statusBar()->showMessage(
                QString("Extraction complete: %1 succeeded, %2 failed (out of %3).")
                    .arg(state->done).arg(state->failed).arg(total), 8000);
            return;
        }

        const auto& item = state->todo[state->idx];
        statusBar()->showMessage(
            QString("Extracting: %1/%2 (done: %3, failed: %4)...")
                .arg(state->idx + 1).arg(total).arg(state->done).arg(state->failed));

        if (!QFileInfo::exists(item.path)) {
            if (raw) {
                sqlite3_exec(raw,
                    QString("UPDATE Files SET indexing_status='failed' WHERE id=%1;")
                        .arg(item.fileId).toUtf8().constData(),
                    nullptr, nullptr, nullptr);
            }
            ++state->failed;
        } else {
            QString extractedText;
            QString source = "native";
            bool ok = false;

            try {
                auto result = registry.extractByExtension(item.path, item.ext);
                extractedText = result.text;
                source = result.source.isEmpty() ? "native" : result.source;
                ok = true;
            } catch (...) {
                ok = false;
            }

            if (ok && raw) {
                QByteArray textBytes = extractedText.toUtf8();
                QByteArray srcBytes = source.toUtf8();
                qint64 charCount = extractedText.size();
                qint64 now = QDateTime::currentSecsSinceEpoch();

                sqlite3_stmt* upd = nullptr;
                sqlite3_prepare_v2(raw,
                    "INSERT INTO DocumentText (file_id, extracted_text, text_source, char_count, updated_at) "
                    "VALUES (?1, ?2, ?3, ?4, ?5) "
                    "ON CONFLICT(file_id) DO UPDATE SET "
                    "  extracted_text=excluded.extracted_text, "
                    "  text_source=excluded.text_source, "
                    "  char_count=excluded.char_count, "
                    "  updated_at=excluded.updated_at;",
                    -1, &upd, nullptr);
                if (upd) {
                    sqlite3_bind_int64(upd, 1, item.fileId);
                    sqlite3_bind_text(upd, 2, textBytes.constData(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(upd, 3, srcBytes.constData(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int64(upd, 4, charCount);
                    sqlite3_bind_int64(upd, 5, now);
                    sqlite3_step(upd);
                    sqlite3_finalize(upd);
                }

                sqlite3_exec(raw,
                    QString("UPDATE Files SET indexing_status='content_done', ocr_status='not_needed' WHERE id=%1;")
                        .arg(item.fileId).toUtf8().constData(),
                    nullptr, nullptr, nullptr);

                sqlite3_stmt* del = nullptr;
                sqlite3_prepare_v2(raw, "DELETE FROM SearchIndex WHERE file_id=?1;",
                                   -1, &del, nullptr);
                if (del) {
                    sqlite3_bind_int64(del, 1, item.fileId);
                    sqlite3_step(del);
                    sqlite3_finalize(del);
                }

                QFileInfo fi(item.path);
                QByteArray fn = fi.fileName().toUtf8();
                QByteArray pth = item.path.toUtf8();
                QByteArray ext = item.ext.toUtf8();
                sqlite3_stmt* ins = nullptr;
                sqlite3_prepare_v2(raw,
                    "INSERT INTO SearchIndex (filename, content, path, extension, file_id) "
                    "VALUES (?1, ?2, ?3, ?4, ?5);",
                    -1, &ins, nullptr);
                if (ins) {
                    sqlite3_bind_text(ins, 1, fn.constData(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(ins, 2, textBytes.constData(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(ins, 3, pth.constData(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(ins, 4, ext.constData(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int64(ins, 5, item.fileId);
                    sqlite3_step(ins);
                    sqlite3_finalize(ins);
                }
                ++state->done;
            } else if (raw) {
                sqlite3_exec(raw,
                    QString("UPDATE Files SET indexing_status='failed' WHERE id=%1;")
                        .arg(item.fileId).toUtf8().constData(),
                    nullptr, nullptr, nullptr);
                ++state->failed;
            } else {
                ++state->failed;
            }
        }

        ++state->idx;
    });

    timer->start();
}

void MainWindow::autoScanIndexedFolders() {
    if (!repo_ || !db_) return;
    if (contentExtractionRunning_) return;
    if (autoScanRunning_) return;

    autoScanRunning_ = true;
    statusBar()->showMessage("Auto-scanning for new files...");

    // Gather unique top-level folders from indexed file paths.
    QSet<QString> folders;
    {
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
    }
    if (folders.isEmpty()) {
        autoScanRunning_ = false;
        return;
    }

    const QStringList folderList(folders.begin(), folders.end());

    // Run the scan in a BACKGROUND THREAD to keep the UI responsive.
    // Previously this ran on the main thread with processEvents() every
    // 200 files, which could freeze the UI for large indexes.
    //
    // The worker thread opens its own sqlite3 connection (FULLMUTEX mode)
    // so it doesn't conflict with the main thread's Database object.
    QString dbPath = Config::instance().dbPath();
    bool hashEnabled = settings_.hashLargeFiles;
    QStringList foldersCopy = folderList;

    QFuture<void> future = QtConcurrent::run([foldersCopy, dbPath, hashEnabled]() {
        sqlite3* workerDb = nullptr;
        if (sqlite3_open_v2(dbPath.toUtf8().constData(), &workerDb,
                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX,
                            nullptr) != SQLITE_OK) {
            return;
        }

        int newFiles = 0, updatedFiles = 0;

        for (const auto& folder : foldersCopy) {
            try {
                QStringList emptyExcludes;
                FileUtils::walkDirectory(folder, emptyExcludes,
                    [&](const QFileInfo& fi) -> bool {
                        const QString path = FileUtils::toNative(fi.absoluteFilePath());

                        // Check if file already exists in DB.
                        sqlite3_stmt* chk = nullptr;
                        bool isNew = true;
                        if (sqlite3_prepare_v2(workerDb,
                                "SELECT id FROM Files WHERE path = ?1;",
                                -1, &chk, nullptr) == SQLITE_OK) {
                            sqlite3_bind_text(chk, 1, path.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                            if (sqlite3_step(chk) == SQLITE_ROW) {
                                isNew = false;
                            }
                            sqlite3_finalize(chk);
                        }

                        // Build file metadata.
                        const QString ext = FileUtils::extensionOf(fi.absoluteFilePath());
                        const QString hash = hashEnabled
                            ? FileUtils::sha256OfFile(path, 64 * 1024 * 1024)
                            : QString();
                        const qint64 size = fi.size();
                        const qint64 created = fi.birthTime().toSecsSinceEpoch();
                        const qint64 modified = fi.lastModified().toSecsSinceEpoch();

                        // Upsert (INSERT OR REPLACE on path).
                        sqlite3_stmt* upd = nullptr;
                        sqlite3_prepare_v2(workerDb,
                            "INSERT INTO Files (path, filename, extension, size, "
                            "  created_date, modified_date, hash, indexing_status, ocr_status) "
                            "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9) "
                            "ON CONFLICT(path) DO UPDATE SET "
                            "  filename=excluded.filename, extension=excluded.extension, "
                            "  size=excluded.size, modified_date=excluded.modified_date, "
                            "  hash=excluded.hash;",
                            -1, &upd, nullptr);
                        if (upd) {
                            sqlite3_bind_text(upd, 1, path.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                            sqlite3_bind_text(upd, 2, fi.fileName().toUtf8().constData(), -1, SQLITE_TRANSIENT);
                            sqlite3_bind_text(upd, 3, ext.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                            sqlite3_bind_int64(upd, 4, size);
                            sqlite3_bind_int64(upd, 5, created);
                            sqlite3_bind_int64(upd, 6, modified);
                            sqlite3_bind_text(upd, 7, hash.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                            // New files get 'metadata_only'; existing files
                            // keep their current status via the ON CONFLICT
                            // clause (we don't update indexing_status).
                            const char* idxStat = isNew ? "metadata_only" : "content_done";
                            sqlite3_bind_text(upd, 8, idxStat, -1, SQLITE_TRANSIENT);
                            const char* ocrStat = (Constants::kDocumentExtensions.contains(ext) ||
                                                   Constants::kImageExtensions.contains(ext))
                                                  ? (isNew ? "pending" : "not_needed")
                                                  : "not_needed";
                            sqlite3_bind_text(upd, 9, ocrStat, -1, SQLITE_TRANSIENT);
                            sqlite3_step(upd);
                            sqlite3_finalize(upd);
                        }

                        if (isNew) ++newFiles;
                        else ++updatedFiles;
                        return true;
                    });
            } catch (...) {}
        }

        sqlite3_close(workerDb);
    });

    auto* watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher]() {
        autoScanRunning_ = false;
        updateIndexStats();
        statusBar()->showMessage("Auto-scan complete.", 5000);
        watcher->deleteLater();
    });
    watcher->setFuture(future);
}

void MainWindow::updateIndexStats() {
    if (!repo_ || !db_) return;
    try {
        const qint64 total       = repo_->totalFiles();
        const qint64 contentDone = repo_->countByStatus(Constants::IndexingStatus::kContentDone);
        const qint64 metaOnly    = repo_->countByStatus(Constants::IndexingStatus::kMetadataOnly);

        // Update the sidebar status labels (bottom of the left nav).
        if (sidebarFileCountLabel_) {
            sidebarFileCountLabel_->setText(QString("%1 files").arg(total));
        }
        if (sidebarDbSizeLabel_) {
            qint64 dbSize = 0;
            {
                QFile f(Config::instance().dbPath());
                if (f.exists()) dbSize = f.size();
            }
            sidebarDbSizeLabel_->setText(Utils::formatFileSize(dbSize));
        }

        // Update the hidden indexing widget (kept for stats plumbing;
        // surfaces the same numbers via Index -> Index Statistics).
        if (indexingWidget_) {
            DocuSearch::IndexingProgress p;
            p.filesScanned.store(total);
            p.documentsIndexed.store(contentDone);
            p.queueRemaining.store(metaOnly);
            indexingWidget_->update(p);
        }

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
void MainWindow::onOcrThisFile(const QString& path) {
    if (!repo_ || !db_ || path.isEmpty()) return;
    if (!QFileInfo::exists(path)) {
        statusBar()->showMessage("File not found: " + path, 5000);
        return;
    }

    // Get the file's extension to decide how to OCR.
    const QString ext = FileUtils::extensionOf(path).toLower();

    // Images: OCR directly.
    // PDFs: render each page via Poppler, then OCR each page.
    // Others: not supported.
    bool isImage = (ext == "png" || ext == "jpg" || ext == "jpeg" ||
                    ext == "bmp" || ext == "tiff" || ext == "tif" ||
                    ext == "webp" || ext == "gif");
    bool isPdf = (ext == "pdf");

    if (!isImage && !isPdf) {
        QMessageBox::information(this, "OCR",
            "OCR is supported for PDF files and images (PNG, JPG, BMP, TIFF, WebP).");
        return;
    }

    // Initialize Windows OCR engine.
    WindowsOcrEngine ocrEngine;
    if (!ocrEngine.init()) {
        QMessageBox::information(this, "OCR",
            "Windows OCR could not be initialized.\n\n"
            "This is normal on some Windows 10 systems.\n"
            "OCR requires Windows 10 version 1903 or later.\n\n"
            "You can still:\n"
            "  - Search by filename\n"
            "  - Extract text from born-digital PDFs, DOCX, XLSX\n"
            "  - Use tags, notes, and saved searches");
        return;
    }

    // Show a progress dialog.
    QProgressDialog progress("Running OCR...", "Cancel", 0, 100, this);
    progress.setWindowTitle("Windows OCR");
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);
    QApplication::processEvents();

    QString ocrText;

    if (isImage) {
        progress.setLabelText("OCR image: " + QFileInfo(path).fileName());
        progress.setValue(50);
        QApplication::processEvents();
        ocrText = ocrEngine.ocrFile(path);
        progress.setValue(100);
    } else {
        // PDF: render each page via Poppler, then OCR.
#ifdef DOCUSEARCH_HAS_POPPLER
        try {
            auto doc = poppler::document::load_from_file(path.toStdString());
            if (!doc || doc->pages() == 0) {
                QMessageBox::warning(this, "OCR", "Failed to open PDF.");
                return;
            }
            poppler::page_renderer renderer;
            renderer.set_render_hint(poppler::page_renderer::text_antialiasing);
            // Use 150 DPI for OCR (good balance of accuracy + memory).
            const int dpi = 150;
            const int maxPages = std::min(doc->pages(), 30);
            progress.setMaximum(maxPages);

            for (int i = 0; i < maxPages; ++i) {
                if (progress.wasCanceled()) break;
                progress.setLabelText(
                    QString("OCR page %1/%2...").arg(i + 1).arg(maxPages));
                progress.setValue(i);
                QApplication::processEvents();

                try {
                    poppler::page* pagePtr = doc->create_page(i);
                    if (!pagePtr) continue;
                    auto img_data = renderer.render_page(pagePtr, dpi, dpi);
                    if (!img_data.is_valid()) continue;
                    char* dataPtr = const_cast<char*>(img_data.data());
                    if (!dataPtr) continue;
                    QImage qimg(reinterpret_cast<const uchar*>(dataPtr),
                                img_data.width(), img_data.height(),
                                img_data.bytes_per_row(),
                                QImage::Format_ARGB32);
                    if (qimg.isNull()) continue;
                    QString pageText = ocrEngine.ocrImage(qimg);
                    if (!pageText.isEmpty()) {
                        ocrText += pageText + "\n";
                    }
                } catch (...) {
                    // Skip this page, continue to next.
                }
            }
            progress.setValue(maxPages);
        } catch (const std::exception& e) {
            QMessageBox::warning(this, "OCR",
                QString("PDF rendering failed: %1").arg(e.what()));
            return;
        } catch (...) {
            QMessageBox::warning(this, "OCR", "PDF rendering failed.");
            return;
        }
#else
        QMessageBox::warning(this, "OCR",
            "PDF rendering is not available in this build (Poppler not linked).");
        return;
#endif
    }

    if (ocrText.isEmpty()) {
        QMessageBox::information(this, "OCR",
            "No text was recognized. The file may not contain "
            "recognizable text, or the image quality is too low.");
        return;
    }

    // Save OCR text to database (raw SQL, no transactions).
    {
        sqlite3* raw = db_->raw();
        if (raw) {
            QByteArray textBytes = ocrText.toUtf8();
            qint64 now = QDateTime::currentSecsSinceEpoch();

            // 1. Upsert DocumentText.
            sqlite3_stmt* upd = nullptr;
            sqlite3_prepare_v2(raw,
                "INSERT INTO DocumentText (file_id, extracted_text, text_source, char_count, updated_at) "
                "VALUES (?1, ?2, 'ocr', ?3, ?4) "
                "ON CONFLICT(file_id) DO UPDATE SET "
                "  extracted_text=excluded.extracted_text, "
                "  text_source='ocr', "
                "  char_count=excluded.char_count, "
                "  updated_at=excluded.updated_at;",
                -1, &upd, nullptr);
            if (upd) {
                sqlite3_bind_int64(upd, 1, selectedFileId_);
                sqlite3_bind_text(upd, 2, textBytes.constData(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(upd, 3, ocrText.size());
                sqlite3_bind_int64(upd, 4, now);
                sqlite3_step(upd);
                sqlite3_finalize(upd);
            }

            // 2. Update Files status.
            sqlite3_exec(raw,
                QString("UPDATE Files SET indexing_status='content_done', ocr_status='done' WHERE id=%1;")
                    .arg(selectedFileId_).toUtf8().constData(),
                nullptr, nullptr, nullptr);

            // 3. Update FTS index (DELETE + INSERT).
            sqlite3_stmt* del = nullptr;
            sqlite3_prepare_v2(raw, "DELETE FROM SearchIndex WHERE file_id=?1;",
                               -1, &del, nullptr);
            if (del) {
                sqlite3_bind_int64(del, 1, selectedFileId_);
                sqlite3_step(del);
                sqlite3_finalize(del);
            }
            QFileInfo fi(path);
            QByteArray fn = fi.fileName().toUtf8();
            QByteArray pth = path.toUtf8();
            QByteArray ext2 = ext.toUtf8();
            sqlite3_stmt* ins = nullptr;
            sqlite3_prepare_v2(raw,
                "INSERT INTO SearchIndex (filename, content, path, extension, file_id) "
                "VALUES (?1, ?2, ?3, ?4, ?5);",
                -1, &ins, nullptr);
            if (ins) {
                sqlite3_bind_text(ins, 1, fn.constData(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(ins, 2, textBytes.constData(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(ins, 3, pth.constData(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(ins, 4, ext2.constData(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(ins, 5, selectedFileId_);
                sqlite3_step(ins);
                sqlite3_finalize(ins);
            }
        }
    }

    // Show OCR text in the preview pane.
    previewPane_->setExtractedText(ocrText);
    updateIndexStats();
    statusBar()->showMessage(
        QString("OCR complete: %1 characters recognized.").arg(ocrText.size()), 5000);
}

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
            AppSettings oldSettings = settings_;
            settings_ = dlg.result();
            darkMode_ = settings_.darkMode;
            saveSettings();
            applyTheme();
            updateIndexStats();

            // If the user added new indexed drives in Settings, scan
            // them now so the user sees files appear immediately.
            // Previously, adding drives in Settings did nothing — the
            // user had to manually use Index → Add Folder to Index.
            for (const QString& drive : settings_.indexedDrives) {
                if (!oldSettings.indexedDrives.contains(drive)) {
                    // New drive added — scan it.
                    statusBar()->showMessage("Scanning " + drive + " ...");
                    QApplication::processEvents();
                    scanFolderFast(drive);
                }
            }
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
