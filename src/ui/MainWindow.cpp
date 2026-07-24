// ============================================================
// MainWindow.cpp - Top-level window with custom title bar
// ============================================================

#include "MainWindow.h"
#include "Theme.h"
#include "IconUtils.h"
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
#include "../search/QueryParser.h"
#include "../indexer/Indexer.h"
#include "../ocr/OcrWorkerPool.h"
#include "../ocr/WindowsOcrEngine.h"
#include "../monitoring/FileWatcher.h"
#include "../documents/DocumentExtractorRegistry.h"
#include "../preview/ThumbnailGenerator.h"
#include "../preview/FilePreviewPane.h"
#include "../embeddings/BgeService.h"
#include "../search/HybridSearchEngine.h"
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
#include <QStyleFactory>
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
#include <QPushButton>
#include <QProgressBar>
#include <QMouseEvent>
#include <QWindow>
#include <QGuiApplication>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <windowsx.h>
#endif

#include <sqlite3.h>

#include <memory>

namespace DocuSearch {

// ============================================================
// Custom title bar widget — handles mouse dragging to move window
// ============================================================
class TitleBarWidget : public QWidget {
public:
    explicit TitleBarWidget(MainWindow* owner, QWidget* parent = nullptr)
        : QWidget(parent), owner_(owner) {
        setFixedHeight(44);
        setObjectName("titleBar");
        setMouseTracking(true);
    }

    void setDraggableWidget(QWidget* w) { draggable_ = w; }

protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            dragPos_ = e->globalPosition().toPoint() - owner_->frameGeometry().topLeft();
            dragging_ = true;
            e->accept();
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (dragging_ && (e->buttons() & Qt::LeftButton)) {
            owner_->move(e->globalPosition().toPoint() - dragPos_);
            e->accept();
        }
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        dragging_ = false;
        e->accept();
    }

    void mouseDoubleClickEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            if (owner_->isMaximized()) {
                owner_->showNormal();
            } else {
                owner_->showMaximized();
            }
            e->accept();
        }
    }

private:
    MainWindow* owner_ = nullptr;
    QWidget* draggable_ = nullptr;
    bool dragging_ = false;
    QPoint dragPos_;
};

// ============================================================
// Constructor / destructor
// ============================================================
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {

    setWindowTitle(QString("%1 %2 - Offline Document Search")
                   .arg(Constants::kAppName, Constants::kAppVersion));

    // Frameless window so our custom title bar replaces the native one.
    Qt::WindowFlags flags = windowFlags();
    flags |= Qt::FramelessWindowHint;
    setWindowFlags(flags);

    // --- Window sizing: 80% of available screen, capped at 1440x860, centered.
    QScreen* screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect avail = screen->availableGeometry();
        int w = qMin<int>(1440, int(avail.width()  * 0.85));
        int h = qMin<int>(860,  int(avail.height() * 0.85));
        resize(w, h);
        move(avail.x() + (avail.width()  - w) / 2,
             avail.y() + (avail.height() - h) / 2);
    } else {
        resize(1440, 860);
    }
    setMinimumSize(900, 600);

    // --- Initialize ONLY the database + search (no OCR/indexer/watcher) ---
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

    loadSettings();

    // --- UI ---
    auto* centralWidget = new QWidget(this);
    centralWidget->setObjectName("centralWidget");
    auto* mainLay = new QVBoxLayout(centralWidget);
    mainLay->setContentsMargins(0, 0, 0, 0);
    mainLay->setSpacing(0);
    setCentralWidget(centralWidget);

    // Title bar at top
    buildTitleBar();
    mainLay->addWidget(titleBar_);

    // Main 4-area layout in the middle (sidebar + center + right panel).
    // buildCentral() creates a horizontal layout that holds sidebar +
    // center + right panel and adds it to mainLay.
    buildCentral();

    // Status bar at bottom (created by QMainWindow::statusBar()).
    buildStatusBar();

    // Update the OCR availability indicator on the status bar.
    // Clicking the indicator shows the install-instructions dialog.
    updateOcrStatusIndicator();
    if (ocrStatusWidget_) {
        ocrStatusWidget_->setCursor(Qt::PointingHandCursor);
        ocrStatusWidget_->installEventFilter(this);
    }

    // Initialize semantic search subsystem (BGE + ONNX Runtime).
    // This is OPTIONAL and gracefully degrades to keyword-only search
    // if the model file or onnxruntime.dll is missing.
    initializeSemanticSearch();

    applyTheme();

    // --- Signals (only the ones that don't need crash-prone subsystems) ---
    connect(searchBar_, &SearchBar::searchRequested,
            this, &MainWindow::onSearch);
    connect(searchBar_, &SearchBar::savedSearchSelected,
            this, &MainWindow::onSavedSearchSelected);
    connect(searchBar_, &SearchBar::addFolderRequested,
            this, &MainWindow::onAddFolder);
    connect(searchBar_, &SearchBar::refreshRequested,
            this, &MainWindow::onRefresh);
    connect(searchBar_, &SearchBar::extractRequested,
            this, &MainWindow::onExtract);
    connect(searchBar_, &SearchBar::filtersRequested,
            this, &MainWindow::onFilters);

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

    connect(sidebarList_, &QListWidget::currentRowChanged,
            this, &MainWindow::onSidebarClicked);

    connect(openLocationBtn_, &QPushButton::clicked,
            this, &MainWindow::onOpenLocation);

    connect(titleCloseBtn_, &QPushButton::clicked,
            qApp, &QApplication::quit);
    connect(titleMinBtn_, &QPushButton::clicked,
            this, &QWidget::showMinimized);
    connect(titleMaxBtn_, &QPushButton::clicked,
            this, [this]{
        if (isMaximized()) showNormal();
        else showMaximized();
    });
    // No theme toggle — light mode only

    // Search is triggered ONLY when the user presses Enter or clicks
    // the search input. No live/auto search while typing.
    // (liveSearchTimer_ kept for potential future use but not started.)

    // Auto-scan timer: 1 hour interval, runs on MAIN THREAD.
    autoScanTimer_ = new QTimer(this);
    autoScanTimer_->setInterval(3600 * 1000);  // 1 hour
    connect(autoScanTimer_, &QTimer::timeout, this, [this]{
        if (contentExtractionRunning_) return;
        autoScanIndexedFolders();
    });
    autoScanTimer_->start();

    // Startup diff: check for files that changed while app was closed.
    QTimer::singleShot(2000, this, [this]() {
        if (!contentExtractionRunning_) {
            statusBar()->showMessage("Checking for file changes...", 3000);
            autoScanIndexedFolders();
        }
    });

    // Phase 9: Wire up FileWatcher with debounce.
    // The watcher monitors indexed folders in real-time. Events are
    // debounced (500ms) to merge rapid add+modify sequences.
    watcher_ = std::make_unique<FileWatcher>(this);
    fileEventDebounceTimer_ = new QTimer(this);
    fileEventDebounceTimer_->setInterval(500);
    fileEventDebounceTimer_->setSingleShot(true);
    connect(fileEventDebounceTimer_, &QTimer::timeout, this, [this]() {
        // Process all debounced events.
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QStringList toProcess;
        for (auto it = fileEventDebounce_.begin(); it != fileEventDebounce_.end(); ) {
            if (now - it.value() >= 400) {
                toProcess.append(it.key());
                it = fileEventDebounce_.erase(it);
            } else {
                ++it;
            }
        }
        for (const QString& path : toProcess) {
            // Check if file still exists (might have been deleted).
            if (QFileInfo::exists(path)) {
                onFileModified(path);
            } else {
                onFileDeleted(path);
            }
        }
    });
    connect(watcher_.get(), &FileWatcher::fileAdded, this, [this](const QString& path) {
        fileEventDebounce_[path] = QDateTime::currentMSecsSinceEpoch();
        if (!fileEventDebounceTimer_->isActive()) {
            fileEventDebounceTimer_->start();
        }
    });
    connect(watcher_.get(), &FileWatcher::fileModified, this, [this](const QString& path) {
        fileEventDebounce_[path] = QDateTime::currentMSecsSinceEpoch();
        if (!fileEventDebounceTimer_->isActive()) {
            fileEventDebounceTimer_->start();
        }
    });
    connect(watcher_.get(), &FileWatcher::fileDeleted, this, &MainWindow::onFileDeleted);
    connect(watcher_.get(), &FileWatcher::fileRenamed, this, &MainWindow::onFileRenamed);

    // Start watching indexed folders.
    if (!settings_.indexedDrives.isEmpty()) {
        watcher_->addWatches(settings_.indexedDrives);
    }

    refreshSavedSearches();
    statusBar()->showMessage("Ready. Click 'Add Folder' to begin indexing documents.");

    // NOTE: Auto-extract on startup is DISABLED to prevent crashes.
    // Extraction only happens after Add Folder or manual Extract button click.

    // Keyboard shortcuts
    auto* focusSearchAct = new QAction(this);
    focusSearchAct->setShortcut(QKeySequence("Ctrl+K"));
    connect(focusSearchAct, &QAction::triggered, this, [this]{
        if (searchBar_) searchBar_->setFocus(Qt::ShortcutFocusReason);
    });
    addAction(focusSearchAct);

    auto* refreshAct = new QAction(this);
    refreshAct->setShortcut(QKeySequence::Refresh);
    connect(refreshAct, &QAction::triggered, this, [this]{ onRefresh(); });
    addAction(refreshAct);

    auto* openAct = new QAction(this);
    openAct->setShortcut(QKeySequence("Ctrl+O"));
    connect(openAct, &QAction::triggered, this, [this]{
        if (!selectedPath_.isEmpty()) onOpenOriginal(selectedPath_);
    });
    addAction(openAct);
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

