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
// Central area: sidebar + (search bar + 3-panel splitter) + right panel
// ============================================================
void MainWindow::buildCentral() {
    // The central widget's main layout was created in the ctor.
    // We add a horizontal layout containing: sidebar + center + right panel.
    auto* centralWidget = this->centralWidget();
    auto* mainLay = qobject_cast<QVBoxLayout*>(centralWidget->layout());

    auto* hLay = new QHBoxLayout();
    hLay->setContentsMargins(0, 0, 0, 0);
    hLay->setSpacing(0);
    mainLay->addLayout(hLay, 1);

    // ============================================================
    // 1) LEFT SIDEBAR (170px — compact professional width)
    // ============================================================
    sidebar_ = new QWidget(centralWidget);
    sidebar_->setObjectName("sidebar");
    sidebar_->setFixedWidth(140);

    auto* sidebarLay = new QVBoxLayout(sidebar_);
    sidebarLay->setContentsMargins(0, 0, 0, 0);
    sidebarLay->setSpacing(0);

    sidebarList_ = new QListWidget(sidebar_);
    sidebarList_->setObjectName("sidebar");
    const QStringList navLabels = {
        "Search", "Saved", "Tags", "Notes",
        "Stats", "Settings", "Help", "About"
    };
    for (int i = 0; i < navLabels.size(); ++i) {
        auto* item = new QListWidgetItem(navLabels[i], sidebarList_);
        item->setData(Qt::UserRole, navLabels[i]);
        item->setSizeHint(QSize(150, 36));
    }
    if (sidebarList_->count() > 0) {
        sidebarList_->setCurrentRow(0);
    }
    sidebarLay->addWidget(sidebarList_, 1);

    // Indexed status section pinned to bottom of sidebar.
    auto* statusSection = new QWidget(sidebar_);
    statusSection->setObjectName("indexedStatus");
    auto* sLay = new QVBoxLayout(statusSection);
    sLay->setContentsMargins(16, 12, 16, 12);
    sLay->setSpacing(4);

    auto* headerRow = new QWidget(statusSection);
    auto* hrLay = new QHBoxLayout(headerRow);
    hrLay->setContentsMargins(0, 0, 0, 0);
    hrLay->setSpacing(6);
    auto* dotLbl = new QLabel(headerRow);
    dotLbl->setFixedSize(8, 8);
    // dotLbl styled by QSS
    indexedHeaderLbl_ = new QLabel("Indexed", headerRow);
    indexedHeaderLbl_->setObjectName("indexedHeader");
    hrLay->addWidget(dotLbl);
    hrLay->addWidget(indexedHeaderLbl_);
    hrLay->addStretch();
    sLay->addWidget(headerRow);

    indexedInfoLbl_ = new QLabel("0 files\n0 B", statusSection);
    indexedInfoLbl_->setObjectName("indexedInfo");
    sLay->addWidget(indexedInfoLbl_);

    indexedBar_ = new QProgressBar(statusSection);
    indexedBar_->setObjectName("indexedBar");
    indexedBar_->setRange(0, 100);
    indexedBar_->setValue(0);
    indexedBar_->setTextVisible(false);
    sLay->addWidget(indexedBar_);

    sidebarLay->addWidget(statusSection);

    hLay->addWidget(sidebar_);

    // ============================================================
    // 2) CENTER PANEL (search bar + results | viewer splitter)
    // ============================================================
    auto* centerWidget = new QWidget(centralWidget);
    auto* centerLay = new QVBoxLayout(centerWidget);
    centerLay->setContentsMargins(0, 0, 0, 0);
    centerLay->setSpacing(0);

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

    previewPane_ = new PreviewPane(mainSplitter_);
    previewPane_->setObjectName("previewPane");
    previewPane_->setMinimumWidth(360);
    mainSplitter_->addWidget(previewPane_);

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

    openLocationBtn_ = new QPushButton(sb);
    openLocationBtn_->setObjectName("openLocationBtn");
    openLocationBtn_->setCursor(Qt::PointingHandCursor);
    openLocationBtn_->setText("Open Location");
    openLocationBtn_->setToolTip("Open the folder containing the selected file");
    sb->addPermanentWidget(openLocationBtn_);
}