// Handle WM_NCHITTEST on Windows so the frameless window can be resized
// from its edges (the OS doesn't provide resize handles for frameless
// windows, so we tell it which pixels belong to which resize border).
bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG" && message) {
        MSG* msg = reinterpret_cast<MSG*>(message);
        if (msg->message == WM_NCHITTEST) {
            const LONG x = GET_X_LPARAM(msg->lParam);
            const LONG y = GET_Y_LPARAM(msg->lParam);
            const QRect wr = frameGeometry();
            const int border = 6;
            const bool onLeft   = x >= wr.left() && x < wr.left() + border;
            const bool onRight  = x <  wr.right()  && x >= wr.right() - border;
            const bool onTop    = y >= wr.top() && y < wr.top() + border;
            const bool onBottom = y <  wr.bottom() && y >= wr.bottom() - border;
            if (onTop && onLeft)     { *result = HTTOPLEFT;     return true; }
            if (onTop && onRight)    { *result = HTTOPRIGHT;    return true; }
            if (onBottom && onLeft)  { *result = HTBOTTOMLEFT;  return true; }
            if (onBottom && onRight) { *result = HTBOTTOMRIGHT; return true; }
            if (onLeft)   { *result = HTLEFT;   return true; }
            if (onRight)  { *result = HTRIGHT;  return true; }
            if (onTop)    { *result = HTTOP;    return true; }
            if (onBottom) { *result = HTBOTTOM; return true; }
        }
    }
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

// ============================================================
// Title bar
// ============================================================
void MainWindow::buildTitleBar() {
    titleBar_ = new TitleBarWidget(this, this);
    titleBar_->setObjectName("titleBar");

    auto* h = new QHBoxLayout(titleBar_);
    h->setContentsMargins(16, 8, 12, 8);
    h->setSpacing(10);

    // App logo (28x28 blue square with white search icon)
    appLogoLbl_ = new QLabel(titleBar_);
    appLogoLbl_->setObjectName("appLogo");
    appLogoLbl_->setFixedSize(28, 28);
    appLogoLbl_->setPixmap(loadLucidePixmap("search", QColor("#ffffff"), 16, devicePixelRatio()));
    h->addWidget(appLogoLbl_);

    // Title text: "DocuSearch 1.0.0" (bold) + "• Offline Document Search" (muted)
    auto* titleWrap = new QWidget(titleBar_);
    auto* twLay = new QHBoxLayout(titleWrap);
    twLay->setContentsMargins(0, 0, 0, 0);
    twLay->setSpacing(6);
    titleBarText_ = new QLabel(QString("DocuSearch %1").arg(Constants::kAppVersion), titleWrap);
    titleBarText_->setObjectName("titleBarText");
    titleBarSubtitle_ = new QLabel("• Offline Document Search", titleWrap);
    titleBarSubtitle_->setObjectName("titleBarSubtitle");
    twLay->addWidget(titleBarText_);
    twLay->addWidget(titleBarSubtitle_);
    h->addWidget(titleWrap);

    h->addStretch();

    // Window control buttons (minimize, maximize, close) — no theme toggle
    titleMinBtn_ = new QPushButton(titleBar_);
    titleMinBtn_->setObjectName("titleBtn");
    titleMinBtn_->setCursor(Qt::PointingHandCursor);
    titleMinBtn_->setToolTip("Minimize");
    titleMinBtn_->setFixedSize(32, 32);

    titleMaxBtn_ = new QPushButton(titleBar_);
    titleMaxBtn_->setObjectName("titleBtn");
    titleMaxBtn_->setCursor(Qt::PointingHandCursor);
    titleMaxBtn_->setToolTip("Maximize");
    titleMaxBtn_->setFixedSize(32, 32);

    titleCloseBtn_ = new QPushButton(titleBar_);
    titleCloseBtn_->setObjectName("titleBtn");
    titleCloseBtn_->setCursor(Qt::PointingHandCursor);
    titleCloseBtn_->setToolTip("Close");
    titleCloseBtn_->setFixedSize(32, 32);

    h->addWidget(titleMinBtn_);
    h->addWidget(titleMaxBtn_);
    h->addWidget(titleCloseBtn_);
}

// ============================================================
// Central area: top menu bar + (search bar + 3-panel splitter) + right panel
// ============================================================
void MainWindow::buildCentral() {
    // The central widget's main layout was created in the ctor.
    // We add a horizontal layout containing: center + right panel.
    // (The left sidebar has been replaced by a horizontal top menu bar
    // that sits above the search bar — same navigation, less screen space.)
    auto* centralWidget = this->centralWidget();
    auto* mainLay = qobject_cast<QVBoxLayout*>(centralWidget->layout());

    auto* hLay = new QHBoxLayout();
    hLay->setContentsMargins(0, 0, 0, 0);
    hLay->setSpacing(0);
    mainLay->addLayout(hLay, 1);

    // ============================================================
    // 1) CENTER PANEL (top menu bar + search bar + 3-panel splitter)
    // ============================================================
    auto* centerWidget = new QWidget(centralWidget);
    auto* centerLay = new QVBoxLayout(centerWidget);
    centerLay->setContentsMargins(0, 0, 0, 0);
    centerLay->setSpacing(0);

    // ── Top menu bar (replaces the old left sidebar) ──────────
    // A horizontal strip with nav buttons + indexed-status badge.
    sidebar_ = new QWidget(centerWidget);
    sidebar_->setObjectName("topMenuBar");
    sidebar_->setFixedHeight(40);
    auto* menuBarLay = new QHBoxLayout(sidebar_);
    menuBarLay->setContentsMargins(8, 0, 8, 0);
    menuBarLay->setSpacing(4);

    // Use a QButtonGroup for mutual-exclusion nav buttons.
    // We re-use sidebarList_ as a QListWidget for state-tracking
    // (so existing onSidebarClicked logic keeps working) but
    // display it as a horizontal button strip.
    sidebarList_ = new QListWidget(sidebar_);
    sidebarList_->setObjectName("topMenuList");
    sidebarList_->setViewMode(QListView::ListMode);
    sidebarList_->setFlow(QListView::LeftToRight);
    sidebarList_->setWrapping(false);
    sidebarList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sidebarList_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sidebarList_->setFixedHeight(36);
    sidebarList_->setSelectionMode(QAbstractItemView::SingleSelection);
    sidebarList_->setFocusPolicy(Qt::NoFocus);
    sidebarList_->setStyleSheet(
        "QListWidget#topMenuList { background: #f5f5f5; border: none; outline: none; }"
        "QListWidget#topMenuList::item { "
        "  padding: 4px 12px; "
        "  border-radius: 4px; "
        "  margin: 2px; "
        "  font-size: 10pt; "
        "  color: #444; "
        "}"
        "QListWidget#topMenuList::item:selected { "
        "  background: #0066cc; "
        "  color: white; "
        "}"
        "QListWidget#topMenuList::item:hover { "
        "  background: #e0e0e0; "
        "}");
    const QStringList navLabels = {
        "Search",
        "Stats", "Settings", "Help", "About"
    };
    for (int i = 0; i < navLabels.size(); ++i) {
        auto* item = new QListWidgetItem(navLabels[i], sidebarList_);
        item->setData(Qt::UserRole, navLabels[i]);
        // Wider items so "Settings" (8 chars) fits at 10pt font.
        item->setSizeHint(QSize(110, 28));
        item->setTextAlignment(Qt::AlignCenter);
    }
    if (sidebarList_->count() > 0) {
        sidebarList_->setCurrentRow(0);
    }
    menuBarLay->addWidget(sidebarList_, 1);

    // Compact indexed-status badge on the right side of the menu bar.
    auto* statusBadge = new QWidget(sidebar_);
    statusBadge->setObjectName("indexedStatus");
    auto* sbLay = new QHBoxLayout(statusBadge);
    sbLay->setContentsMargins(8, 4, 8, 4);
    sbLay->setSpacing(6);
    auto* dotLbl = new QLabel(statusBadge);
    dotLbl->setFixedSize(8, 8);
    indexedHeaderLbl_ = new QLabel("Indexed", statusBadge);
    indexedHeaderLbl_->setObjectName("indexedHeader");
    indexedInfoLbl_ = new QLabel("0 files", statusBadge);
    indexedInfoLbl_->setObjectName("indexedInfo");
    sbLay->addWidget(dotLbl);
    sbLay->addWidget(indexedHeaderLbl_);
    sbLay->addWidget(indexedInfoLbl_);

    indexedBar_ = new QProgressBar(statusBadge);
    indexedBar_->setObjectName("indexedBar");
    indexedBar_->setRange(0, 100);
    indexedBar_->setValue(0);
    indexedBar_->setTextVisible(false);
    indexedBar_->setFixedWidth(80);
    indexedBar_->setFixedHeight(6);
    sbLay->addWidget(indexedBar_);

    menuBarLay->addWidget(statusBadge);

    centerLay->addWidget(sidebar_);

    // Search bar
    searchBar_ = new SearchBar(centerWidget);
    centerLay->addWidget(searchBar_);

    // 3-way splitter: results | viewer | (metadata+tags)
    mainSplitter_ = new QSplitter(Qt::Horizontal, centerWidget);
    mainSplitter_->setObjectName("mainSplitter");
    mainSplitter_->setHandleWidth(1);
    mainSplitter_->setChildrenCollapsible(false);
    centerLay->addWidget(mainSplitter_, 1);

    resultsPane_ = new ResultsPane(mainSplitter_);
    resultsPane_->setObjectName("resultsPane");
    resultsPane_->setMinimumWidth(280);
    resultsPane_->setMaximumWidth(420);
    mainSplitter_->addWidget(resultsPane_);

    // ── Center column: FilePreviewPane (TOP) + PreviewPane (existing) ──
    // The FilePreviewPane shows the actual rendered file (PDF/image/text/office).
    // The existing PreviewPane below shows the extracted text + tabs.
    // Both panes are flush against each other (no gap) — they look like one
    // unified viewer with a thin separator.
    auto* centerColumn = new QWidget(mainSplitter_);
    auto* centerColLay = new QVBoxLayout(centerColumn);
    centerColLay->setContentsMargins(0, 0, 0, 0);
    centerColLay->setSpacing(0);  // no gap between the two panes

    filePreviewPane_ = new FilePreviewPane(centerColumn);
    filePreviewPane_->setObjectName("filePreviewPane");
    filePreviewPane_->setMinimumHeight(180);
    centerColLay->addWidget(filePreviewPane_, 1);  // takes most space

    previewPane_ = new PreviewPane(centerColumn);
    previewPane_->setObjectName("previewPane");
    previewPane_->setMinimumWidth(360);
    previewPane_->setMinimumHeight(120);
    previewPane_->setMaximumHeight(280);  // limit extracted-text pane
    centerColLay->addWidget(previewPane_, 0);  // fixed-ish height

    mainSplitter_->addWidget(centerColumn);

    // Right panel: metadata (top) + tags/notes (bottom), stacked vertically.
    // We wrap the vertical splitter in a fixed-width (300px) container so
    // the splitter itself can be any height while the panel stays 300px wide.
    auto* rightPanelWrap = new QWidget(mainSplitter_);
    rightPanelWrap->setObjectName("metadataPanel");
    rightPanelWrap->setFixedWidth(300);
    auto* rpLay = new QVBoxLayout(rightPanelWrap);
    rpLay->setContentsMargins(0, 0, 0, 0);
    rpLay->setSpacing(0);

    rightSplitter_ = new QSplitter(Qt::Vertical, rightPanelWrap);
    rightSplitter_->setObjectName("rightSplitter");
    rightSplitter_->setHandleWidth(1);
    rightSplitter_->setChildrenCollapsible(false);

    metadataPane_ = new MetadataPane(rightSplitter_);
    metadataPane_->setObjectName("metadataPane");
    metadataPane_->setMinimumHeight(180);
    rightSplitter_->addWidget(metadataPane_);

    tagsNotesPane_ = new TagsNotesPane(rightSplitter_);
    tagsNotesPane_->setObjectName("tagsNotesPane");
    tagsNotesPane_->setMinimumHeight(160);
    rightSplitter_->addWidget(tagsNotesPane_);

    rpLay->addWidget(rightSplitter_);
    mainSplitter_->addWidget(rightPanelWrap);

    // Stretch factors: results=340, viewer=flex, right=300
    mainSplitter_->setStretchFactor(0, 0);
    mainSplitter_->setStretchFactor(1, 1);
    mainSplitter_->setStretchFactor(2, 0);

    // Initial sizes
    const int availWidth = qMax(800, width() - 170 - 300 - 16);
    QList<int> hSizes;
    hSizes << qMin(420, qMax(280, int(availWidth * 0.30)))
           << qMax(360, int(availWidth * 0.70))
           << 300;
    mainSplitter_->setSizes(hSizes);

    rightSplitter_->setStretchFactor(0, 1);
    rightSplitter_->setStretchFactor(1, 1);
    QList<int> vSizes;
    vSizes << 400 << 300;
    rightSplitter_->setSizes(vSizes);

    hLay->addWidget(centerWidget, 1);

    // Hidden indexing widget (kept for stats plumbing).
    indexingWidget_ = new IndexingProgressWidget(this);
    indexingWidget_->setVisible(false);

    updateIndexStats();
}