void MainWindow::applyTheme() {
    // Light mode only — no dark mode.
    // CRITICAL: Do NOT call setPalette() on individual widgets.
    // Setting a palette on a widget makes Qt use the palette INSTEAD
    // of the QSS stylesheet for that widget, which overrides our
    // styling and causes the "old UI" appearance.
    //
    // The QSS stylesheet (set via Theme::apply) handles ALL styling.
    // We only set the application-wide palette for fallback colors
    // (used by widgets that don't have QSS rules).
    QPalette pal;
    pal.setColor(QPalette::Window,          QColor(0xff, 0xff, 0xff));
    pal.setColor(QPalette::Base,            QColor(0xff, 0xff, 0xff));
    pal.setColor(QPalette::WindowText,      QColor(0x1a, 0x1a, 0x1a));
    pal.setColor(QPalette::Text,            QColor(0x1a, 0x1a, 0x1a));
    pal.setColor(QPalette::ButtonText,      QColor(0x1a, 0x1a, 0x1a));
    pal.setColor(QPalette::Button,          QColor(0xff, 0xff, 0xff));
    pal.setColor(QPalette::Highlight,       QColor(0x25, 0x63, 0xeb));
    pal.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
    pal.setColor(QPalette::ToolTipBase,     QColor(0x1a, 0x1a, 0x1a));
    pal.setColor(QPalette::ToolTipText,     QColor(0xff, 0xff, 0xff));
    pal.setColor(QPalette::Disabled, QPalette::WindowText,  QColor(160, 160, 160));
    pal.setColor(QPalette::Disabled, QPalette::Text,        QColor(160, 160, 160));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText,  QColor(160, 160, 160));

    QApplication::setPalette(pal);

    // Apply the QSS stylesheet — this is the PRIMARY styling mechanism.
    Theme::apply(Theme::Mode::Light);

    // Do NOT call setPalette() on child widgets — let the QSS handle it.
    // Just trigger a global style recalculation.
    qApp->setStyleSheet(qApp->styleSheet());

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
    } else if (page == "Saved") {
        // Show the saved searches dropdown.
        statusBar()->showMessage("Use the 'Saved Searches' dropdown in the search bar.", 5000);
        sidebarList_->setCurrentRow(0);
    } else if (page == "Tags") {
        statusBar()->showMessage("Tags are shown in the right panel for the selected file.", 5000);
        sidebarList_->setCurrentRow(0);
    } else if (page == "Notes") {
        statusBar()->showMessage("Notes are shown in the right panel for the selected file.", 5000);
        sidebarList_->setCurrentRow(0);
    }
    // "Search" stays as the current page.
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
        statusBar()->showMessage("No files need content extraction.", 3000);
        return;
    }

    contentExtractionRunning_ = true;
    const int total = todo.size();
    const int maxFilesThisSession = qMin(total, 30);
    statusBar()->showMessage(
        QString("Extracting %1 of %2 files...").arg(maxFilesThisSession).arg(total));

    // Show progress bar
    if (extractionProgressBar_) {
        extractionProgressBar_->setRange(0, maxFilesThisSession);
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

    auto* timer = new QTimer(this);
    timer->setInterval(200);  // 200ms between files — gives UI time to process events

    connect(timer, &QTimer::timeout, this, [this, timer, total, maxFilesThisSession, state]() {
      try {
        auto& registry = DocumentExtractorRegistry::instance();
        sqlite3* raw = db_->raw();

        if (state->idx >= total || state->idx >= maxFilesThisSession) {
            timer->stop();
            timer->deleteLater();
            contentExtractionRunning_ = false;
            if (extractionProgressBar_) extractionProgressBar_->setVisible(false);
            updateIndexStats();
            refreshPreviewForSelectedFile();
            if (state->idx >= total) {
                statusBar()->showMessage(
                    QString("Extraction complete: %1 succeeded, %2 failed (out of %3).")
                        .arg(state->done).arg(state->failed).arg(total), 8000);
            } else {
                statusBar()->showMessage(
                    QString("Extracted %1 of %2 files. Click Extract again to continue.")
                        .arg(state->done + state->failed).arg(total), 8000);
            }
            return;
        }

        const auto& item = state->todo[state->idx];
        statusBar()->showMessage(
            QString("Extracting: %1/%2 (done: %3, failed: %4)...")
                .arg(state->idx + 1).arg(maxFilesThisSession).arg(state->done).arg(state->failed));
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
            QFileInfo fi(item.path);
            if (fi.size() > Constants::kMaxFilesizeToExtract) {
                if (raw) {
                    sqlite3_exec(raw,
                        QString("UPDATE Files SET indexing_status='skipped' WHERE id=%1;")
                            .arg(item.fileId).toUtf8().constData(),
                        nullptr, nullptr, nullptr);
                }
                ++state->failed;
                // Don't return — fall through to ++state->idx at the end.
            } else {

            QString extractedText;
            QString source = "native";
            bool ok = false;

            try {
                auto result = registry.extractByExtension(item.path, item.ext);
                extractedText = result.text;
                source = result.source.isEmpty() ? "native" : result.source;
                ok = true;

                // If the extractor says needsOcr and text is empty,
                // mark the file as 'needs_ocr' (not 'failed').
                // The user can run OCR later via the OCR button.
                if (result.needsOcr && extractedText.isEmpty()) {
                    if (raw) {
                        sqlite3_exec(raw,
                            QString("UPDATE Files SET indexing_status='needs_ocr' WHERE id=%1;")
                                .arg(item.fileId).toUtf8().constData(),
                            nullptr, nullptr, nullptr);
                    }
                    ++state->done;  // count as done (just needs OCR later)
                    ok = false;     // skip the DB insert below
                }
            } catch (const std::exception& e) {
                DS_WARN("Extract", QString("Failed: %1 — %2").arg(item.path).arg(e.what()));
                ok = false;
            } catch (...) {
                ok = false;
            }

            // Cap extracted text size to protect memory on low-end systems.
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
            }  // close the else (file not too large)
        }

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
void MainWindow::updateOcrStatusIndicator() {
    if (!ocrDotLbl_ || !ocrStatusLbl_) return;

    WindowsOcrEngine engine;
    engine.init();
    const bool available = engine.isOneocrAvailable();

    if (available) {
        ocrDotLbl_->setStyleSheet("background: #10b981; border-radius: 4px;");  // green
        ocrStatusLbl_->setText("OCR: Ready");
        ocrStatusLbl_->setStyleSheet("color: #10b981;");
    } else {
        ocrDotLbl_->setStyleSheet("background: #f59e0b; border-radius: 4px;");  // amber
        ocrStatusLbl_->setText("OCR: Setup Required");
        ocrStatusLbl_->setStyleSheet("color: #f59e0b;");
    }
}

bool MainWindow::eventFilter(QObject* obj, QEvent* e) {
    // Click on the OCR status indicator → show install instructions.
    if (obj == ocrStatusWidget_ && e->type() == QEvent::MouseButtonPress) {
        WindowsOcrEngine engine;
        engine.init();
        if (!engine.isOneocrAvailable()) {
            QMessageBox::information(this, "OCR Setup Required",
                "OCR is not yet configured.\n\n"
                "DocuSearch uses oneocr.dll (the native OCR engine from the\n"
                "Windows 11 Snipping Tool) for text recognition. These files\n"
                "are NOT bundled with DocuSearch.\n\n"
                "To install OCR support:\n"
                "  1. Open PowerShell in the DocuSearch folder\n"
                "  2. Run:  scripts\\get_oneocr.ps1\n\n"
                "This copies oneocr.dll, oneocr.onemodel, and onnxruntime.dll\n"
                "from your locally-installed Snipping Tool into the DocuSearch\n"
                "folder. See ONEOCR_SETUP.md for details.\n\n"
                "After installing, restart DocuSearch and try OCR again.");
        } else {
            QMessageBox::information(this, "OCR Status",
                "OCR is ready to use.\n\n"
                "Click the green OCR button on a scanned PDF or image\n"
                "to extract text using oneocr.dll.\n\n"
                "Supported languages: English, Chinese (Simplified/Traditional),\n"
                "Korean, Japanese (auto-detected by the model).");
        }
        return true;
    }
    return QMainWindow::eventFilter(obj, e);
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

    WindowsOcrEngine ocrEngine;
    if (!ocrEngine.init()) {
        statusBar()->showMessage("OCR helper not found.", 5000);
        QMessageBox::information(this, "OCR",
            "OCR helper (docusearch_ocr_helper.exe) not found.\n"
            "Make sure it's in the same folder as DocuSearch.exe.");
        return;
    }
    if (!ocrEngine.isOneocrAvailable()) {
        statusBar()->showMessage("oneocr.dll not installed.", 5000);
        QMessageBox::warning(this, "OCR Setup Required",
            "OCR is not yet configured.\n\n"
            "DocuSearch uses oneocr.dll (the native OCR engine from the\n"
            "Windows 11 Snipping Tool) for text recognition. These files\n"
            "are NOT bundled with DocuSearch.\n\n"
            "To install OCR support:\n"
            "  1. Open PowerShell in the DocuSearch folder\n"
            "  2. Run:  scripts\\get_oneocr.ps1\n\n"
            "This copies oneocr.dll, oneocr.onemodel, and onnxruntime.dll\n"
            "from your locally-installed Snipping Tool into the DocuSearch\n"
            "folder. See ONEOCR_SETUP.md for details.\n\n"
            "After installing, restart DocuSearch and try OCR again.");
        return;
    }

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

        const int rc = dlg.exec();
        refreshSavedSearches();
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
        applyTheme();
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