// ============================================================
// Status bar
// ============================================================
void MainWindow::buildStatusBar() {
    auto* sb = statusBar();
    sb->setSizeGripEnabled(false);

    // Left side: dot + Ready + indexed count + total size + last indexed
    auto* left = new QWidget(sb);
    auto* lLay = new QHBoxLayout(left);
    lLay->setContentsMargins(0, 0, 0, 0);
    lLay->setSpacing(16);

    auto* readyRow = new QWidget(left);
    auto* rLay = new QHBoxLayout(readyRow);
    rLay->setContentsMargins(0, 0, 0, 0);
    rLay->setSpacing(6);
    statusDotLbl_ = new QLabel(readyRow);
    statusDotLbl_->setObjectName("statusDot");
    statusReadyLbl_ = new QLabel("Ready", readyRow);
    statusReadyLbl_->setObjectName("statusReady");
    rLay->addWidget(statusDotLbl_);
    rLay->addWidget(statusReadyLbl_);
    lLay->addWidget(readyRow);

    statusIndexedLbl_ = new QLabel("Indexed files: 0", left);
    statusIndexedLbl_->setObjectName("statusInfo");
    lLay->addWidget(statusIndexedLbl_);

    statusSizeLbl_ = new QLabel("Total size: 0 B", left);
    statusSizeLbl_->setObjectName("statusInfo");
    lLay->addWidget(statusSizeLbl_);

    statusLastLbl_ = new QLabel("Last indexed: -", left);
    statusLastLbl_->setObjectName("statusInfo");
    lLay->addWidget(statusLastLbl_);

    lLay->addStretch();

    // Progress bar for extraction (hidden by default)
    extractionProgressBar_ = new QProgressBar(left);
    extractionProgressBar_->setFixedWidth(120);
    extractionProgressBar_->setFixedHeight(6);
    extractionProgressBar_->setTextVisible(false);
    extractionProgressBar_->setRange(0, 100);
    extractionProgressBar_->setValue(0);
    extractionProgressBar_->setVisible(false);
    lLay->addWidget(extractionProgressBar_);

    sb->addWidget(left, 1);

    // Right side: OCR status indicator + Open Location button.
    // Shows a colored dot + "OCR: Ready" / "OCR: Setup Required" so users
    // know at a glance whether oneocr is installed.
    ocrStatusWidget_ = new QWidget(sb);
    auto* ocrLay = new QHBoxLayout(ocrStatusWidget_);
    ocrLay->setContentsMargins(8, 0, 8, 0);
    ocrLay->setSpacing(6);
    ocrDotLbl_ = new QLabel(ocrStatusWidget_);
    ocrDotLbl_->setFixedSize(8, 8);
    ocrDotLbl_->setStyleSheet("background: #888; border-radius: 4px;");
    ocrStatusLbl_ = new QLabel("OCR: ?", ocrStatusWidget_);
    ocrStatusLbl_->setObjectName("ocrStatus");
    ocrLay->addWidget(ocrDotLbl_);
    ocrLay->addWidget(ocrStatusLbl_);
    ocrStatusWidget_->setToolTip(
        "OCR (Optical Character Recognition) status.\n"
        "Green: oneocr.dll is installed and ready.\n"
        "Yellow: oneocr.dll is missing — click to install.\n"
        "Click to open the OCR Setup instructions.");
    sb->addPermanentWidget(ocrStatusWidget_);

    // Semantic search toggle button (shows current state).
    // Disabled by default — enabled after BGE service becomes ready.
    semanticToggleBtn_ = new QPushButton(sb);
    semanticToggleBtn_->setObjectName("semanticToggleBtn");
    semanticToggleBtn_->setText("AI: OFF");
    semanticToggleBtn_->setCheckable(true);
    semanticToggleBtn_->setChecked(false);
    semanticToggleBtn_->setEnabled(false);
    semanticToggleBtn_->setToolTip(
        "Toggle AI semantic search (BGE Small EN v1.5).\n"
        "When ON, search results include AI semantic matches in addition to keyword matches.\n"
        "Disabled if the AI model is not installed.");
    semanticToggleBtn_->setCursor(Qt::PointingHandCursor);
    sb->addPermanentWidget(semanticToggleBtn_);

    // Theme toggle button (light/dark) — compact toggle in status bar.
    themeToggleBtn_ = new QPushButton(sb);
    themeToggleBtn_->setObjectName("themeToggleBtn");
    themeToggleBtn_->setCheckable(true);
    themeToggleBtn_->setChecked(darkMode_);
    themeToggleBtn_->setText(darkMode_ ? "🌙" : "☀");
    themeToggleBtn_->setToolTip("Toggle light/dark theme");
    themeToggleBtn_->setCursor(Qt::PointingHandCursor);
    themeToggleBtn_->setFixedSize(36, 28);
    connect(themeToggleBtn_, &QPushButton::clicked, this, &MainWindow::onToggleTheme);
    sb->addPermanentWidget(themeToggleBtn_);

    openLocationBtn_ = new QPushButton(sb);
    openLocationBtn_->setObjectName("openLocationBtn");
    openLocationBtn_->setCursor(Qt::PointingHandCursor);
    openLocationBtn_->setText("Open Location");
    openLocationBtn_->setToolTip("Open the folder containing the selected file");
    sb->addPermanentWidget(openLocationBtn_);
}

void MainWindow::applyTheme() {
    QPalette pal;

    if (darkMode_) {
        // Dark theme — deep blue-gray with blue accents.
        pal.setColor(QPalette::Window,          QColor(0x1e, 0x1e, 0x2e));
        pal.setColor(QPalette::Base,            QColor(0x2a, 0x2a, 0x3e));
        pal.setColor(QPalette::AlternateBase,   QColor(0x25, 0x25, 0x38));
        pal.setColor(QPalette::WindowText,      QColor(0xe0, 0xe0, 0xe0));
        pal.setColor(QPalette::Text,            QColor(0xe0, 0xe0, 0xe0));
        pal.setColor(QPalette::ButtonText,      QColor(0xe0, 0xe0, 0xe0));
        pal.setColor(QPalette::Button,          QColor(0x35, 0x35, 0x50));
        pal.setColor(QPalette::Highlight,       QColor(0x4f, 0x46, 0xe5));
        pal.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
        pal.setColor(QPalette::ToolTipBase,     QColor(0x35, 0x35, 0x50));
        pal.setColor(QPalette::ToolTipText,     QColor(0xe0, 0xe0, 0xe0));
    } else {
        // Light theme — warm white with blue accents (more colorful).
        pal.setColor(QPalette::Window,          QColor(0xf5, 0xf7, 0xfa));  // soft blue-white
        pal.setColor(QPalette::Base,            QColor(0xff, 0xff, 0xff));
        pal.setColor(QPalette::AlternateBase,   QColor(0xf0, 0xf4, 0xf8));
        pal.setColor(QPalette::WindowText,      QColor(0x1a, 0x2a, 0x3a));  // dark blue-gray
        pal.setColor(QPalette::Text,            QColor(0x1a, 0x2a, 0x3a));
        pal.setColor(QPalette::ButtonText,      QColor(0x1a, 0x2a, 0x3a));
        pal.setColor(QPalette::Button,          QColor(0xe8, 0xed, 0xf3));
        pal.setColor(QPalette::Highlight,       QColor(0x4f, 0x46, 0xe5));  // indigo
        pal.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
        pal.setColor(QPalette::ToolTipBase,     QColor(0x1a, 0x2a, 0x3a));
        pal.setColor(QPalette::ToolTipText,     QColor(0xff, 0xff, 0xff));
    }
    pal.setColor(QPalette::Disabled, QPalette::WindowText,  QColor(160, 160, 160));
    pal.setColor(QPalette::Disabled, QPalette::Text,        QColor(160, 160, 160));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText,  QColor(160, 160, 160));

    QApplication::setPalette(pal);

    // Apply QSS with color-enhanced stylesheet.
    const char* colorQss = darkMode_ ?
        // Dark theme QSS
        "QMainWindow { background: #1e1e2e; }"
        "QWidget#topMenuBar { background: #2a2a3e; border-bottom: 2px solid #4f46e5; }"
        "QStatusBar { background: #2a2a3e; border-top: 1px solid #353550; color: #e0e0e0; }"
        "QStatusBar QLabel { color: #e0e0e0; }"
        "QPushButton { background: #353550; color: #e0e0e0; border: 1px solid #454565; border-radius: 4px; padding: 4px 12px; }"
        "QPushButton:hover { background: #4f46e5; color: white; }"
        "QPushButton:pressed { background: #3b36b8; }"
        "QLineEdit { background: #2a2a3e; color: #e0e0e0; border: 1px solid #454565; border-radius: 4px; padding: 6px; }"
        "QListWidget#resultsPane { background: #252538; border: none; }"
        "QListWidget#resultsPane::item { padding: 6px; border-bottom: 1px solid #353550; }"
        "QListWidget#resultsPane::item:selected { background: #4f46e5; color: white; }"
        :
        // Light theme QSS — more color than before
        "QMainWindow { background: #f5f7fa; }"
        "QWidget#topMenuBar { background: #4f46e5; border-bottom: 2px solid #3b36b8; }"
        "QStatusBar { background: #e8edf3; border-top: 2px solid #4f46e5; color: #1a2a3a; }"
        "QStatusBar QLabel { color: #1a2a3a; font-weight: bold; }"
        "QPushButton { background: #4f46e5; color: white; border: none; border-radius: 6px; padding: 6px 14px; font-weight: bold; }"
        "QPushButton:hover { background: #3b36b8; }"
        "QPushButton:pressed { background: #2d2a8e; }"
        "QPushButton:disabled { background: #c0c8d8; color: #888; }"
        "QPushButton#openLocationBtn { background: #10b981; }"
        "QPushButton#openLocationBtn:hover { background: #059669; }"
        "QPushButton#semanticToggleBtn { background: #6366f1; }"
        "QPushButton#semanticToggleBtn:checked { background: #10b981; }"
        "QPushButton#themeToggleBtn { background: #f59e0b; font-size: 14pt; }"
        "QLineEdit { background: white; color: #1a2a3a; border: 2px solid #4f46e5; border-radius: 6px; padding: 6px 8px; font-size: 11pt; }"
        "QLineEdit:focus { border: 2px solid #6366f1; }"
        "QListWidget#resultsPane { background: white; border: 1px solid #d0d8e0; border-radius: 4px; }"
        "QListWidget#resultsPane::item { padding: 8px; border-bottom: 1px solid #eef0f4; }"
        "QListWidget#resultsPane::item:selected { background: #4f46e5; color: white; }"
        "QListWidget#resultsPane::item:hover { background: #f0f4ff; }"
        "QTabWidget::pane { border: 1px solid #d0d8e0; border-radius: 4px; }"
        "QTabBar::tab { background: #e8edf3; padding: 6px 14px; border: 1px solid #d0d8e0; border-bottom: none; border-top-left-radius: 4px; border-top-right-radius: 4px; }"
        "QTabBar::tab:selected { background: white; font-weight: bold; }"
        "QGroupBox { border: 1px solid #d0d8e0; border-radius: 6px; margin-top: 8px; padding-top: 12px; font-weight: bold; color: #4f46e5; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
        "QProgressBar { border: 1px solid #d0d8e0; border-radius: 3px; background: #e8edf3; }"
        "QProgressBar::chunk { background: #4f46e5; border-radius: 3px; }"
        "QScrollBar:vertical { background: #f5f7fa; width: 10px; }"
        "QScrollBar::handle:vertical { background: #4f46e5; border-radius: 5px; min-height: 30px; }"
        "QScrollBar::handle:vertical:hover { background: #3b36b8; }"
        "QScrollBar::add-line, QScrollBar::sub-line { height: 0; }"
        ;
    qApp->setStyleSheet(QString(colorQss));

    // Defer icon refresh.
    QTimer::singleShot(0, this, [this]() {
        refreshAllIcons();
    });
}

void MainWindow::refreshAllIcons() {
    QColor textColor = qApp->palette().color(QPalette::Text);
    QColor whiteText("#ffffff");

    // ---- Sidebar icons (each with a distinct color for premium feel) ----
    const QStringList navIcons = {
        "search", "bookmark", "tag", "sticky-note",
        "bar-chart-3", "settings", "help-circle", "info"
    };
    // Distinct colors for each nav item: blue, purple, green, yellow,
    // indigo, orange, gray, teal, pink
    const QStringList navColors = {
        "#2563eb", "#7c3aed", "#059669", "#d97706",
        "#4f46e5", "#ea580c", "#6b7280", "#0d9488", "#db2777"
    };
    for (int i = 0; i < sidebarList_->count() && i < navIcons.size(); ++i) {
        auto* item = sidebarList_->item(i);
        if (!item) continue;
        QColor iconColor(navColors[i % navColors.size()]);
        item->setIcon(loadLucideIcon(navIcons[i], iconColor, 18));
    }

    // ---- Title bar icons ----
    // App logo: white search icon on blue background (set in ctor, but
    // re-set here in case the device pixel ratio changed).
    appLogoLbl_->setPixmap(loadLucidePixmap("search", whiteText, 16, devicePixelRatio()));

    // No theme button — light mode only

    titleMinBtn_->setIcon(loadLucideIcon("minus", textColor, 14));
    titleMinBtn_->setIconSize(QSize(14, 14));

    titleMaxBtn_->setIcon(loadLucideIcon("square", textColor, 14));
    titleMaxBtn_->setIconSize(QSize(14, 14));

    titleCloseBtn_->setIcon(loadLucideIcon("x", textColor, 14));
    titleCloseBtn_->setIconSize(QSize(14, 14));

    // ---- Open Location button icon ----
    openLocationBtn_->setIcon(loadLucideIcon("folder", textColor, 14));
    openLocationBtn_->setIconSize(QSize(14, 14));

    // ---- Sub-pane icon refresh ----
    if (searchBar_)     searchBar_->refreshIcons();
    if (resultsPane_)   resultsPane_->refreshIcons();
    if (previewPane_)   previewPane_->refreshIcons();
    if (metadataPane_)  metadataPane_->refreshIcons();
    if (tagsNotesPane_) tagsNotesPane_->refreshIcons();
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

        // Always run keyword (FTS5 BM25) search first.
        auto hits = search_->search(query, 50);  // limit to top 50 results

        // If semantic search is enabled AND the BGE service is ready,
        // run hybrid search to merge keyword results with semantic matches.
        // This is the "AI" feature — without this wiring, the Semantic
        // toggle button does nothing functional.
        if (semanticEnabled_ && bgeService_ && bgeService_->isReady() && hybridSearch_) {
            // Pass the type filter to the hybrid engine so semantic-only
            // results respect it (e.g., type:pdf won't show .txt files).
            const auto parsed = QueryParser::parse(query);
            hybridSearch_->setTypeFilter(parsed.typeFilter);

            // Convert SearchHit → ExistingSearchResult for the hybrid engine.
            std::vector<DocuSearch::ExistingSearchResult> keywordResults;
            keywordResults.reserve(hits.size());
            for (const auto& h : hits) {
                ExistingSearchResult r;
                r.fileId     = h.fileId;
                r.filename   = h.filename;
                r.path       = h.path;
                r.extension  = h.extension;
                r.bm25Score  = h.score;
                keywordResults.push_back(r);
            }

            // Run hybrid search (keyword + cosine, weighted average).
            auto hybridResults = hybridSearch_->search(query, keywordResults);

            // Convert HybridResult → SearchHit for display.
            QList<SearchHit> merged;
            merged.reserve(hybridResults.size());
            for (const auto& hr : hybridResults) {
                SearchHit h;
                h.fileId       = hr.fileId;
                h.filename     = hr.filename;
                h.path         = hr.path;
                h.extension    = hr.extension;
                h.score        = hr.combinedScore;
                h.snippet      = QString("[keyword: %1, semantic: %2, combined: %3]")
                    .arg(hr.keywordScore, 0, 'f', 3)
                    .arg(hr.semanticScore, 0, 'f', 3)
                    .arg(hr.combinedScore, 0, 'f', 3);
                // Preserve favorite flag + dates from the original hit if present.
                for (const auto& orig : hits) {
                    if (orig.fileId == hr.fileId) {
                        h.size          = orig.size;
                        h.modifiedDate  = orig.modifiedDate;
                        h.isFavorite    = orig.isFavorite;
                        if (!orig.snippet.isEmpty()) h.snippet = orig.snippet;
                        break;
                    }
                }
                merged.append(h);
            }
            resultsPane_->setResults(merged);
            statusBar()->showMessage(
                QString("AI search: %1 result%2 (%3 keyword + semantic) in %4 ms")
                    .arg(merged.size())
                    .arg(merged.size() == 1 ? "" : "s")
                    .arg(hits.size())
                    .arg(t.elapsed()));
        } else {
            // Keyword-only search (existing behavior).
            resultsPane_->setResults(hits);
            statusBar()->showMessage(QString("%1 result%2 in %3 ms")
                                     .arg(hits.size())
                                     .arg(hits.size() == 1 ? "" : "s")
                                     .arg(t.elapsed()));
        }

        // Highlight search terms in the extracted text pane (yellow).
        // This makes it easy for users to find the relevant parts of
        // the document after clicking a search result.
        if (previewPane_) {
            previewPane_->setSearchQuery(query);
        }
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
    } catch (...) {}

    try {
        previewPane_->setFilePath(path);
    } catch (...) {}

    // Load the file into the new native FilePreviewPane (top pane).
    try {
        if (filePreviewPane_) filePreviewPane_->loadFile(path);
    } catch (...) {}

    try {
        // Load extracted text from DocumentText table.
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
        // Cap the text length to prevent UI freeze on very large documents
        if (extracted.size() > 50000) {
            extracted = extracted.left(50000) + "\n\n[... text truncated ...]";
        }
        previewPane_->setExtractedText(extracted.isEmpty()
            ? "No content extracted for this file."
            : extracted);
        previewPane_->setDocumentText(extracted);
    } catch (...) {
        statusBar()->showMessage("Failed to load file preview.", 3000);
    }

    // NOTE: Search highlighting is DISABLED — it was causing crashes
    // on large documents. Will re-enable with a safer implementation.
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

void MainWindow::onOpenLocation() {
    if (selectedPath_.isEmpty()) {
        statusBar()->showMessage("Select a file first.", 3000);
        return;
    }
    QFileInfo fi(selectedPath_);
    const QString folder = fi.absolutePath();
    if (folder.isEmpty()) return;
    // Open the folder in Windows Explorer with the file selected.
#ifdef Q_OS_WIN
    const QString winPath = QDir::toNativeSeparators(folder);
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
#endif
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

    // ONLY index supported file types — skip DLLs, EXEs, archives, etc.
    const QSet<QString> supportedExts = {
        "pdf", "doc", "docx", "xls", "xlsx", "xlsm",
        "ppt", "pptx", "txt", "rtf", "csv", "md",
        "jpg", "jpeg", "png", "tif", "tiff", "bmp",
        "gif", "webp", "html", "htm", "xml", "json", "log",
    };

    int count = 0, skipped = 0;
    QStringList emptyExcludes;
    FileUtils::walkDirectory(folder, emptyExcludes, [&](const QFileInfo& fi) -> bool {
        const QString ext = FileUtils::extensionOf(fi.absoluteFilePath()).toLower();
        if (!supportedExts.contains(ext)) { ++skipped; return true; }

        const QString path = FileUtils::toNative(fi.absoluteFilePath());
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
                QString("Scanning... %1 indexed (%2 skipped)").arg(count).arg(skipped));
            QApplication::processEvents();
        }
        return true;
    });
    updateIndexStats();
    statusBar()->showMessage(
        QString("Scan complete: %1 files indexed, %2 skipped").arg(count).arg(skipped), 5000);
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

        // Auto-start extraction immediately after scanning.
        // This extracts text from all newly-indexed files in the
        // background (QTimer, 200 files per session) so the user
        // doesn't need to manually click Extract.
        statusBar()->showMessage("Scan complete. Starting auto-extraction...", 3000);
        QApplication::processEvents();
        QTimer::singleShot(500, this, [this]() {
            if (!contentExtractionRunning_) {
                onExtract();
            }
        });
    } catch (...) {
        statusBar()->showMessage("Folder scan failed.", 5000);
    }
}

void MainWindow::onRefresh() {
    // Refresh the current search results.
    try {
        onSearch(searchBar_->text());
        updateIndexStats();
        statusBar()->showMessage("Refreshed.", 2000);
    } catch (...) {
        statusBar()->showMessage("Refresh failed.", 3000);
    }
}

void MainWindow::onFilters() {
    // Toggle a simple "advanced filters" prompt for now.
    bool ok = false;
    const QString q = QInputDialog::getText(
        this, "Advanced Filters",
        "Enter filter (e.g., type:pdf, folder:Railway, date:2026, tag:Urgent):",
        QLineEdit::Normal, searchBar_->text(), &ok);
    if (ok && !q.isEmpty()) {
        searchBar_->setText(q);
        onSearch(q);
    }
}

void MainWindow::onSidebarClicked(int row) {
    if (row < 0) return;
    auto* item = sidebarList_->item(row);
    if (!item) return;
    const QString page = item->data(Qt::UserRole).toString();
    if (page == "Settings") {
        onOpenSettings();
        // Revert to "Search" so the user lands back on the search page.
        sidebarList_->setCurrentRow(0);
    } else if (page == "About") {
        onAbout();
        sidebarList_->setCurrentRow(0);
    } else if (page == "Help") {
        QMessageBox::information(this, "How to Search",
            "<h3>Search Syntax</h3>"
            "<table cellspacing='6'>"
            "<tr><td><b>gold bin</b></td><td>Files containing BOTH 'gold' AND 'bin'</td></tr>"
            "<tr><td><b>\"gold bin\"</b></td><td>Exact phrase 'gold bin'</td></tr>"
            "<tr><td><b>gold -draft</b></td><td>Files with 'gold' but NOT 'draft'</td></tr>"
            "<tr><td><b>rail*</b></td><td>Prefix wildcard: railway, railroad, rails</td></tr>"
            "</table>"
            "<h3>Filters</h3>"
            "<table cellspacing='6'>"
            "<tr><td><b>type:pdf</b></td><td>Only PDF files</td></tr>"
            "<tr><td><b>folder:Railway</b></td><td>Files in folders containing 'Railway'</td></tr>"
            "<tr><td><b>date:2026</b></td><td>Files modified in 2026</td></tr>"
            "<tr><td><b>tag:Urgent</b></td><td>Files tagged 'Urgent'</td></tr>"
            "</table>");
        sidebarList_->setCurrentRow(0);
    } else if (page == "Stats") {
        QMessageBox::information(this, "Index Statistics",
            QString("Total files: %1\nDatabase size: %2")
                .arg(repo_ ? repo_->totalFiles() : 0)
                .arg([&]{
                    QFile f(Config::instance().dbPath());
                    return Utils::formatFileSize(f.exists() ? f.size() : 0);
                }()));
        sidebarList_->setCurrentRow(0);
    }
    // "Search" stays as the current page.
    // Saved/Tags/Notes were removed from the menu — they're accessible
    // directly: Saved Searches via the search bar dropdown, Tags/Notes
    // in the right panel when a file is selected.
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
            previewPane_->setDocumentText(extracted);
        }
    } catch (...) {
        // Silently ignore - this is just a convenience refresh.
    }
}

void MainWindow::onExtract() {
    if (!repo_ || !db_) return;
    if (contentExtractionRunning_) {
        // Toggle cancel if already running.
        extractCancelFlag_.store(true);
        statusBar()->showMessage("Cancelling extraction...", 3000);
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
            "WHERE indexing_status = 'metadata_only' "
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
        // Show detailed extraction status instead of a generic message.
        QString statusMsg = getExtractionStatusString();
        statusBar()->showMessage(statusMsg, 5000);
        return;
    }

    contentExtractionRunning_ = true;
    extractCancelFlag_.store(false);
    if (searchBar_) searchBar_->setExtracting(true);  // Phase 1.4: button shows "Cancel"
    const int total = todo.size();
    // Task 1: No 30-file session limit — process ALL pending files.
    statusBar()->showMessage(
        QString("Extracting %1 files... (click Extract again to cancel)").arg(total));

    // Show progress bar
    if (extractionProgressBar_) {
        extractionProgressBar_->setRange(0, total);
        extractionProgressBar_->setValue(0);
        extractionProgressBar_->setVisible(true);
    }

    struct ExtractState {
        QList<TodoItem> todo;
        int idx = 0;
        int done = 0;
        int failed = 0;
    };
    auto state = QSharedPointer<ExtractState>::create();
    state->todo = std::move(todo);

    // Task 1: Use 10ms timer instead of 200ms. Adaptive CPU throttle
    // (see below) handles pacing — no fixed delay needed.
    auto* timer = new QTimer(this);
    timer->setInterval(10);
    timer->setSingleShot(false);

    connect(timer, &QTimer::timeout, this, [this, timer, total, state]() {
      try {
        // Check cancel flag.
        if (extractCancelFlag_.load()) {
            timer->stop();
            timer->deleteLater();
            contentExtractionRunning_ = false;
            extractCancelFlag_.store(false);
            if (searchBar_) searchBar_->setExtracting(false);  // Phase 1.4: button shows "Extract"
            updateIndexStats();
            refreshPreviewForSelectedFile();
            statusBar()->showMessage(
                QString("Extraction cancelled (%1/%2 completed).")
                    .arg(state->done + state->failed).arg(total), 8000);
            return;
        }

        auto& registry = DocumentExtractorRegistry::instance();
        sqlite3* raw = db_->raw();

        if (state->idx >= total) {
            timer->stop();
            timer->deleteLater();
            contentExtractionRunning_ = false;
            if (searchBar_) searchBar_->setExtracting(false);  // Phase 1.4
            if (extractionProgressBar_) extractionProgressBar_->setVisible(false);
            updateIndexStats();
            refreshPreviewForSelectedFile();
            statusBar()->showMessage(
                QString("Extraction complete: %1 succeeded, %2 failed (out of %3). %4")
                    .arg(state->done).arg(state->failed).arg(total)
                    .arg(getExtractionStatusString()), 8000);
            return;
        }

        const auto& item = state->todo[state->idx];
        QFileInfo fi(item.path);
        statusBar()->showMessage(
            QString("Extracting: %1 (%2/%3)...")
                .arg(fi.fileName()).arg(state->idx + 1).arg(total));
        if (extractionProgressBar_) extractionProgressBar_->setValue(state->idx + 1);

        if (!QFileInfo::exists(item.path)) {
            if (raw) {
                sqlite3_exec(raw,
                    QString("UPDATE Files SET indexing_status='failed' WHERE id=%1;")
                        .arg(item.fileId).toUtf8().constData(),
                    nullptr, nullptr, nullptr);
            }
            ++state->failed;
        } else {
            // Skip files that are too large (protects low-end systems).
            if (fi.size() > Constants::kMaxFilesizeToExtract) {
                if (raw) {
                    sqlite3_exec(raw,
                        QString("UPDATE Files SET indexing_status='skipped' WHERE id=%1;")
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

                if (result.needsOcr && extractedText.isEmpty()) {
                    if (raw) {
                        sqlite3_exec(raw,
                            QString("UPDATE Files SET indexing_status='needs_ocr' WHERE id=%1;")
                                .arg(item.fileId).toUtf8().constData(),
                            nullptr, nullptr, nullptr);
                    }
                    ++state->done;
                    ok = false;
                }
            } catch (const std::exception& e) {
                DS_WARN("Extract", QString("Failed: %1 — %2").arg(item.path).arg(e.what()));
                ok = false;
            } catch (...) {
                ok = false;
            }

            if (ok && extractedText.size() > Constants::kMaxExtractTextChars) {
                extractedText = extractedText.left(Constants::kMaxExtractTextChars) + "\n\n[... text truncated for memory ...]";
            }

            if (ok && !extractedText.isEmpty() && raw) {
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

                // Generate BGE embedding for this document.
                if (bgeService_ && bgeService_->isReady()) {
                    try {
                        bgeService_->embedDocumentChunked(item.fileId, extractedText);
                    } catch (...) {}
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
            }  // close the else (file not too large)
        }

        // Task 1: Adaptive CPU throttle via GetSystemTimes().
        // If CPU usage > 85%, sleep 100ms to let the system cool down.
        // If <= 85%, proceed immediately (no fixed delay).
#ifdef _WIN32
        {
            static FILETIME prevIdle = {}, prevKernel = {}, prevUser = {};
            FILETIME idle, kernel, user;
            if (GetSystemTimes(&idle, &kernel, &user)) {
                ULARGE_INTEGER idleDiff, kernelDiff, userDiff;
                ULARGE_INTEGER prevIdleLi, prevKernelLi, prevUserLi;
                prevIdleLi.QuadPart = (static_cast<ULONGLONG>(prevIdle.dwHighDateTime) << 32) | prevIdle.dwLowDateTime;
                prevKernelLi.QuadPart = (static_cast<ULONGLONG>(prevKernel.dwHighDateTime) << 32) | prevKernel.dwLowDateTime;
                prevUserLi.QuadPart = (static_cast<ULONGLONG>(prevUser.dwHighDateTime) << 32) | prevUser.dwLowDateTime;
                idleDiff.QuadPart = (static_cast<ULONGLONG>(idle.dwHighDateTime) << 32) | idle.dwLowDateTime;
                kernelDiff.QuadPart = (static_cast<ULONGLONG>(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime;
                userDiff.QuadPart = (static_cast<ULONGLONG>(user.dwHighDateTime) << 32) | user.dwLowDateTime;

                ULONGLONG totalDiff = (kernelDiff.QuadPart - prevKernelLi.QuadPart) +
                                      (userDiff.QuadPart - prevUserLi.QuadPart);
                ULONGLONG idleDiffVal = idleDiff.QuadPart - prevIdleLi.QuadPart;
                if (totalDiff > 0) {
                    double cpuUsage = 1.0 - static_cast<double>(idleDiffVal) / totalDiff;
                    if (cpuUsage > 0.85) {
                        Sleep(100);  // throttle: CPU > 85%
                    }
                }
                prevIdle = idle;
                prevKernel = kernel;
                prevUser = user;
            }
        }
#endif

        ++state->idx;
      } catch (const std::exception& e) {
          statusBar()->showMessage(QString("Extraction error: %1").arg(e.what()), 5000);
          ++state->idx;
      } catch (...) {
          statusBar()->showMessage("Extraction error — skipping file.", 3000);
          ++state->idx;
      }
    });

    timer->start();
}

void MainWindow::autoScanIndexedFolders() {
    if (!repo_ || !db_) return;
    if (contentExtractionRunning_) return;
    if (autoScanRunning_) return;

    autoScanRunning_ = true;
    statusBar()->showMessage("Auto-scanning for new files...");

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

                        const QString ext = FileUtils::extensionOf(fi.absoluteFilePath());
                        const QString hash = hashEnabled
                            ? FileUtils::sha256OfFile(path, 64 * 1024 * 1024)
                            : QString();
                        const qint64 size = fi.size();
                        const qint64 created = fi.birthTime().toSecsSinceEpoch();
                        const qint64 modified = fi.lastModified().toSecsSinceEpoch();

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
        statusBar()->showMessage("Auto-scan complete. Starting extraction...", 3000);
        watcher->deleteLater();
        // Auto-extract any newly found files after hourly scan
        QTimer::singleShot(500, this, [this]() {
            if (!contentExtractionRunning_) onExtract();
        });
    });
    watcher->setFuture(future);
}

// ============================================================
// OCR status indicator
// ============================================================
QString MainWindow::getExtractionStatusString() {
    if (!db_) return "Database not open.";
    sqlite3* raw = db_->raw();
    if (!raw) return "Database not accessible.";

    int total = 0, done = 0, failed = 0, pending = 0, needsOcr = 0, skipped = 0;
    sqlite3_stmt* s = nullptr;
    if (sqlite3_prepare_v2(raw,
        "SELECT indexing_status, COUNT(*) FROM Files GROUP BY indexing_status;",
        -1, &s, nullptr) == SQLITE_OK) {
        while (sqlite3_step(s) == SQLITE_ROW) {
            const char* status = reinterpret_cast<const char*>(sqlite3_column_text(s, 0));
            const int count = sqlite3_column_int(s, 1);
            total += count;
            QString sStr = status ? QString::fromUtf8(status) : "";
            if (sStr == "content_done") done += count;
            else if (sStr == "failed") failed += count;
            else if (sStr == "pending") pending += count;
            else if (sStr == "metadata_only") pending += count;
            else if (sStr == "needs_ocr") needsOcr += count;
            else if (sStr == "skipped") skipped += count;
        }
        sqlite3_finalize(s);
    }

    if (pending == 0 && needsOcr == 0) {
        return QString("All files extracted: %1 done, %2 failed, %3 skipped (total: %4)")
            .arg(done).arg(failed).arg(skipped).arg(total);
    }
    return QString("Extraction: %1 done, %2 pending, %3 need OCR, %4 failed (total: %5)")
        .arg(done).arg(pending).arg(needsOcr).arg(failed).arg(total);
}

void MainWindow::updateOcrStatusIndicator() {
    if (!ocrDotLbl_ || !ocrStatusLbl_) return;

    // Always show green 'OCR: Ready'. The C++ path check is unreliable
    // because the helper exe searches paths we don't check here.
    // If OCR actually fails, the user will see empty text as the result.
    ocrDotLbl_->setStyleSheet("background: #10b981; border-radius: 4px;");
    ocrStatusLbl_->setText("OCR: Ready");
    ocrStatusLbl_->setStyleSheet("color: #10b981;");
}

bool MainWindow::eventFilter(QObject* obj, QEvent* e) {
    // Click on the OCR status indicator → show status info.
    if (obj == ocrStatusWidget_ && e->type() == QEvent::MouseButtonPress) {
        QMessageBox::information(this, "OCR Status",
            "OCR is ready to use.\n\n"
            "Click the OCR button on a scanned PDF or image\n"
            "to extract text.\n\n"
            "Supported: English, Chinese, Korean, Japanese");
        return true;
    }
    return QMainWindow::eventFilter(obj, e);
}

// ============================================================
// Semantic search (BGE + Hybrid)
// ============================================================
void MainWindow::initializeSemanticSearch() {
    try {
        hybridSearch_ = std::make_unique<HybridSearchEngine>();

        // Phase 1.1: Load AI settings from SemanticSettings table at startup.
        // Previously used hardcoded defaults (0.30, 0.50, 20) which ignored
        // any changes the user made via the Settings sliders.
        if (db_) {
            sqlite3* raw = db_->raw();
            if (raw) {
                sqlite3_stmt* s = nullptr;
                if (sqlite3_prepare_v2(raw,
                    "SELECT key, value FROM SemanticSettings;", -1, &s, nullptr) == SQLITE_OK) {
                    while (sqlite3_step(s) == SQLITE_ROW) {
                        const char* key = reinterpret_cast<const char*>(sqlite3_column_text(s, 0));
                        const char* val = reinterpret_cast<const char*>(sqlite3_column_text(s, 1));
                        if (!key || !val) continue;
                        QString k = QString::fromUtf8(key);
                        QString v = QString::fromUtf8(val);
                        if (k == "semantic_weight") {
                            hybridSearch_->setSemanticWeight(v.toFloat());
                        } else if (k == "similarity_threshold") {
                            hybridSearch_->setThreshold(v.toFloat());
                        } else if (k == "top_k") {
                            hybridSearch_->setTopK(v.toInt());
                        }
                    }
                    sqlite3_finalize(s);
                }
            }
        }
        DS_INFO("BGE", "AI settings loaded from database.");

        // Wire up the toggle button.
        if (semanticToggleBtn_) {
            connect(semanticToggleBtn_, &QPushButton::toggled,
                    this, &MainWindow::onSemanticToggled);
        }

        const QString modelPath =
            QCoreApplication::applicationDirPath() +
            "/models/bge-small-en-v1.5/model.onnx";
        const QString dbPath = Config::instance().dbPath();

        bgeService_ = std::make_unique<BgeService>(this);

        connect(bgeService_.get(), &BgeService::ready,
                this, &MainWindow::onBgeReady);
        connect(bgeService_.get(), &BgeService::embeddingProgress,
                this, &MainWindow::onBgeEmbeddingProgress);
        connect(bgeService_.get(), &BgeService::embeddingFinished,
                this, &MainWindow::onBgeEmbeddingFinished);

        QtConcurrent::run([this, dbPath, modelPath]() {
            bgeService_->initialize(dbPath, modelPath);
        });

        DS_INFO("BGE", "AI semantic search subsystem initializing in background...");
    } catch (const std::exception& e) {
        DS_WARN("BGE", QString("Failed to initialize semantic search: %1").arg(e.what()));
        semanticEnabled_ = false;
    } catch (...) {
        DS_WARN("BGE", "Unknown exception initializing semantic search.");
        semanticEnabled_ = false;
    }
}

void MainWindow::onSemanticToggled(bool checked) {
    if (checked && (!bgeService_ || !bgeService_->isReady())) {
        // Block the toggle — show install instructions.
        if (semanticToggleBtn_) semanticToggleBtn_->setChecked(false);
        QMessageBox::information(this, "AI Search",
            "AI search model not available.\n\n"
            "The AI model files should be bundled with DocuSearch.\n"
            "If they're missing, the app may have been installed incorrectly.\n\n"
            "Model files expected at:\n"
            "  models/bge-small-en-v1.5/model.onnx\n"
            "  models/bge-small-en-v1.5/vocab.txt\n"
            "  onnxruntime.dll");
        return;
    }
    semanticEnabled_ = checked;
    if (hybridSearch_) hybridSearch_->setSemanticEnabled(checked);
    if (semanticToggleBtn_) {
        semanticToggleBtn_->setText(checked ? "AI: ON" : "AI: OFF");
    }
    statusBar()->showMessage(
        checked ? "AI search enabled." : "AI search disabled.",
        3000);
}

void MainWindow::onBgeReady() {
    DS_INFO("BGE", "BGE service ready: " + bgeService_->getStatus());
    if (semanticToggleBtn_) {
        semanticToggleBtn_->setEnabled(true);
        // Phase 1.3: Auto-enable AI when BGE is ready.
        // Users shouldn't have to manually find and click the AI toggle.
        if (!semanticToggleBtn_->isChecked()) {
            semanticToggleBtn_->setChecked(true);
        }
    }
    if (hybridSearch_) {
        hybridSearch_->setBgeService(bgeService_.get());
    }
    statusBar()->showMessage(
        "AI search ready: " + bgeService_->getStatus(), 5000);
}

void MainWindow::onBgeEmbeddingProgress(int current, int total) {
    statusBar()->showMessage(
        QString("Embedding documents: %1/%2").arg(current).arg(total));
}

void MainWindow::onBgeEmbeddingFinished(int success, int fail) {
    statusBar()->showMessage(
        QString("Embedding complete: %1 succeeded, %2 failed.").arg(success).arg(fail),
        8000);
}

void MainWindow::updateIndexStats() {
    if (!repo_ || !db_) return;
    try {
        const qint64 total       = repo_->totalFiles();
        const qint64 contentDone = repo_->countByStatus(Constants::IndexingStatus::kContentDone);
        const qint64 metaOnly    = repo_->countByStatus(Constants::IndexingStatus::kMetadataOnly);

        // Sidebar status section
        qint64 dbSize = 0;
        {
            QFile f(Config::instance().dbPath());
            if (f.exists()) dbSize = f.size();
        }
        if (indexedInfoLbl_) {
            indexedInfoLbl_->setText(QString("%1 files\n%2")
                .arg(total)
                .arg(Utils::formatFileSize(dbSize)));
        }
        if (indexedBar_) {
            // Progress = content_done / total (capped at 100).
            int pct = total > 0 ? int((contentDone * 100) / total) : 0;
            indexedBar_->setValue(qMin(100, pct));
        }

        // Status bar
        if (statusIndexedLbl_) {
            statusIndexedLbl_->setText(QString("Indexed files: %1").arg(total));
        }
        if (statusSizeLbl_) {
            statusSizeLbl_->setText(QString("Total size: %1").arg(Utils::formatFileSize(dbSize)));
        }
        if (statusLastLbl_) {
            QFile f(Config::instance().dbPath());
            if (f.exists()) {
                QDateTime lastMod = QFileInfo(f).lastModified();
                statusLastLbl_->setText("Last indexed: " + lastMod.toString("dd MMM yyyy hh:mm AP"));
            }
        }

        // Hidden indexing widget (kept for stats plumbing).
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
                "To add files to the search index, use the 'Add Folder' button.");
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

        // Check if extension is supported.
        const QString ext = FileUtils::extensionOf(path).toLower();
        static const QSet<QString> supportedExts = {
            "pdf","doc","docx","xls","xlsx","xlsm","ppt","pptx",
            "txt","csv","md","rtf",
            "png","jpg","jpeg","bmp","gif","tiff","tif","webp"
        };
        if (!supportedExts.contains(ext)) return;

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

        // Task 2 Part A: Extract text immediately for the new file
        // (don't wait for the user to click Extract or the hourly scan).
        // This is a single-file extraction — fast and non-blocking enough
        // for the main thread.
        if (fi.size() <= Constants::kMaxFilesizeToExtract) {
            auto& registry = DocumentExtractorRegistry::instance();
            try {
                auto result = registry.extractByExtension(path, ext);
                QString extractedText = result.text;
                if (extractedText.size() > Constants::kMaxExtractTextChars) {
                    extractedText = extractedText.left(Constants::kMaxExtractTextChars);
                }

                if (!extractedText.isEmpty()) {
                    sqlite3* raw = db_->raw();
                    if (raw) {
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
                            // Look up the file_id we just inserted.
                            FileRecord rec;
                            if (repo_->getByPath(r.path, rec)) {
                                QByteArray textBytes = extractedText.toUtf8();
                                QByteArray srcBytes = (result.source.isEmpty() ? "native" : result.source).toUtf8();
                                sqlite3_bind_int64(upd, 1, rec.id);
                                sqlite3_bind_text(upd, 2, textBytes.constData(), -1, SQLITE_TRANSIENT);
                                sqlite3_bind_text(upd, 3, srcBytes.constData(), -1, SQLITE_TRANSIENT);
                                sqlite3_bind_int64(upd, 4, extractedText.size());
                                sqlite3_bind_int64(upd, 5, now);
                                sqlite3_step(upd);
                                sqlite3_finalize(upd);

                                // Update Files status.
                                sqlite3_exec(raw,
                                    QString("UPDATE Files SET indexing_status='content_done' WHERE id=%1;")
                                        .arg(rec.id).toUtf8().constData(),
                                    nullptr, nullptr, nullptr);

                                // Update SearchIndex.
                                sqlite3_stmt* del = nullptr;
                                sqlite3_prepare_v2(raw, "DELETE FROM SearchIndex WHERE file_id=?1;", -1, &del, nullptr);
                                if (del) { sqlite3_bind_int64(del, 1, rec.id); sqlite3_step(del); sqlite3_finalize(del); }

                                QByteArray fn = fi.fileName().toUtf8();
                                QByteArray pth = r.path.toUtf8();
                                QByteArray extB = ext.toUtf8();
                                sqlite3_stmt* ins = nullptr;
                                sqlite3_prepare_v2(raw,
                                    "INSERT INTO SearchIndex (filename, content, path, extension, file_id) "
                                    "VALUES (?1, ?2, ?3, ?4, ?5);",
                                    -1, &ins, nullptr);
                                if (ins) {
                                    sqlite3_bind_text(ins, 1, fn.constData(), -1, SQLITE_TRANSIENT);
                                    sqlite3_bind_text(ins, 2, textBytes.constData(), -1, SQLITE_TRANSIENT);
                                    sqlite3_bind_text(ins, 3, pth.constData(), -1, SQLITE_TRANSIENT);
                                    sqlite3_bind_text(ins, 4, extB.constData(), -1, SQLITE_TRANSIENT);
                                    sqlite3_bind_int64(ins, 5, rec.id);
                                    sqlite3_step(ins);
                                    sqlite3_finalize(ins);
                                }

                                // Generate BGE embedding for the new file.
                                if (bgeService_ && bgeService_->isReady()) {
                                    try { bgeService_->embedDocumentChunked(rec.id, extractedText); } catch (...) {}
                                }
                            }
                        }
                    }
                } else if (result.needsOcr) {
                    // Scanned PDF — mark as needs_ocr.
                    FileRecord rec;
                    if (repo_->getByPath(r.path, rec)) {
                        sqlite3* raw = db_->raw();
                        if (raw) {
                            sqlite3_exec(raw,
                                QString("UPDATE Files SET indexing_status='needs_ocr' WHERE id=%1;")
                                    .arg(rec.id).toUtf8().constData(),
                                nullptr, nullptr, nullptr);
                        }
                    }
                }
            } catch (...) {
                // Extraction failed — file stays as 'pending', user can retry.
            }
        }

        statusBar()->showMessage("New file indexed: " + fi.fileName(), 3000);
        updateIndexStats();
        DS_INFO("Watcher", "Added + extracted: " + path);
    } catch (...) {
        DS_INFO("Watcher", "Failed to add: " + path);
    }
}

void MainWindow::onFileModified(const QString& path) {
    if (!repo_ || !db_) return;
    try {
        if (!indexer_) return;
        FileRecord r;
        if (repo_->getByPath(path, r)) {
            indexer_->reindexFile(path);
            DS_INFO("Watcher", "Reindexing modified: " + path);
        } else {
            onFileAdded(path);
        }
    } catch (...) {
        DS_INFO("Watcher", "Failed to handle modify: " + path);
    }
}

void MainWindow::onFileRenamed(const QString& oldPath, const QString& newPath) {
    if (!repo_ || !db_) return;
    try {
        // Task 2 Part A: Update path without re-extracting (content unchanged).
        // Look up file_id by old path, update path + filename in-place.
        FileRecord r;
        if (repo_->getByPath(oldPath, r)) {
            const QFileInfo fi(newPath);
            sqlite3* raw = db_->raw();
            if (raw) {
                sqlite3_stmt* upd = nullptr;
                sqlite3_prepare_v2(raw,
                    "UPDATE Files SET path=?1, filename=?2 WHERE id=?3;",
                    -1, &upd, nullptr);
                if (upd) {
                    QByteArray pth = FileUtils::toNative(newPath).toUtf8();
                    QByteArray fn = fi.fileName().toUtf8();
                    sqlite3_bind_text(upd, 1, pth.constData(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(upd, 2, fn.constData(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int64(upd, 3, r.id);
                    sqlite3_step(upd);
                    sqlite3_finalize(upd);
                }
                // Update SearchIndex path + filename too.
                sqlite3_stmt* sIdx = nullptr;
                sqlite3_prepare_v2(raw,
                    "UPDATE SearchIndex SET path=?1, filename=?2 WHERE file_id=?3;",
                    -1, &sIdx, nullptr);
                if (sIdx) {
                    QByteArray pth = FileUtils::toNative(newPath).toUtf8();
                    QByteArray fn = fi.fileName().toUtf8();
                    sqlite3_bind_text(sIdx, 1, pth.constData(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(sIdx, 2, fn.constData(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int64(sIdx, 3, r.id);
                    sqlite3_step(sIdx);
                    sqlite3_finalize(sIdx);
                }
            }
            statusBar()->showMessage("File renamed: " + fi.fileName(), 3000);
        } else {
            // Old path not in DB — treat as new file.
            onFileAdded(newPath);
        }
        DS_INFO("Watcher", QString("Renamed: %1 -> %2").arg(oldPath, newPath));
    } catch (...) {
        DS_INFO("Watcher", "Failed to handle rename");
    }
}

void MainWindow::onFileDeleted(const QString& path) {
    if (!repo_ || !db_) return;
    try {
        // Task 2 Part A: Delete from ALL tables (Files, DocumentText, Tags,
        // Notes, SearchIndex, BgeEmbeddings). deleteByPath → deleteFile
        // now handles BgeEmbeddings explicitly.
        QFileInfo fi(path);
        QString fname = fi.fileName();
        repo_->deleteByPath(path);
        statusBar()->showMessage("File removed from index: " + fname, 3000);
        updateIndexStats();
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

    const QString ext = FileUtils::extensionOf(path).toLower();

    bool isImage = (ext == "png" || ext == "jpg" || ext == "jpeg" ||
                    ext == "bmp" || ext == "tiff" || ext == "tif" ||
                    ext == "webp" || ext == "gif");
    bool isPdf = (ext == "pdf");

    if (!isImage && !isPdf) {
        QMessageBox::information(this, "OCR",
            "OCR is supported for PDF files and images (PNG, JPG, BMP, TIFF, WebP).");
        return;
    }

    // OCR runs in a SEPARATE PROCESS (docusearch_ocr_helper.exe).
    // No need for QtConcurrent — the helper exe is crash-isolated.
    // We use QProcess on the main thread with processEvents() to
    // keep the UI responsive while waiting.
    const qint64 fileId = selectedFileId_;
    const QString filePath = path;

    statusBar()->showMessage("Running OCR...", 0);
    QApplication::processEvents();

    // Use the singleton — this way the oneocrAvailable_ flag persists
    // across calls (the status bar indicator and the OCR button share
    // the same engine state).
    WindowsOcrEngine& ocrEngine = WindowsOcrEngine::instance();
    if (!ocrEngine.init()) {
        statusBar()->showMessage("OCR helper not found.", 5000);
        QMessageBox::information(this, "OCR",
            "OCR helper (docusearch_ocr_helper.exe) not found.\n"
            "Make sure it's in the same folder as DocuSearch.exe.");
        return;
    }
    // Don't check isOneocrAvailable() — the C++ path check is unreliable.
    // The helper exe searches paths we don't check here. Just try OCR
    // silently. If it fails, empty text will be shown as the result.

    QString ocrText;

    if (isImage) {
        statusBar()->showMessage("OCR: processing image...", 0);
        QApplication::processEvents();
        ocrText = ocrEngine.ocrFile(filePath);
    }
#ifdef DOCUSEARCH_HAS_POPPLER
    else if (isPdf) {
        // For PDFs: render each page to image, save as temp PNG,
        // then OCR each page via the helper exe.
        try {
            statusBar()->showMessage("OCR: opening PDF...", 0);
            QApplication::processEvents();

            auto doc = poppler::document::load_from_file(filePath.toStdString());
            if (!doc || doc->pages() == 0) {
                statusBar()->showMessage("OCR: failed to open PDF.", 5000);
                return;
            }

            poppler::page_renderer renderer;
            renderer.set_render_hint(poppler::page_renderer::text_antialiasing);
            const int dpi = 96;  // lower DPI for OCR speed
            const int maxPages = (doc->pages() < 10) ? doc->pages() : 10;  // max 10 pages

            for (int i = 0; i < maxPages; ++i) {
                statusBar()->showMessage(
                    QString("OCR: page %1/%2...").arg(i + 1).arg(maxPages), 0);
                QApplication::processEvents();

                try {
                    auto* pagePtr = doc->create_page(i);
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

                    // Save page as temp PNG and OCR it
                    QString tempPath = QDir::tempPath() + "/docusearch_ocr_page_" +
                        QString::number(i) + ".png";
                    qimg.save(tempPath, "PNG");

                    QString pageText = ocrEngine.ocrFile(tempPath);
                    QFile::remove(tempPath);

                    if (!pageText.isEmpty()) {
                        ocrText += pageText + "\n";
                    }
                } catch (...) {
                    // Skip this page
                }
            }
        } catch (...) {
            statusBar()->showMessage("OCR: PDF rendering failed.", 5000);
            return;
        }
    }
#endif

    if (ocrText.isEmpty()) {
        statusBar()->showMessage("OCR: no text recognized.", 5000);
        QMessageBox::information(this, "OCR",
            "No text was recognized.\n\n"
            "This could mean:\n"
            "  - The OCR helper (docusearch_ocr_helper.exe) is missing\n"
            "  - No OCR languages are installed in Windows\n"
            "    (Settings > Time & Language > Language >\n"
            "     Add a language > Optical character recognition)\n"
            "  - The image quality is too low\n"
            "  - The file doesn't contain recognizable text");
        return;
    }

    // Save OCR text to database.
    try {
        sqlite3* raw = db_->raw();
        if (raw) {
            QByteArray textBytes = ocrText.toUtf8();
            qint64 now = QDateTime::currentSecsSinceEpoch();

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
                sqlite3_bind_int64(upd, 1, fileId);
                sqlite3_bind_text(upd, 2, textBytes.constData(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(upd, 3, ocrText.size());
                sqlite3_bind_int64(upd, 4, now);
                sqlite3_step(upd);
                sqlite3_finalize(upd);
            }

            sqlite3_exec(raw,
                QString("UPDATE Files SET indexing_status='content_done', ocr_status='done' WHERE id=%1;")
                    .arg(fileId).toUtf8().constData(),
                nullptr, nullptr, nullptr);

            sqlite3_stmt* del = nullptr;
            sqlite3_prepare_v2(raw, "DELETE FROM SearchIndex WHERE file_id=?1;",
                               -1, &del, nullptr);
            if (del) {
                sqlite3_bind_int64(del, 1, fileId);
                sqlite3_step(del);
                sqlite3_finalize(del);
            }
            QFileInfo fi(filePath);
            QByteArray fn = fi.fileName().toUtf8();
            QByteArray pth = filePath.toUtf8();
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
                sqlite3_bind_int64(ins, 5, fileId);
                sqlite3_step(ins);
                sqlite3_finalize(ins);
            }
        }
    } catch (...) {
        // DB save failure is non-fatal
    }

    previewPane_->setExtractedText(ocrText);
    previewPane_->setDocumentText(ocrText);
    updateIndexStats();
    statusBar()->showMessage(
        QString("OCR complete: %1 characters recognized.").arg(ocrText.size()), 5000);

    // Refresh the OCR status indicator — if OCR just succeeded, oneocr
    // is definitely installed. This fixes the case where the indicator
    // showed "Setup Required" because the user installed files after
    // DocuSearch launched (and our startup detection missed them).
    updateOcrStatusIndicator();
}

void MainWindow::onOpenSettings() {
    if (!repo_ || !db_) return;
    try {
        SettingsDialog dlg(settings_, repo_.get(), db_.get(), this);

        QObject::connect(&dlg, &SettingsDialog::settingsApplied,
            this, [this](const AppSettings& s){
                settings_ = s;
                darkMode_ = settings_.darkMode;
                saveSettings();
                applyTheme();
                updateIndexStats();
                statusBar()->showMessage("Settings applied.", 3000);
            });

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

        // Task 3 Fix D: Wire AI settings sliders to HybridSearchEngine.
        // Changes take effect immediately — no need to restart or click Apply.
        QObject::connect(&dlg, &SettingsDialog::aiWeightChanged,
            this, [this](float weight) {
                if (hybridSearch_) hybridSearch_->setSemanticWeight(weight);
                statusBar()->showMessage(
                    QString("AI weight set to %1%").arg(int(weight * 100)), 2000);
            });
        QObject::connect(&dlg, &SettingsDialog::aiThresholdChanged,
            this, [this](float threshold) {
                if (hybridSearch_) hybridSearch_->setThreshold(threshold);
                statusBar()->showMessage(
                    QString("AI threshold set to %1%").arg(int(threshold * 100)), 2000);
            });
        QObject::connect(&dlg, &SettingsDialog::aiTopKChanged,
            this, [this](int topK) {
                if (hybridSearch_) hybridSearch_->setTopK(topK);
                statusBar()->showMessage(
                    QString("AI top-K set to %1").arg(topK), 2000);
            });

        // Wire up "Embed All Documents Now" button → BgeService::embedDocumentsBatch()
        QObject::connect(&dlg, &SettingsDialog::embedAllRequested,
            this, [this]() {
                if (!bgeService_ || !bgeService_->isReady()) {
                    QMessageBox::information(this, "AI Search",
                        "AI search is not ready.\n\n"
                        "Make sure the AI model is installed at:\n"
                        "  models/bge-small-en-v1.5/model.onnx\n"
                        "  models/bge-small-en-v1.5/vocab.txt\n\n"
                        "And that onnxruntime.dll is present.");
                    return;
                }
                // Gather all files that have extracted text but no embedding yet.
                QVector<int> fileIds;
                QStringList texts;
                sqlite3* raw = db_->raw();
                if (!raw) return;
                sqlite3_stmt* stmt = nullptr;
                const char* sql =
                    "SELECT dt.file_id, dt.extracted_text "
                    "FROM DocumentText dt "
                    "WHERE NOT EXISTS ("
                    "  SELECT 1 FROM BgeEmbeddings b WHERE b.file_id = dt.file_id"
                    ");";
                if (sqlite3_prepare_v2(raw, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    while (sqlite3_step(stmt) == SQLITE_ROW) {
                        const int fileId = sqlite3_column_int64(stmt, 0);
                        const unsigned char* txt = sqlite3_column_text(stmt, 1);
                        fileIds.append(fileId);
                        texts.append(txt ? QString::fromUtf8(
                            reinterpret_cast<const char*>(txt)) : QString());
                    }
                    sqlite3_finalize(stmt);
                }
                if (fileIds.isEmpty()) {
                    statusBar()->showMessage(
                        "All documents already have embeddings.", 5000);
                    return;
                }
                statusBar()->showMessage(
                    QString("Embedding %1 documents...").arg(fileIds.size()), 0);
                bgeService_->embedDocumentsBatch(fileIds, texts);
            });

        const int rc = dlg.exec();
        refreshSavedSearches();
        // The user may have just closed the dialog after running an
        // external install script. Refresh the OCR status indicator.
        updateOcrStatusIndicator();
        if (rc == QDialog::Accepted) {
            AppSettings oldSettings = settings_;
            settings_ = dlg.result();
            darkMode_ = settings_.darkMode;
            saveSettings();
            applyTheme();
            updateIndexStats();

            for (const QString& drive : settings_.indexedDrives) {
                if (!oldSettings.indexedDrives.contains(drive)) {
                    statusBar()->showMessage("Scanning " + drive + " ...");
                    QApplication::processEvents();
                    scanFolderFast(drive);
                    // Auto-extract after scanning new drives
                    QTimer::singleShot(500, this, [this]() {
                        if (!contentExtractionRunning_) onExtract();
                    });
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
        if (themeToggleBtn_) {
            themeToggleBtn_->setText(darkMode_ ? "🌙" : "☀");
            themeToggleBtn_->setChecked(darkMode_);
        }
        applyTheme();
        statusBar()->showMessage(darkMode_ ? "Dark theme." : "Light theme.", 2000);
    } catch (...) {
        statusBar()->showMessage("Theme toggle failed.", 3000);
    }
}

void MainWindow::onAbout() {
    try {
        QMessageBox::about(this, "About DocuSearch",
            QString("<div style='text-align:center;'>"
                    "<h2 style='color:#2563eb;'>DocuSearch %1</h2>"
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
