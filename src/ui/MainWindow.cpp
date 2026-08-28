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
#include "SwitchControl.h"

#include "../core/Config.h"
#include "../core/Constants.h"
#include "../core/Logger.h"
#include "../core/SehTranslator.h"
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
#include <QSettings>
#include <QFile>
#include <QTextStream>
#include <QProgressDialog>
#include <QThread>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QStyle>
#include <QStyleFactory>
#include <QFrame>
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
            // Delegate to the platform's native move loop instead of manual
            // owner_->move() tracking. The manual tracker had a fatal flaw:
            // if the release happened outside the window (Alt+Tab, a toast
            // stealing focus, Win+Down minimizing mid-drag), dragging_ stayed
            // true forever and every later mouse-move teleported the window —
            // which also wedged WM_NCHITTEST resize handling until restart.
            // startSystemMove() hands control to Windows, supports Aero Snap,
            // and can never get stuck in a half-finished drag.
            if (owner_->windowHandle())
                owner_->windowHandle()->startSystemMove();
            e->accept();
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        QWidget::mouseMoveEvent(e);  // native loop owns movement
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        QWidget::mouseReleaseEvent(e);
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

    // Probe the OCR engine NOW (cheap helper-exe existence check —
    // no WinRT init, no language pack load). This sets
    // WindowsOcrEngine::isAvailable() based on whether the helper
    // exe is present and whether the last OCR call surfaced a
    // "no language packs" error. Without this call, the indicator
    // would default to "Ready" — fine on most Windows installs.
    WindowsOcrEngine::instance().init();

    // Update the OCR availability indicator on the status bar.
    // Clicking the indicator shows the install-instructions dialog.
    updateOcrStatusIndicator();
    if (ocrStatusWidget_) {
        ocrStatusWidget_->setCursor(Qt::PointingHandCursor);
        ocrStatusWidget_->installEventFilter(this);
    }

    // DEFER semantic search init to after the window is shown.
    // initializeSemanticSearch() creates a BgeService + QtConcurrent::run
    // which, even though it runs on a worker thread, still allocates memory
    // and loads the ONNX model path check on the main thread. Deferring it
    // means the window appears faster.
    QTimer::singleShot(100, this, [this]() {
        initializeSemanticSearch();
    });

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
    // Use explicit lambdas — defensive against Qt 6 member-function-pointer
    // ambiguity on QWidget slots. (User reported minimize not working.)
    connect(titleMinBtn_, &QPushButton::clicked,
            this, [this]{ this->showMinimized(); });
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

    // P0.1: Restore window geometry and splitter sizes.
    {
        QSettings qs(QSettings::IniFormat, QSettings::UserScope, "DocuSearch", "DocuSearch");
        const QByteArray geom = qs.value("geometry").toByteArray();
        if (!geom.isEmpty()) restoreGeometry(geom);
        const QByteArray ws = qs.value("windowState").toByteArray();
        if (!ws.isEmpty()) restoreState(ws);
        // Defer splitter restore — they need to be fully laid out first.
        QTimer::singleShot(100, this, [this]() {
            QSettings qs2(QSettings::IniFormat, QSettings::UserScope, "DocuSearch", "DocuSearch");
            if (mainSplitter_) {
                const QByteArray ms = qs2.value("mainSplitter").toByteArray();
                if (!ms.isEmpty()) mainSplitter_->restoreState(ms);
            }
            if (rightSplitter_) {
                const QByteArray rs = qs2.value("rightSplitter").toByteArray();
                if (!rs.isEmpty()) rightSplitter_->restoreState(rs);
            }
        });
    }

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
    // Cancel any in-progress extraction so the timer callback doesn't
    // fire on a half-destroyed window.
    extractCancelFlag_.store(true);
    if (indexer_) indexer_->stopIndexing();
    if (ocrPool_) ocrPool_->shutdown();
    if (watcher_) watcher_->stop();
    if (db_)      db_->close();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    // P0.1: Persist window geometry and splitter sizes.
    QSettings qs(QSettings::IniFormat, QSettings::UserScope, "DocuSearch", "DocuSearch");
    qs.setValue("geometry", saveGeometry());
    qs.setValue("windowState", saveState());
    if (mainSplitter_) qs.setValue("mainSplitter", mainSplitter_->saveState());
    if (rightSplitter_) qs.setValue("rightSplitter", rightSplitter_->saveState());

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

// Handle WM_NCCALCSIZE so the re-added WS_THICKFRAME (see
// enableNativeResize) never visually reserves nonclient frame pixels:
// our widgets keep owning the entire window. While maximized we pull
// the client rect back inside the monitor by the invisible frame pad.
// Handle WM_NCHITTEST on Windows so the frameless window can be resized
// from its edges (the OS doesn't provide resize handles for frameless
// windows, so we tell it which pixels belong to which resize border).
bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG" && message) {
        MSG* msg = reinterpret_cast<MSG*>(message);
        if (msg->message == WM_NCCALCSIZE) {
            if (msg->wParam) {
                if (IsZoomed(msg->hwnd)) {
                    // Some SDK configurations hide these post-XP metrics.
#ifndef SM_CXSIZEFRAME
#define SM_CXSIZEFRAME 32
#endif
#ifndef SM_CYPADDEDBWIDTH
#define SM_CYPADDEDBWIDTH 93
#endif
#ifndef SM_CXPADDEDBWIDTH
#define SM_CXPADDEDBWIDTH 92
#endif
                    auto* nccs = reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam);
                    const int padX = GetSystemMetrics(SM_CXSIZEFRAME)
                                   + GetSystemMetrics(SM_CXPADDEDBWIDTH);
                    const int padY = GetSystemMetrics(SM_CYSIZEFRAME)
                                   + GetSystemMetrics(SM_CXPADDEDBWIDTH);
                    nccs->rgrc[0].left   += padX;
                    nccs->rgrc[0].right  -= padX;
                    nccs->rgrc[0].top    += padY;
                    nccs->rgrc[0].bottom -= padY;
                }
                *result = 0;
                return true;
            }
            return false;
        }
        if (msg->message == WM_NCHITTEST) {
            // Work entirely in PHYSICAL screen pixels — the same coordinate
            // space as WM_NCHITTEST's lParam. The previous version compared
            // against Qt's frameGeometry(), which is device-independent:
            // on any monitor scaled above 100% the mismatch pushed the
            // responsive border bands off the visible edge, so after a
            // minimize/restore cycle the window looked unresizable.
            RECT rc;
            if (GetWindowRect(msg->hwnd, &rc)) {
                const LONG x = GET_X_LPARAM(msg->lParam);
                const LONG y = GET_Y_LPARAM(msg->lParam);

                // A minimized or maximized window must not offer resize
                // borders; restoring to normal re-enables them below.
                if (!IsIconic(msg->hwnd) && !IsZoomed(msg->hwnd)) {
                    // 6 logical px of grab zone, scaled to this window's DPI.
                    UINT dpi = GetDpiForWindow(msg->hwnd);
                    if (dpi == 0) dpi = USER_DEFAULT_SCREEN_DPI;
                    const LONG border = MulDiv(6, int(dpi), USER_DEFAULT_SCREEN_DPI);

                    const bool onLeft   = x >= rc.left  && x < rc.left  + border;
                    const bool onRight  = x <  rc.right && x >= rc.right - border;
                    const bool onTop    = y >= rc.top   && y < rc.top   + border;
                    const bool onBottom = y <  rc.bottom && y >= rc.bottom - border;
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
        }
    }
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

// Restore WS_THICKFRAME on this frameless window. Qt's
// FramelessWindowHint strips that style, and WITHOUT it Windows silently
// ignores hit-test results like HTLEFT/HTBOTTOMRIGHT — dragging any edge
// did nothing in ANY window state, which users experienced as "cannot
// resize after minimize". With the style back (and WM_NCCALCSIZE handled
// above to keep the frame invisible), edge dragging enters the native
// sizing loop in every state, including right after restore.
void MainWindow::enableNativeResize() {
#ifdef Q_OS_WIN
    if (nativeResizeApplied_) return;
    nativeResizeApplied_ = true;
    HWND hwnd = reinterpret_cast<HWND>(winId());
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    SetWindowLongPtrW(hwnd, GWL_STYLE,
                      style | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                 SWP_NOACTIVATE | SWP_FRAMECHANGED);
#endif
}

void MainWindow::showEvent(QShowEvent* e) {
    QMainWindow::showEvent(e);
#ifdef Q_OS_WIN
    enableNativeResize();
#else
    Q_UNUSED(e)
#endif
}

// Keep the maximize button's icon/tooltip in sync with the real window
// state (also corrects state drift after Aero-Snap or Win+Up/Down).
void MainWindow::updateTitleBarState() {
    if (!titleMaxBtn_) return;
    QColor textColor = qApp->palette().color(QPalette::Text);
    if (isMaximized()) {
        titleMaxBtn_->setIcon(loadLucideIcon("copy", textColor, 14));   // restore glyph
        titleMaxBtn_->setToolTip("Restore down");
    } else {
        titleMaxBtn_->setIcon(loadLucideIcon("square", textColor, 14)); // maximize glyph
        titleMaxBtn_->setToolTip("Maximize");
    }
    titleMaxBtn_->setIconSize(QSize(14, 14));
}

void MainWindow::changeEvent(QEvent* e) {
    if (e && e->type() == QEvent::WindowStateChange)
        updateTitleBarState();
    QMainWindow::changeEvent(e);
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

    // Title text: "DocuSearch" (bold). No version subtitle — keep it minimal.
    titleBarText_ = new QLabel("DocuSearch", titleBar_);
    titleBarText_->setObjectName("titleBarText");
    h->addWidget(titleBarText_);

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
    titleCloseBtn_->setObjectName("closeBtn");
    titleCloseBtn_->setCursor(Qt::PointingHandCursor);
    titleCloseBtn_->setToolTip("Close");
    titleCloseBtn_->setFixedSize(32, 32);

    h->addWidget(titleMinBtn_);
    h->addWidget(titleMaxBtn_);
    h->addWidget(titleCloseBtn_);

    // Icon + tooltip must match the current maximize/restore state.
    updateTitleBarState();
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
    // [brand] | [nav action buttons] .......... [index badge]
    // NOTE: there is intentionally NO "Search" item — the search view IS
    // the home page (the big command field sits directly below this
    // strip), so a clickable Search tab was dead chrome. Every remaining
    // item is an ACTION that opens a dialog and returns to the search
    // view, so the strip keeps no selection state.
    sidebar_ = new QWidget(centerWidget);
    sidebar_->setObjectName("topMenuBar");
    sidebar_->setFixedHeight(40);
    auto* menuBarLay = new QHBoxLayout(sidebar_);
    menuBarLay->setContentsMargins(12, 0, 8, 0);
    menuBarLay->setSpacing(6);

    // Brand anchor: quiet wordmark only — the frameless title bar directly
    // above already carries the 28px app glyph, so a second icon here
    // would be duplicative.
    auto* brand = new QWidget(sidebar_);
    brand->setObjectName("menuBrand");
    auto* brandLay = new QHBoxLayout(brand);
    brandLay->setContentsMargins(0, 0, 2, 0);
    brandLay->setSpacing(0);
    auto* brandName = new QLabel("DocuSearch", brand);
    brandName->setObjectName("brandName");
    brandLay->addWidget(brandName);
    menuBarLay->addWidget(brand);

    auto* brandSep = new QFrame(sidebar_);
    brandSep->setObjectName("menuBrandSep");
    brandSep->setFrameShape(QFrame::VLine);
    brandSep->setFixedHeight(18);
    menuBarLay->addWidget(brandSep);

    // Action buttons rendered as a horizontal strip. We re-use
    // sidebarList_ as a QListWidget for state-tracking (so existing
    // onSidebarClicked logic keeps working) but display it as a flat
    // button row with no persistent selection.
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
    // topMenuList styling handled by global QSS in applyTheme().
    // Items are momentary actions: after each click we clear selection,
    // so nothing here should ever look like a selected "page".
    const QStringList navLabels = {
        "Duplicates", "Stats", "Settings", "Help", "About"
    };
    for (int i = 0; i < navLabels.size(); ++i) {
        auto* item = new QListWidgetItem(navLabels[i], sidebarList_);
        item->setData(Qt::UserRole, navLabels[i]);
        // Width must fit [icon][gap][longest label] plus QSS padding —
        // a tighter box silently clipped the leading glyph off some items.
        item->setSizeHint(QSize(112, 28));
        item->setTextAlignment(Qt::AlignCenter);
    }
    // No initial selection — the strip is action buttons, not tabs.
    menuBarLay->addWidget(sidebarList_, 1);

    // Compact indexed-status badge on the right side of the menu bar.
    // Minimal: just "X files" + thin progress bar. No "Indexed" label, no dot.
    auto* statusBadge = new QWidget(sidebar_);
    statusBadge->setObjectName("indexedStatus");
    auto* sbLay = new QHBoxLayout(statusBadge);
    sbLay->setContentsMargins(8, 4, 8, 4);
    sbLay->setSpacing(8);
    indexedInfoLbl_ = new QLabel("0 indexed", statusBadge);
    indexedInfoLbl_->setObjectName("indexedInfo");
    indexedInfoLbl_->setToolTip(
        "Number of files currently searchable in your offline index.\n"
        "The small bar appears only while files are being processed and "
        "disappears when the queue is done.");
    sbLay->addWidget(indexedInfoLbl_);

    indexedBar_ = new QProgressBar(statusBadge);
    indexedBar_->setObjectName("indexedBar");
    indexedBar_->setRange(0, 100);
    indexedBar_->setValue(0);
    indexedBar_->setTextVisible(false);
    indexedBar_->setFixedWidth(80);
    indexedBar_->setFixedHeight(4);
    sbLay->addWidget(indexedBar_);

    menuBarLay->addWidget(statusBadge);

    centerLay->addWidget(sidebar_);

    // Search bar
    searchBar_ = new SearchBar(centerWidget);
    centerLay->addWidget(searchBar_);

    // 3-way splitter: results | viewer | (metadata+tags)
    mainSplitter_ = new QSplitter(Qt::Horizontal, centerWidget);
    mainSplitter_->setObjectName("mainSplitter");
    mainSplitter_->setHandleWidth(6);  // themed in QSS: double-hairline gutter
    mainSplitter_->setChildrenCollapsible(false);
    centerLay->addWidget(mainSplitter_, 1);

    resultsPane_ = new ResultsPane(mainSplitter_);
    resultsPane_->setObjectName("resultsPane");
    resultsPane_->setMinimumWidth(280);
    resultsPane_->setMaximumWidth(420);
    mainSplitter_->addWidget(resultsPane_);

    // ── Center column: FilePreviewPane (TOP) + PreviewPane (BOTTOM) ──
    // Both panes are user-resizable via the splitter — no forced max height.
    // Default ratio is 1:1 so users see both the rendered file AND the
    // extracted text equally. They can drag the splitter handle to adjust.
    auto* centerColumn = new QWidget(mainSplitter_);
    auto* centerColLay = new QVBoxLayout(centerColumn);
    centerColLay->setContentsMargins(0, 0, 0, 0);
    centerColLay->setSpacing(1);  // hairline gap between the two panes

    filePreviewPane_ = new FilePreviewPane(centerColumn);
    filePreviewPane_->setObjectName("filePreviewPane");
    filePreviewPane_->setMinimumHeight(160);
    centerColLay->addWidget(filePreviewPane_, 1);

    previewPane_ = new PreviewPane(centerColumn);
    previewPane_->setObjectName("previewPane");
    previewPane_->setMinimumWidth(360);
    previewPane_->setMinimumHeight(120);
    centerColLay->addWidget(previewPane_, 1);

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
    rightSplitter_->setHandleWidth(6);
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

    // Left side: dot + Ready + indexed count + extraction progress.
    // Simplified — removed "Total size" and "Last indexed" (low-value info;
    // both are shown in the Stats panel if the user wants them).
    auto* left = new QWidget(sb);
    auto* lLay = new QHBoxLayout(left);
    lLay->setContentsMargins(0, 0, 0, 0);
    lLay->setSpacing(12);

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

    statusIndexedLbl_ = new QLabel("Indexed: 0", left);
    statusIndexedLbl_->setObjectName("statusInfo");
    lLay->addWidget(statusIndexedLbl_);

    // "Total size" and "Last indexed" labels are KEPT AS MEMBERS (other
    // code calls setText on them) but are NOT added to the layout — they
    // remain hidden. This avoids touching every call site.
    statusSizeLbl_ = new QLabel(left);
    statusSizeLbl_->setObjectName("statusInfo");
    statusSizeLbl_->setVisible(false);
    statusLastLbl_ = new QLabel(left);
    statusLastLbl_->setObjectName("statusInfo");
    statusLastLbl_->setVisible(false);

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
    // know at a glance whether Windows.Media.Ocr has language packs installed.
    ocrStatusWidget_ = new QWidget(sb);
    auto* ocrLay = new QHBoxLayout(ocrStatusWidget_);
    ocrLay->setContentsMargins(8, 0, 8, 0);
    ocrLay->setSpacing(6);
    ocrDotLbl_ = new QLabel(ocrStatusWidget_);
    ocrDotLbl_->setFixedSize(8, 8);
    ocrDotLbl_->setObjectName("ocrDot");
    ocrStatusLbl_ = new QLabel("OCR: ?", ocrStatusWidget_);
    ocrStatusLbl_->setObjectName("ocrStatus");
    ocrLay->addWidget(ocrDotLbl_);
    ocrLay->addWidget(ocrStatusLbl_);
    ocrStatusWidget_->setToolTip(
        "Optical character recognition (OCR).\n"
        "Ready — text in images and scanned PDFs is searchable.\n"
        "Setup required — no Windows OCR language pack installed; click for steps.");
    sb->addPermanentWidget(ocrStatusWidget_);

    // Semantic search toggle — custom slider pill (matches Pastel Pop design).
    // Layout:  [sparkles-icon] AI  [====switch====]  ON/OFF
    // Disabled by default — enabled after BGE service becomes ready.
    aiControlWidget_ = new QWidget(sb);
    aiControlWidget_->setObjectName("aiControl");
    auto* aiLay = new QHBoxLayout(aiControlWidget_);
    aiLay->setContentsMargins(8, 0, 8, 0);
    aiLay->setSpacing(7);
    aiLay->setAlignment(Qt::AlignVCenter);

    aiIconLbl_ = new QLabel(aiControlWidget_);
    aiIconLbl_->setObjectName("aiIcon");
    aiIconLbl_->setFixedSize(14, 14);
    aiLay->addWidget(aiIconLbl_);

    auto* aiLabel = new QLabel("AI", aiControlWidget_);
    aiLabel->setObjectName("aiLabel");
    // The pink candy accent for the AI glyph is fixed across all 3 themes.
    // Set via QSS (#aiLabel) in applyTheme.
    aiLay->addWidget(aiLabel);

    aiSwitch_ = new SwitchControl(aiControlWidget_);
    aiSwitch_->setObjectName("aiSwitch");
    aiSwitch_->setChecked(false);
    aiSwitch_->setEnabled(false);
    aiSwitch_->setToolTip(
        "AI semantic search (BGE-small-en-v1.5, runs fully offline).\n"
        "When on, results are re-ranked by meaning in addition to keywords,\n"
        "and semantically close documents appear even without keyword hits.");
    aiLay->addWidget(aiSwitch_);

    aiStateLbl_ = new QLabel("OFF", aiControlWidget_);
    aiStateLbl_->setObjectName("aiState");
    // Color is updated dynamically in onSemanticToggled based on Theme::active().
    aiStateLbl_->setStyleSheet("background:transparent; color:#8d93b2; font-weight:800; font-size:11px; min-width:24px;");
    aiLay->addWidget(aiStateLbl_);

    aiControlWidget_->setToolTip(aiSwitch_->toolTip());
    sb->addPermanentWidget(aiControlWidget_);

    // Theme toggle button — Daylight ⇄ Midnight (palette icon + label).
    themeToggleBtn_ = new QPushButton(sb);
    themeToggleBtn_->setObjectName("themeToggleBtn");
    themeToggleBtn_->setText("Light");
    themeToggleBtn_->setToolTip("Switch between light and dark appearance");
    themeToggleBtn_->setCursor(Qt::PointingHandCursor);
    themeToggleBtn_->setFixedHeight(29);
    connect(themeToggleBtn_, &QPushButton::clicked, this, &MainWindow::onToggleTheme);
    sb->addPermanentWidget(themeToggleBtn_);

    // Open Location — small secondary button with folder-open lucide icon.
    openLocationBtn_ = new QPushButton(sb);
    openLocationBtn_->setObjectName("openLocationBtn");
    openLocationBtn_->setCursor(Qt::PointingHandCursor);
    openLocationBtn_->setText("Open");
    openLocationBtn_->setToolTip("Show the selected file in File Explorer");
    openLocationBtn_->setFixedHeight(29);
    sb->addPermanentWidget(openLocationBtn_);
}

void MainWindow::applyTheme() {
    // ════════════════════════════════════════════════════════════════
    // Fluent Design — 2 swappable palettes (light + dark).
    //   0 = Fluent Light   — warm mica #f2f1ee + accent #0067c0 (Win11 blue)
    //   1 = Fluent Dark    — dark mica #1c1c1c + accent #4cc2ff (Win11 cyan)
    //
    // Based on docs/ui-design-reference.html (Fluent Design tokens).
    // Replaces the previous 4-palette Pastel Pop system — that design
    // felt too "kid-friendly" for a $9.99 commercial product.
    //
    // The new palette uses the same field names as the previous Pastel
    // Pop (bg / surface / primary / etc.) so the existing QSS structure
    // continues to work — only the color values changed.
    // ════════════════════════════════════════════════════════════════

    // ════════════════════════════════════════════════════════════════
    // Fluent Design — 2 swappable palettes (light + dark).
    //   0 = Fluent Light   — warm mica #f2f1ee + accent #0067c0 (Win11 blue)
    //   1 = Fluent Dark    — dark mica #1c1c1c + accent #4cc2ff (Win11 cyan)
    //
    // Based on docs/ui-design-reference.html (Fluent Design tokens).
    // Replaces the previous 4-palette Pastel Pop system — that design
    // felt too "kid-friendly" for a $9.99 commercial product.
    //
    // The new palette uses the same field names as the previous Pastel
    // Pop (bg / surface / primary / etc.) so the existing QSS structure
    // continues to work — only the color values changed.
    // ════════════════════════════════════════════════════════════════

    // Fluent accent-styled candy colors for file-type icons. Slightly
    // different per palette for readability on the contrasting surfaces.
    QString cPdf, cDocx, cXlsx, cMd, cTxt;
    QString success, warn, pink, orange, sky, violet;
    QString cPdfBg, cDocxBg, cXlsxBg, cMdBg, cTxtBg;
    QString tooltipBg, tooltipText;

    // Fluent palette tokens (same names as before so QSS works unchanged).
    QString bg, surface, surface2, surface3, field, border, border2, hover, hoverSoft, text, muted;
    QString primary, primaryStrong, primarySoft, primaryBorder, primaryGlow;
    QString shadow, elevation1, elevation2, btnText;
    QString themeLabel;
    bool isDark = false;

    switch (pastelTheme_) {
        case 1: // Midnight (pro dark)
            isDark = true;
            // ── DocuSearch Pro · Midnight — 2026 design refresh ──
            // Deep blue-slate neutrals (never pure black), hairline borders,
            // and a luminous indigo accent. Accent buttons use DARK ink on a
            // bright fill (the Linear/GitHub-dark pattern) for AA contrast.
            bg        = "#14181f";   // window canvas
            surface   = "#1b212b";   // panels / cards
            surface2  = "#232b38";   // chrome: title bar, search bar, status bar
            surface3  = "#2b3547";   // chrome deep end of gradients
            field     = "#161c25";   // inputs sit recessed below panels
            border    = "#26303f";   // hairline ≈ rgba(255,255,255,.06)
            border2   = "#3a465a";   // stronger ≈ rgba(255,255,255,.12)
            hover     = "#232c3b";
            hoverSoft = "#1f2734";
            text      = "#e8edf5";
            muted     = "#97a3b8";
            primary        = "#4d8df6";   // luminous indigo-blue (fills)
            primaryStrong  = "#7cb0ff";   // emphasis text / hover lift
            primarySoft    = "#223259";   // selected backgrounds
            primaryBorder  = "#45639e";
            primaryGlow    = "#79acff";
            btnText   = "#ffffff";         // white on luminous accent fills
            shadow     = "#000000aa";
            elevation1 = "#222b3a";
            elevation2 = "#293349";
            tooltipBg  = "#0d1118";
            tooltipText = "#e8edf5";
            // File-type accent colors — kept punchy on dark surfaces.
            cPdf="#ff99a4"; cDocx="#67d4ff"; cXlsx="#6ccb9f"; cMd="#b18aff"; cTxt="#7ad7f0";
            cPdfBg="#3d1f24"; cDocxBg="#1f2d3a"; cXlsxBg="#1f3d2a"; cMdBg="#2d1f3d"; cTxtBg="#1f3d40";
            success="#3ecf8e"; warn="#f5bf4f"; pink="#f472b6"; orange="#fb923c"; sky="#66b1ff"; violet="#b18aff";
            themeLabel = "Dark";
            break;
        default: // Daylight (pro light)
            // ── DocuSearch Pro · Daylight — 2026 design refresh ──
            // Cool neutral canvas (no more beige cast), crisp white cards,
            // slate text ramp, and a confident indigo-blue accent.
            bg        = "#f6f7f9";
            surface   = "#ffffff";
            surface2  = "#eff1f5";   // chrome bars read as "device shell"
            surface3  = "#e7eaf0";
            field     = "#fafbfd";
            border    = "#e4e7ee";   // hairline
            border2   = "#d2d9e4";
            hover     = "#eef1f6";
            hoverSoft = "#f2f4f8";
            text      = "#151f2c";
            muted     = "#667188";
            primary        = "#2563eb";
            primaryStrong  = "#1d4ed8";
            primarySoft    = "#e9f0fd";
            primaryBorder  = "#bcd0f6";
            primaryGlow    = "#3b82f6";
            btnText   = "#ffffff";
            shadow     = "#00000026";
            elevation1 = "#ffffff";
            elevation2 = "#fafbfd";
            tooltipBg  = "#101828";
            tooltipText = "#ffffff";
            cPdf="#c42b1c"; cDocx="#0067c0"; cXlsx="#0f7b4a"; cMd="#8a5b00"; cTxt="#005a9e";
            cPdfBg="#fde0dc"; cDocxBg="#dbeaf6"; cXlsxBg="#d6ecd9"; cMdBg="#f4e6cc"; cTxtBg="#d6e8f4";
            success="#059669"; warn="#b45309"; pink="#db2777"; orange="#ea580c"; sky="#0369a1"; violet="#7c3aed";
            themeLabel = "Light";
            break;
    }

    // Update theme toggle button label
    if (themeToggleBtn_) themeToggleBtn_->setText(themeLabel);

    // Publish the active pastel palette so delegates and custom widgets
    // (ResultItemDelegate, SwitchControl) can read the exact tokens
    // instead of re-deriving from QPalette::Highlight.
    Theme::PastelPalette palActive;
    palActive.bg = bg; palActive.surface = surface; palActive.surface2 = surface2;
    palActive.field = field; palActive.border = border; palActive.hover = hover;
    palActive.text = text; palActive.muted = muted;
    palActive.primary = primary; palActive.primaryStrong = primaryStrong;
    palActive.primarySoft = primarySoft; palActive.primaryBorder = primaryBorder;
    palActive.themeLabel = themeLabel; palActive.index = pastelTheme_;
    Theme::setActive(palActive);

    // ── QPalette ──────────────────────────────────────────────
    QPalette pal;
    pal.setColor(QPalette::Window,        QColor(bg));
    pal.setColor(QPalette::Base,          QColor(surface));
    pal.setColor(QPalette::AlternateBase, QColor(surface2));
    pal.setColor(QPalette::WindowText,    QColor(text));
    pal.setColor(QPalette::Text,          QColor(text));
    pal.setColor(QPalette::ButtonText,    QColor(text));
    pal.setColor(QPalette::Button,        QColor(surface));
    pal.setColor(QPalette::Highlight,     QColor(primary));
    pal.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    pal.setColor(QPalette::ToolTipBase,   QColor(tooltipBg));
    pal.setColor(QPalette::ToolTipText,  QColor(tooltipText));
    pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor(muted));
    pal.setColor(QPalette::Disabled, QPalette::Text,     QColor(muted));
    QApplication::setPalette(pal);

    // ── QSS with @token@ substitution ────────────────────────
    // DocuSearch v1.5 "Modern Professional" master stylesheet.
    // The sheet lives in resources/themes/base.qss (compiled in as
    // :/themes/base.qss) instead of a C++ raw string: MSVC aborts with
    // error C2026 "string too big" once one string literal exceeds
    // 65,535 bytes — exactly what this stylesheet did on CI. Data
    // files carry no such ceiling and stay editable without recompiles.
    QString s;
    QFile qssRes(QStringLiteral(":/themes/base.qss"));
    if (qssRes.open(QIODevice::ReadOnly | QIODevice::Text)) {
        s = QString::fromUtf8(qssRes.readAll());
    } else {
        // Fallback: minimal neutral sheet so the app never ships unstyled.
        DS_WARN("Theme", QString("base.qss unavailable (%1) - using fallback")
                             .arg(qssRes.errorString()));
        s = QStringLiteral(
            "QWidget { background:#f6f7f9; color:#151f2c;"
            " font-family:'Segoe UI'; font-size:13px; }"
            "QPushButton { background:#2563eb; color:white;"
            " border-radius:8px; padding:7px 15px; font-weight:600; }"
            "QLineEdit { background:white; border:1px solid #d2d9e4;"
            " border-radius:10px; padding:8px 12px; }");
    }

    // Token substitution
    struct P { const char* k; const QString& v; };
    const P map[] = {
        {"@bg@",bg},{"@surface@",surface},{"@surface2@",surface2},{"@surface3@",surface3},
        {"@field@",field},{"@border@",border},{"@border2@",border2},
        {"@hover@",hover},{"@hoverSoft@",hoverSoft},
        {"@text@",text},{"@muted@",muted},{"@btnText@",btnText},
        {"@primary@",primary},{"@primaryStrong@",primaryStrong},
        {"@primarySoft@",primarySoft},{"@primaryBorder@",primaryBorder},
        {"@primaryGlow@",primaryGlow},
        {"@tooltipBg@",tooltipBg},{"@tooltipText@",tooltipText},
        {"@success@",success},{"@warn@",warn},{"@pink@",pink},
        {"@orange@",orange},{"@sky@",sky},{"@violet@",violet},
        {"@elevation1@",elevation1},{"@elevation2@",elevation2},
    };
    for (const auto& p : map) s.replace(QLatin1String(p.k), p.v);

    qApp->setStyleSheet(s);

    QTimer::singleShot(0, this, [this]() {
        refreshAllIcons();
    });
}

void MainWindow::refreshAllIcons() {
    QColor textColor = qApp->palette().color(QPalette::Text);
    QColor whiteText("#ffffff");

    // ---- Menu strip icons (aligned with the 5 action items) ----
    // Order must match navLabels: Duplicates, Stats, Settings, Help, About.
    const QStringList navIcons = {
        "duplicate", "bar-chart-3", "settings", "help-circle", "info"
    };
    const QStringList navColors = {
        "#7c3aed", "#059669", "#ea580c", "#2563eb", "#0891b2"
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
    openLocationBtn_->setIcon(loadLucideIcon("folder-open", textColor, 14));
    openLocationBtn_->setIconSize(QSize(14, 14));

    // ---- Theme toggle button icon (palette) ----
    if (themeToggleBtn_) {
        themeToggleBtn_->setIcon(loadLucideIcon("palette", textColor, 14));
        themeToggleBtn_->setIconSize(QSize(14, 14));
    }

    // ---- AI control icon (pink sparkles, fixed candy accent) ----
    if (aiIconLbl_) {
        aiIconLbl_->setPixmap(loadLucidePixmap("sparkles", QColor("#e85d97"), 14, devicePixelRatio()));
    }

    // ---- Sub-pane icon refresh ----
    if (searchBar_)     searchBar_->refreshIcons();
    if (resultsPane_)   resultsPane_->refreshIcons();
    if (previewPane_)   previewPane_->refreshIcons();
    if (metadataPane_)  metadataPane_->refreshIcons();
    if (tagsNotesPane_) tagsNotesPane_->refreshIcons();

    // ---- PDF / image preview toolbar zoom glyphs ----
    if (filePreviewPane_) filePreviewPane_->refreshIcons();
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
                // Visible AI contribution indicator. Show three cases:
                //  1. Pure keyword match (semanticScore = 0)  → no badge
                //  2. Hybrid match (both > 0)                 → "AI + keyword"
                //  3. Pure semantic match (keywordScore = 0)   → "AI only"
                // The user said "AI has no role in search. It is acting
                // like normal keyword search" — this badge makes the AI
                // contribution visible so they can SEE when AI is working.
                if (hr.semanticScore > 0.01f && hr.keywordScore > 0.01f) {
                    h.snippet = QString(
                        "<b>[AI + keyword]</b> keyword: %1%  •  semantic: %2%")
                        .arg(int(hr.keywordScore * 100))
                        .arg(int(hr.semanticScore * 100));
                } else if (hr.semanticScore > 0.01f) {
                    h.snippet = QString(
                        "<b>[AI match]</b> semantic similarity: %1%  "
                        "(no keyword match — AI found this document)")
                        .arg(int(hr.semanticScore * 100));
                } else {
                    h.snippet = QString("[keyword match] relevance: %1%")
                        .arg(int(hr.keywordScore * 100));
                }
                // Phase 2 BUGFIX: previously the line below OVERWROTE
                //   the [AI + keyword] / [AI match] badge with the original
                //   keyword snippet, making AI contributions invisible to the
                //   user. Now we PREPEND the AI badge to the original snippet
                //   so the user sees BOTH the badge AND the keyword context.
                for (const auto& orig : hits) {
                    if (orig.fileId == hr.fileId) {
                        h.size          = orig.size;
                        h.modifiedDate  = orig.modifiedDate;
                        h.isFavorite    = orig.isFavorite;
                        // Phase 2: keep the AI badge, append original snippet if any.
                        if (!orig.snippet.isEmpty()) {
                            h.snippet = h.snippet + "<br>" + orig.snippet;
                        }
                        break;
                    }
                }
                merged.append(h);
            }
            resultsPane_->setResults(merged);
            // Phase 2: surface AI debug info so user can SEE that AI ran.
            //   Status bar now shows: result count + how many had AI
            //   contribution + total time. This directly addresses the
            //   user complaint "AI has no role in search" — every search
            //   now reports its AI contribution in the status bar.
            int aiContribCount = 0;
            int aiOnlyCount = 0;
            for (const auto& hr : hybridResults) {
                if (hr.semanticScore > 0.01f) ++aiContribCount;
                if (hr.semanticScore > 0.01f && hr.keywordScore < 0.01f) ++aiOnlyCount;
            }
            statusBar()->showMessage(
                QString("%1 result%2 · AI refined %3 · %4 ms")
                    .arg(merged.size())
                    .arg(merged.size() == 1 ? "" : "s")
                    .arg(aiContribCount)
                    .arg(t.elapsed()));
            // Persistent summary pill directly above the results list —
            // the status-bar toast disappears, this stays until the next
            // search so users can actually SEE what AI did.
            if (aiContribCount > 0) {
                resultsPane_->setAiSummary(QString(
                    "<b>Semantic ranking active</b> — AI refined %1 of %2 "
                    "results%3")
                    .arg(aiContribCount)
                    .arg(merged.size())
                    .arg(aiOnlyCount > 0
                        ? QString(", including %1 found by semantic match "
                          "alone (no keyword overlap)")
                          .arg(aiOnlyCount)
                        : QString()));
            } else if (bgeService_ && bgeService_->isReady()) {
                // Zero semantic contribution: explain exactly why, with the
                // real numbers from the scan, instead of a vague complaint.
                const auto stats = bgeService_->getStats();
                const float bestSim = bgeService_->lastBestSimilarity();
                const int thrPct = hybridSearch_
                    ? qRound(hybridSearch_->threshold() * 100) : 45;
                if (stats.total == 0) {
                    resultsPane_->setAiSummary(QString(
                        "<b>Semantic index is empty.</b> AI ranking starts "
                        "working once documents are extracted — each indexed "
                        "document builds an embedding on its own."));
                } else if (bestSim < 0.0f) {
                    // Embeddings exist but nothing was comparable this pass
                    // (e.g. the chunk index is still being built in the
                    // background). Say that instead of the misleading
                    // "still queued" wording.
                    resultsPane_->setAiSummary(QString(
                        "<b>AI index warming up.</b> %1 document%2 embedded; "
                        "the chunk index is still building in the background — "
                        "results below are keyword-only for now.")
                        .arg(stats.total)
                        .arg(stats.total == 1 ? " is" : "s are"));
                } else {
                    resultsPane_->setAiSummary(QString(
                        "<b>No semantic match above the %1% similarity bar.</b> "
                        "Closest of %2 embedded documents scored %3%. Lower "
                        "the AI threshold in Settings → Search to admit "
                        "weaker semantic matches.")
                        .arg(thrPct)
                        .arg(stats.total)
                        .arg(qRound(bestSim * 100)));
                }
            } else {
                resultsPane_->setAiSummary(QString(
                    "<b>Semantic ranking unavailable</b> — the AI model is "
                    "not loaded, so results are keyword-only."));
            }
        } else {
            // Keyword-only search (existing behavior).
            resultsPane_->setResults(hits);
            resultsPane_->setAiSummary(QString());
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

    // The extracted-text pane is only useful next to a RENDERED preview:
    // for PDFs the top pane shows page images while this pane shows the
    // matching text. Every other type (txt/docx/xlsx/pptx/images…) already
    // displays its full content in the top preview, so showing the same
    // text twice was redundant — hide the bottom pane for those types.
    try {
        const bool isPdf = path.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive);
        previewPane_->setVisible(isPdf || path.isEmpty());
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

    // ONLY index file types the user cares about — documents and common
    // images. Everything else (txt/md/csv/rtf/html/json/logs, exotic image
    // formats, archives…) stays out of the index entirely.
    const QSet<QString> supportedExts = {
        "pdf", "doc", "docx", "xls", "xlsx", "xlsm",
        "ppt", "pptx", "jpg", "jpeg", "png",
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
    // Momentary-action strip: run the action, then clear selection so
    // the search view (the only page) remains the resting state.
    sidebarList_->setCurrentRow(-1);
    if (page == "Settings") {
        onOpenSettings();
    } else if (page == "About") {
        onAbout();
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
    } else if (page == "Duplicates") {
        onDetectDuplicates();
    }
    // Strip shows actions only — Saved/Tags/Notes are reachable directly:
    // Saved Searches via the search bar dropdown, Tags/Notes in the right
    // panel when a file is selected.
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


// ============================================================
// Extraction — timer-based, runs on the main thread but yields between
// files so the UI stays responsive. Reverted from the worker-thread
// approach (which had Poppler/minizip thread-safety crashes).
// ============================================================
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

        // Retire plain-text-ish types the user excluded from extraction.
        // Leaving them as 'metadata_only' made them look like pending work
        // forever; 'skipped' is honest and keeps them filename-searchable.
        sqlite3_exec(raw,
            "UPDATE Files SET indexing_status='skipped' "
            "WHERE indexing_status='metadata_only' "
            "AND lower(extension) IN ('txt','csv','md','rtf','log');",
            nullptr, nullptr, nullptr);

        sqlite3_stmt* s = nullptr;
        const char* sql =
            "SELECT id, path, extension FROM Files "
            "WHERE indexing_status = 'metadata_only' "
            "AND extension IN ("
            "'pdf','doc','docx',"
            "'xls','xlsx','xlsm',"
            "'ppt','pptx') "
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
        try {
            QString statusMsg = getExtractionStatusString();
            statusBar()->showMessage(statusMsg, 5000);
        } catch (...) {
            statusBar()->showMessage("All files extracted.", 3000);
        }
        return;
    }

    contentExtractionRunning_ = true;
    extractCancelFlag_.store(false);
    if (searchBar_) searchBar_->setExtracting(true);  // Phase 1.4: button shows "Cancel"
    const int total = todo.size();
    // Restore the 30-file batch limit. Processing all pending files in one
    // go was unstable (large batches + 10ms timer interval → memory pressure
    // + UI event starvation → crashes). The 30-file batch + 200ms interval
    // was the original stable behavior. User clicks Extract again to
    // continue with the next 30.
    const int maxFilesThisSession = qMin(total, 30);
    statusBar()->showMessage(
        QString("Extracting %1 of %2 files... (click Extract again to cancel)")
            .arg(maxFilesThisSession).arg(total));

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

    // 200ms between files — gives the UI time to process events between
    // heavy extractions. The 10ms interval was too aggressive and caused
    // event starvation on large batches.
    auto* timer = new QTimer(this);
    timer->setInterval(200);
    timer->setSingleShot(false);

    connect(timer, &QTimer::timeout, this, [this, timer, total, maxFilesThisSession, state]() {
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

        if (state->idx >= total || state->idx >= maxFilesThisSession) {
            timer->stop();
            timer->deleteLater();
            contentExtractionRunning_ = false;
            if (searchBar_) searchBar_->setExtracting(false);  // Phase 1.4
            if (extractionProgressBar_) extractionProgressBar_->setVisible(false);
            updateIndexStats();
            refreshPreviewForSelectedFile();
            if (state->idx >= total) {
                statusBar()->showMessage(
                    QString("Extraction complete: %1 succeeded, %2 failed (out of %3).")
                        .arg(state->done).arg(state->failed).arg(total), 8000);

                // Auto-queue AI (BGE) embedding generation for all newly-extracted
                // files. Previously this was commented out during extraction
                // (ONNX inference on the main thread was crashing). Now we
                // trigger the BACKGROUND batch path AFTER extraction completes,
                // which is safe: BgeService::embedDocumentsBatch runs on a
                // worker thread and emits embeddingProgress/embeddingFinished.
                if (bgeService_ && bgeService_->isReady() && semanticEnabled_) {
                    // Re-read the extracted text for every just-indexed file
                    // from the FTS5 table, then queue the batch on the
                    // background BGE worker thread. Prepare the statement
                    // ONCE outside the loop (avoids N prepare/finalize pairs).
                    QVector<int> fileIds;
                    QStringList texts;
                    sqlite3_stmt* sel = nullptr;
                    if (raw) {
                        sqlite3_prepare_v2(raw,
                            "SELECT content FROM SearchIndex WHERE file_id=?1 LIMIT 1;",
                            -1, &sel, nullptr);
                    }
                    if (sel) {
                        for (const auto& item : state->todo) {
                            if (item.fileId <= 0) continue;
                            sqlite3_bind_int64(sel, 1, item.fileId);
                            if (sqlite3_step(sel) == SQLITE_ROW) {
                                const unsigned char* c = sqlite3_column_text(sel, 0);
                                if (c && c[0]) {
                                    fileIds.append(static_cast<int>(item.fileId));
                                    texts.append(QString::fromUtf8(
                                        reinterpret_cast<const char*>(c)));
                                }
                            }
                            sqlite3_reset(sel);
                            sqlite3_clear_bindings(sel);
                        }
                        sqlite3_finalize(sel);
                    }
                    if (!fileIds.isEmpty()) {
                        statusBar()->showMessage(
                            QString("AI: generating embeddings for %1 files...")
                                .arg(fileIds.size()), 5000);
                        bgeService_->embedDocumentsBatch(fileIds, texts);
                    }
                }
            } else {
                statusBar()->showMessage(
                    QString("Extracted %1 of %2 files. Click Extract again to continue.")
                        .arg(state->done + state->failed).arg(total), 8000);
            }
            return;
        }

        const auto& item = state->todo[state->idx];
        QFileInfo fi(item.path);
        statusBar()->showMessage(
            QString("Extracting: %1 (%2/%3)...")
                .arg(fi.fileName()).arg(state->idx + 1).arg(maxFilesThisSession));
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
                // ── BEFORE log: filename + type + size ──────────────────
                // If the app crashes during extraction, this is the LAST
                // line in the log — it tells us exactly which file killed it.
                DS_INFO("Extract",
                    QString("START file %1/%2: %3 [%4, %5 bytes]")
                        .arg(state->idx + 1).arg(maxFilesThisSession)
                        .arg(item.path).arg(item.ext).arg(fi.size()));

                // CRITICAL: Run extractByExtension() on a thread with a LARGE
                // STACK (16MB). The main thread's 1MB stack overflows on
                // deeply-nested PDFs — Poppler's recursive parser blows the
                // stack and raises STACK_OVERFLOW (0xC00000FD).
                //
                // The stack size is set on the global QThreadPool in main.cpp
                // (16MB). QtConcurrent::run uses that pool.
                //
                // We also install the SEH translator on this worker thread
                // so any SEH exception (including stack overflow that exceeds
                // even 16MB) is caught by catch(...) instead of crashing.
                //
                // The call is SYNCHRONOUS (we block on .waitForFinished())
                // because the timer-based extraction loop expects the result
                // inline. But instead of waitForFinished() (which blocks the
                // main thread → UI freeze if user clicks Duplicates/etc.),
                // we use a non-blocking wait with processEvents() so the UI
                // stays responsive while the extraction runs on the pool thread.
                QFuture<ExtractionResult> future = QtConcurrent::run([&registry, &item]() -> ExtractionResult {
                    installSehTranslator();
                    return registry.extractByExtension(item.path, item.ext);
                });
                // Non-blocking wait — process UI events every 50ms so clicks,
                // paints, and other interactions work during extraction.
                while (!future.isFinished()) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                    QThread::msleep(10);
                }
                auto result = future.result();
                extractedText = result.text;
                source = result.source.isEmpty() ? "native" : result.source;
                ok = true;

                // ── AFTER log: success + text length ───────────────────
                DS_INFO("Extract",
                    QString("DONE  file %1/%2: %3 — %4 chars, source=%5%6")
                        .arg(state->idx + 1).arg(maxFilesThisSession)
                        .arg(item.path).arg(extractedText.size()).arg(source)
                        .arg(result.needsOcr ? " (needs OCR)" : ""));

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
                DS_WARN("Extract", QString("Failed (exception): %1 — %2").arg(item.path).arg(e.what()));
                ok = false;
            } catch (...) {
                // Caught SEH-translated or unknown exception. If this was
                // an SEH (access violation), the sehTranslator already
                // logged the exception code. Log the file context here.
                DS_ERROR("Extract", QString("Failed (unknown/SEH): %1").arg(item.path));
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

                // NOTE: BGE embedding generation during extraction is DISABLED.
                // Calling bgeService_->embedDocument() here runs ONNX inference
                // on the main thread — that was the crash source. The catch(...)
                // below catches C++ exceptions but NOT SEH exceptions, and ONNX
                // Runtime can raise SEH (access violations) on certain inputs.
                //
                // Users can generate embeddings on-demand via the
                // "Generate AI Embeddings" button in Settings → AI Search,
                // which runs in a proper background thread.
                //
                // if (bgeService_ && bgeService_->isReady()) {
                //     try {
                //         bgeService_->embedDocument(item.fileId, extractedText);
                //     } catch (...) {}
                // }

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

        // The 200ms timer interval already provides CPU relief between
        // extractions. The previous adaptive CPU throttle (GetSystemTimes +
        // Sleep(100) on the main thread) was removed — it blocked the UI
        // for an extra 100ms per file and wasn't necessary with the 30-file
        // batch limit.

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

    // CRITICAL: Only scan the folders the user explicitly added via Settings
    // → Indexing → Indexed Drives. The old code derived folders from existing
    // DB entries (SELECT path FROM Files), which meant:
    //   - If you ever had a file from D:\ in the DB, it would re-scan D:\
    //   - If you removed a folder from Settings, it would STILL scan it
    //   - Files from other sources (file watcher, manual add) would cause
    //     their parent folders to be scanned on next startup
    // This was the "scanning not only selected folders but other source too" bug.
    if (settings_.indexedDrives.isEmpty()) {
        autoScanRunning_ = false;
        return;
    }

    autoScanRunning_ = true;
    statusBar()->showMessage("Auto-scanning indexed folders...");

    const QStringList folderList = settings_.indexedDrives;
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

    // Use dynamic properties + QSS selectors instead of inline setStyleSheet.
    auto& ocr = DocuSearch::WindowsOcrEngine::instance();
    if (ocr.isAvailable()) {
        ocrDotLbl_->setProperty("status", "ready");
        ocrStatusLbl_->setProperty("status", "ready");
        ocrStatusLbl_->setText("OCR: Ready");
    } else {
        ocrDotLbl_->setProperty("status", "setup");
        ocrStatusLbl_->setProperty("status", "setup");
        // "Click to setup" is softer than "Required" — and accurate,
        // since clicking the chip surfaces the language-pack instructions.
        ocrStatusLbl_->setText("OCR: Click to setup");
    }
    // Force QSS re-evaluation.
    ocrDotLbl_->style()->unpolish(ocrDotLbl_);
    ocrDotLbl_->style()->polish(ocrDotLbl_);
    ocrStatusLbl_->style()->unpolish(ocrStatusLbl_);
    ocrStatusLbl_->style()->polish(ocrStatusLbl_);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* e) {
    // Click on the OCR status indicator → show status info.
    if (obj == ocrStatusWidget_ && e->type() == QEvent::MouseButtonPress) {
        auto& ocr = DocuSearch::WindowsOcrEngine::instance();

        QString msg;
        if (ocr.isAvailable()) {
            msg = "OCR engine: Windows.Media.Ocr (built into Windows 10/11)\n\n"
                  "• Runs entirely on your machine — no internet needed.\n"
                  "• Auto-detects document language from your Windows profile.\n"
                  "• Supports every OCR language pack you have installed.\n\n"
                  "Click an image or scanned PDF's OCR button to extract text.";
        } else {
            msg = "OCR is not set up.\n\n"
                  "DocuSearch uses Windows.Media.Ocr, which is built into\n"
                  "Windows 10/11. To enable OCR, install at least one OCR\n"
                  "language pack:\n\n"
                  "  Settings > Time & Language > Language >\n"
                  "    Add a language > Optical character recognition\n\n"
                  "Then restart DocuSearch. No DLLs to download, no scripts\n"
                  "to run — Windows itself provides the OCR engine.";
        }
        QMessageBox::information(this, "OCR Status", msg);
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

        // Wire up the AI slider switch.
        if (aiSwitch_) {
            connect(aiSwitch_, &SwitchControl::toggled,
                    this, &MainWindow::onSemanticToggled);
        }

        // Check model in multiple possible locations.
        // The model may be at:
        //   1. <exe_dir>/models/bge-small-en-v1.5/model.onnx  (standard)
        //   2. <exe_dir>/bge-small-en-v1.5/model.onnx         (no models/ subfolder)
        //   3. %APPDATA%/DocuSearch/models/bge-small-en-v1.5/model.onnx
        //   4. %APPDATA%/DocuSearch/bge-small-en-v1.5/model.onnx
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        const QStringList modelCandidates = {
            exeDir + "/models/bge-small-en-v1.5/model.onnx",
            exeDir + "/bge-small-en-v1.5/model.onnx",
            appData + "/models/bge-small-en-v1.5/model.onnx",
            appData + "/bge-small-en-v1.5/model.onnx",
        };
        QString modelPath;
        for (const auto& candidate : modelCandidates) {
            DS_INFO("BGE", "Checking model path: " + candidate +
                    (QFileInfo::exists(candidate) ? " — FOUND" : " — not found"));
            if (QFileInfo::exists(candidate)) {
                modelPath = candidate;
                break;
            }
        }
        if (modelPath.isEmpty()) {
            DS_WARN("BGE", "Model not found in any of the checked paths. "
                    "Semantic search will be unavailable.");
        }
        const QString dbPath = Config::instance().dbPath();

        bgeService_ = std::make_unique<BgeService>(this);

        connect(bgeService_.get(), &BgeService::ready,
                this, &MainWindow::onBgeReady);
        connect(bgeService_.get(), &BgeService::embeddingProgress,
                this, &MainWindow::onBgeEmbeddingProgress);
        connect(bgeService_.get(), &BgeService::embeddingFinished,
                this, &MainWindow::onBgeEmbeddingFinished);

        QtConcurrent::run([this, dbPath, modelPath]() {
            const bool ok = bgeService_->initialize(dbPath, modelPath);
            if (!ok) {
                // initialize() only emits ready() on success — surface the
                // failure so the UI can explain WHY AI is unavailable
                // instead of leaving a forever-disabled silent switch.
                QMetaObject::invokeMethod(this, "onBgeFailed",
                                          Qt::QueuedConnection);
            }
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
        if (aiSwitch_) aiSwitch_->setCheckedNoAnim(false);
        if (aiStateLbl_) aiStateLbl_->setText("OFF");
        QMessageBox::information(this, "AI Search",
            "The AI model is not available.\n\n"
            "Model files ship with DocuSearch and are expected at:\n"
            "  models/bge-small-en-v1.5/model.onnx (+ vocab.txt)\n"
            "next to the app or under %APPDATA%/DocuSearch/models.\n\n"
            "Reinstall or restore those files, then restart the app.");
        return;
    }
    semanticEnabled_ = checked;
    if (hybridSearch_) hybridSearch_->setSemanticEnabled(checked);
    setAiChip(checked ? "ON" : "OFF", checked);

    if (checked && bgeService_ && bgeService_->isReady()) {
        // Queue any indexed-but-unembedded files on the background BGE
        // worker (shared with the onBgeReady path; batches chain from
        // onBgeEmbeddingFinished until the backlog is drained).
        ensureEmbeddingsBackfill();
        if (!aiBackfillRunning_) {
            const auto stats = bgeService_->getStats();
            statusBar()->showMessage(
                stats.total > 0
                    ? QString("AI search on — %1 document%2 ready for "
                              "semantic ranking.")
                          .arg(stats.total)
                          .arg(stats.total == 1 ? "" : "s")
                    : "AI search on. Extract a document to start building "
                      "the semantic index.",
                4000);
        }
    } else {
        statusBar()->showMessage(
            checked ? "AI search on." : "AI search off.", 3000);
    }
}

void MainWindow::onBgeReady() {
    DS_INFO("BGE", "BGE service ready: " + bgeService_->getStatus());
    if (aiSwitch_) {
        aiSwitch_->setEnabled(true);
        // Phase 1.3: Auto-enable AI when BGE is ready.
        // Users shouldn't have to manually find and click the AI toggle.
        if (!aiSwitch_->isChecked()) {
            aiSwitch_->setChecked(true);
        }
    }
    if (hybridSearch_) {
        hybridSearch_->setBgeService(bgeService_.get());
    }
    // Persistent chip: steady state reads "ON"; embedding counts only
    // appear as i/n progress while a backfill batch is running, so an
    // idle number never sits in the status bar confusing anyone.
    if (!aiBackfillRunning_) {
        setAiChip("ON", true);
    }
    statusBar()->showMessage(
        "AI search ready — " + bgeService_->getStatus(), 5000);
    // Drain the embedding backlog right away — previously this only ran
    // when the user manually toggled AI on, so files indexed before the
    // service was up stayed invisible to semantic search.
    ensureEmbeddingsBackfill();
}

void MainWindow::onBgeFailed() {
    const QString why = bgeService_ ? bgeService_->getStatus()
                                    : QString("service unavailable");
    DS_WARN("BGE", "AI subsystem failed to initialize: " + why);
    semanticEnabled_ = false;
    if (aiSwitch_) {
        aiSwitch_->setCheckedNoAnim(false);
        aiSwitch_->setEnabled(false);
        aiSwitch_->setToolTip(
            "AI semantic search is unavailable: " + why +
            "\n\nExpected model files:\n"
            "  models/bge-small-en-v1.5/model.onnx (+ vocab.txt)\n"
            "  next to the app (or under %APPDATA%/DocuSearch/models).\n"
            "Reinstall or restore those files, then restart the app.");
    }
    if (aiControlWidget_) aiControlWidget_->setToolTip(aiSwitch_ ? aiSwitch_->toolTip() : QString());
    // Chip spells out the problem instead of a silent gray switch.
    setAiChip("NO MODEL", false);
    statusBar()->showMessage(
        "AI search unavailable: " + why +
        " - model files missing or failed to load.", 10000);
}

void MainWindow::setAiChip(const QString& text, bool active) {
    if (!aiStateLbl_) return;
    const auto& tp = Theme::active();
    const QString col = text.isEmpty() ? tp.muted
                        : (active ? tp.primaryStrong : tp.muted);
    aiStateLbl_->setText(text);
    aiStateLbl_->setStyleSheet(QString(
        "background:transparent; color:%1; font-weight:800; font-size:11px;"
        " min-width:24px;").arg(col));
}

void MainWindow::ensureEmbeddingsBackfill() {
    // One batch in flight at a time; onBgeEmbeddingFinished chains the
    // next batch while unembedded files remain, so a >1000-file backlog
    // drains progressively instead of being silently truncated.
    if (aiBackfillRunning_ || !bgeService_ || !bgeService_->isReady() || !db_)
        return;
    sqlite3* raw = db_->raw();
    if (!raw) return;

    QVector<int> fileIds;
    QStringList texts;
    bool chunkMode = false;

    // Phase A — documents with extracted text but no embedding at all.
    // Sourced from DocumentText (the authoritative extraction store);
    // the old SearchIndex-based query missed most documents because the
    // FTS table only carries a subset of extracted content.
    {
        sqlite3_stmt* sel = nullptr;
        sqlite3_prepare_v2(raw,
            "SELECT dt.file_id, dt.extracted_text "
            "FROM DocumentText dt "
            "LEFT JOIN BgeEmbeddings e ON e.file_id = dt.file_id "
            "WHERE e.file_id IS NULL "
            "  AND length(dt.extracted_text) > 0 "
            "ORDER BY dt.file_id LIMIT 500;",
            -1, &sel, nullptr);
        if (sel) {
            while (sqlite3_step(sel) == SQLITE_ROW) {
                const int fileId = static_cast<int>(sqlite3_column_int64(sel, 0));
                const unsigned char* c = sqlite3_column_text(sel, 1);
                if (c && c[0]) {
                    fileIds.append(fileId);
                    texts.append(QString::fromUtf8(
                        reinterpret_cast<const char*>(c)));
                }
            }
            sqlite3_finalize(sel);
        }
    }

    // Phase B — embedded documents that predate chunked indexing. They
    // have a full-document embedding but no EmbeddingChunks rows, which
    // used to make semantic search permanently blind to them.
    if (fileIds.isEmpty()) {
        chunkMode = true;
        sqlite3_stmt* sel = nullptr;
        sqlite3_prepare_v2(raw,
            "SELECT dt.file_id, dt.extracted_text "
            "FROM DocumentText dt "
            "WHERE EXISTS (SELECT 1 FROM BgeEmbeddings b WHERE b.file_id = dt.file_id) "
            "  AND NOT EXISTS (SELECT 1 FROM EmbeddingChunks c WHERE c.file_id = dt.file_id) "
            "  AND length(dt.extracted_text) > 1000 "
            "ORDER BY dt.file_id LIMIT 120;",
            -1, &sel, nullptr);
        if (sel) {
            while (sqlite3_step(sel) == SQLITE_ROW) {
                const int fileId = static_cast<int>(sqlite3_column_int64(sel, 0));
                const unsigned char* c = sqlite3_column_text(sel, 1);
                if (c && c[0]) {
                    fileIds.append(fileId);
                    texts.append(QString::fromUtf8(
                        reinterpret_cast<const char*>(c)));
                }
            }
            sqlite3_finalize(sel);
        }
    }
    if (fileIds.isEmpty()) return;

    aiBackfillRunning_ = true;
    aiBackfillChunkMode_ = chunkMode;
    setAiChip(QString("0/%1").arg(fileIds.size()), true);
    statusBar()->showMessage(
        chunkMode
            ? QString("AI: building chunk index for %1 document%2...")
                .arg(fileIds.size()).arg(fileIds.size() == 1 ? "" : "s")
            : QString("AI: generating embeddings for %1 unembedded file%2...")
                .arg(fileIds.size())
                .arg(fileIds.size() == 1 ? "" : "s"));
    bgeService_->embedDocumentsBatch(fileIds, texts);
}

qint64 MainWindow::countMissingEmbeddings() {
    if (!db_) return 0;
    sqlite3* raw = db_->raw();
    if (!raw) return 0;
    sqlite3_stmt* s = nullptr;
    qint64 n = 0;
    if (sqlite3_prepare_v2(raw,
            "SELECT COUNT(*) FROM DocumentText dt "
            "LEFT JOIN BgeEmbeddings e ON e.file_id = dt.file_id "
            "WHERE e.file_id IS NULL AND length(dt.extracted_text) > 0;",
            -1, &s, nullptr) == SQLITE_OK) {
        if (sqlite3_step(s) == SQLITE_ROW) n = sqlite3_column_int64(s, 0);
        sqlite3_finalize(s);
    }
    return n;
}

qint64 MainWindow::countMissingChunkDocs() {
    if (!db_) return 0;
    sqlite3* raw = db_->raw();
    if (!raw) return 0;
    sqlite3_stmt* s = nullptr;
    qint64 n = 0;
    if (sqlite3_prepare_v2(raw,
            "SELECT COUNT(*) FROM DocumentText dt "
            "WHERE EXISTS (SELECT 1 FROM BgeEmbeddings b WHERE b.file_id = dt.file_id) "
            "  AND NOT EXISTS (SELECT 1 FROM EmbeddingChunks c WHERE c.file_id = dt.file_id) "
            "  AND length(dt.extracted_text) > 1000;",
            -1, &s, nullptr) == SQLITE_OK) {
        if (sqlite3_step(s) == SQLITE_ROW) n = sqlite3_column_int64(s, 0);
        sqlite3_finalize(s);
    }
    return n;
}

void MainWindow::onBgeEmbeddingProgress(int current, int total) {
    // Chip stays live during backfill so the user can SEE the AI working.
    setAiChip(QString("%1/%2").arg(current).arg(total), true);
    statusBar()->showMessage(
        QString("Embedding documents: %1/%2").arg(current).arg(total));
}

void MainWindow::onBgeEmbeddingFinished(int success, int fail) {
    aiBackfillRunning_ = false;
    // Deadlock guard: a batch where EVERY file failed would be re-selected
    // verbatim by the next query and retried forever. Two consecutive
    // all-fail batches means something is systematically wrong — stop and
    // say so instead of spinning.
    if (success == 0 && fail > 0) ++aiBackfillDeadlock_;
    else                           aiBackfillDeadlock_ = 0;

    const qint64 remaining = countMissingEmbeddings() + countMissingChunkDocs();
    if (aiBackfillDeadlock_ >= 2 && remaining > 0) {
        aiBackfillDeadlock_ = 0;
        statusBar()->showMessage(
            QString("AI indexing paused — %1 document%2 could not be "
                    "embedded (see log). Keyword search is unaffected.")
                .arg(remaining)
                .arg(remaining == 1 ? "" : "s"), 10000);
        setAiChip(semanticEnabled_ ? "ON" : "OFF", false);
        return;
    }
    if (remaining > 0) {
        // Mid-drain: say exactly how much work is left instead of claiming
        // completion after every batch (the old message fired per batch,
        // which read as "done" while thousands were still queued).
        statusBar()->showMessage(
            QString("AI indexing: %1 processed this pass, %2 remaining...")
                .arg(success).arg(remaining));
        setAiChip(QString("%1 left").arg(remaining), true);
    } else {
        statusBar()->showMessage(
            QString("AI indexing complete — %1 document%2 embedded%3")
                .arg(success)
                .arg(success == 1 ? "" : "s")
                .arg(fail > 0 ? QString(", %1 failed").arg(fail) : QString()),
            8000);
        setAiChip(semanticEnabled_ ? "ON" : "OFF", false);
    }
    // Chain unconditionally while work remains. The old gate stopped the
    // drain whenever the AI toggle was off, freezing the queue forever —
    // embeddings are cheap, async, and useful the moment AI is re-enabled.
    if (remaining > 0 && bgeService_ && bgeService_->isReady()) {
        QTimer::singleShot(250, this, [this]() { ensureEmbeddingsBackfill(); });
    }
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
            // Single-line, self-explanatory: "1,234 indexed". Size info
            // stays in the Stats panel where curious users can find it.
            indexedInfoLbl_->setText(QString("%1 indexed").arg(total));
        }
        if (indexedBar_) {
            // Progress = content_done / total. Only VISIBLE while indexing
            // or extraction is actively running — a permanent partial bar
            // read as "my index is incomplete" (it was also the #1 support
            // question). At idle the badge is just "N indexed".
            const bool busy = contentExtractionRunning_
                || (indexer_ && indexer_->isRunning());
            int pct = total > 0 ? int((contentDone * 100) / total) : 0;
            indexedBar_->setValue(qMin(100, pct));
            indexedBar_->setVisible(busy);
        }

        // Status bar
        if (statusIndexedLbl_) {
            statusIndexedLbl_->setText(QString("Indexed: %1").arg(total));
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

        // NOTE: no periodic statusBar()->showMessage() here anymore — it
        // fired on every timer tick and kept overwriting action feedback
        // ("OCR complete", "N results", …) with raw counters nobody read.
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

        // Refresh the indexed-file counter in the top-right badge + status
        // bar. Throttle to every 5th file to avoid DB hammering.
        // (p.filesScanned is the running count from the Indexer.)
        static qint64 lastRefreshAt = -1;
        const qint64 current = p.filesScanned.load();
        if (current == 0 || current - lastRefreshAt >= 5) {
            updateIndexStats();
            lastRefreshAt = current;
        }
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
    try {
        statusBar()->showMessage("Indexing finished.", 5000);
        // Final refresh so the badge shows the actual end count.
        updateIndexStats();
    } catch (...) {}
}

// ============================================================
// File watcher
// ============================================================
void MainWindow::onFileAdded(const QString& path) {
    if (!repo_ || !db_) return;
    try {
        // SAFETY: Only process files that are under one of the user's
        // indexed folders. The file watcher should only fire for these,
        // but this is a defensive check in case a watch was added for
        // a folder the user later removed from Settings.
        bool underIndexed = false;
        for (const QString& drive : settings_.indexedDrives) {
            if (path.startsWith(drive, Qt::CaseInsensitive)) {
                underIndexed = true;
                break;
            }
        }
        if (!underIndexed) return;

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

                                // NOTE: BGE embedding generation during file-add is DISABLED.
                                // Same reason as in onExtract(): runs ONNX inference on the
                                // main thread — SEH exceptions from ONNX Runtime bypass
                                // catch(...) and crash the app.
                                // Users can generate embeddings via Settings → AI Search →
                                // "Generate AI Embeddings" (runs in a background thread).
                                //
                                // if (bgeService_ && bgeService_->isReady()) {
                                //     try { bgeService_->embedDocument(rec.id, extractedText); } catch (...) {}
                                // }
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

    // Use the singleton — this way the available_ flag persists
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
    // Don't check isAvailable() — the cached flag may be stale.
    // Just try OCR silently. If it fails, empty text will be shown as
    // the result, and the helper's stderr will update the flag for
    // the next status-bar refresh.

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

                    // Save page as temp PNG and OCR it.
                    // Use native separators: the OCR helper exe calls
                    // WinRT StorageFile::GetFileFromPathAsync which
                    // rejects forward slashes with the misleading
                    // "The path contains one or more invalid characters"
                    // error. (Note: ocrFile() also normalizes defensively,
                    // but doing it here keeps the tempPath we log / remove
                    // consistent with what we pass to the helper.)
                    QString tempPath = QDir::toNativeSeparators(
                        QDir::tempPath() + "/docusearch_ocr_page_" +
                        QString::number(i) + ".png");
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
    // OCR results are text — surface them even when the selected file is
    // not a PDF (the extracted-text pane starts hidden for those types).
    previewPane_->setVisible(true);
    updateIndexStats();
    statusBar()->showMessage(
        QString("OCR complete: %1 characters recognized.").arg(ocrText.size()), 5000);

    // Refresh the OCR status indicator — if OCR just succeeded, the
    // Windows.Media.Ocr language packs are definitely installed. This
    // fixes the case where the indicator showed "Setup Required" because
    // the user installed language packs after launching DocuSearch.
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
                updateOcrStatusIndicator();  // refresh in case OCR setup changed
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

        // Wire up "Embed All Documents Now" → shared two-phase backfill.
        // The old inline handler shipped ALL pending texts in one giant
        // batch and only covered missing embeddings — never chunk rows —
        // so it "finished" in seconds while the semantic index stayed
        // blind. Delegating keeps one queue, one message stream, and the
        // background chain drains everything after the dialog closes.
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
                const qint64 pending = countMissingEmbeddings()
                                     + countMissingChunkDocs();
                if (pending == 0) {
                    statusBar()->showMessage(
                        "All documents already have embeddings.", 5000);
                    return;
                }
                ensureEmbeddingsBackfill();
                statusBar()->showMessage(
                    QString("AI indexing started — %1 document%2 in queue; "
                            "progress shows in the status bar.")
                        .arg(pending).arg(pending == 1 ? "" : "s"), 6000);
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
        // Fluent Design — toggle between Light (0) and Dark (1) only.
        // Was previously cycling 4 Pastel Pop themes — too many options.
        pastelTheme_ = (pastelTheme_ + 1) % 2;
        QString names[] = {"Light", "Dark"};
        saveSettings();
        applyTheme();
        statusBar()->showMessage(QString("Theme: %1").arg(names[pastelTheme_]), 2000);
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
        // ── Batch query: get ALL duplicate file records in ONE SQL query ──
        sqlite3* raw = db_->raw();
        if (!raw) return;

        // Duplicate detection is only meaningful for user documents.
        // Photos, screenshots and notes (png/jpg/md/txt…) naturally share
        // bytes or tiny sizes and were pure noise here — the whitelist is
        // deliberately limited to real document formats.
        static const char* kDocTypes[] = {
            "pdf", "doc", "docx", "xls", "xlsx",
            "ppt", "pptx", "rtf", "csv"
        };
        QString typeList;
        for (const char* t : kDocTypes) {
            if (!typeList.isEmpty()) typeList += QLatin1Char(',');
            typeList += QString("'%1'").arg(QLatin1String(t));
        }

        sqlite3_stmt* s = nullptr;
        // Find document files that share a hash with at least one other
        // file. Ordered by hash so duplicates appear grouped.
        const QString sql = QString(
            "SELECT f.id, f.path, f.filename, f.extension, f.size, "
            "       f.modified_date, f.hash "
            "FROM Files f "
            "WHERE f.hash != '' AND lower(f.extension) IN (%1) AND f.hash IN ("
            "  SELECT hash FROM Files WHERE hash != '' "
            "  GROUP BY hash HAVING COUNT(*) > 1"
            ") ORDER BY f.hash, f.filename;").arg(typeList);
        sqlite3_prepare_v2(raw, sql.toUtf8().constData(), -1, &s, nullptr);

        QList<SearchHit> hits;
        QStringList hashes;      // aligned with hits — used to regroup below
        int skippedMissing = 0;

        while (sqlite3_step(s) == SQLITE_ROW) {
            SearchHit h;
            h.fileId       = sqlite3_column_int64(s, 0);
            const unsigned char* p  = sqlite3_column_text(s, 1);
            const unsigned char* fn = sqlite3_column_text(s, 2);
            const unsigned char* e  = sqlite3_column_text(s, 3);
            h.path         = p  ? QString::fromUtf8(reinterpret_cast<const char*>(p))  : QString();
            h.filename     = fn ? QString::fromUtf8(reinterpret_cast<const char*>(fn)) : QString();
            h.extension    = e  ? QString::fromUtf8(reinterpret_cast<const char*>(e))  : QString();
            h.size         = sqlite3_column_int64(s, 4);
            h.modifiedDate = QDateTime::fromSecsSinceEpoch(sqlite3_column_int64(s, 5));
            const unsigned char* hash = sqlite3_column_text(s, 6);
            const QString currentHash =
                hash ? QString::fromUtf8(reinterpret_cast<const char*>(hash)) : QString();

            // Index rows can outlive their files (deleted after scanning).
            // A "duplicate" pointing at nothing helps nobody — skip it.
            if (!QFile::exists(h.path)) { ++skippedMissing; continue; }

            hits.append(h);
            hashes.append(currentHash);

            // Process events periodically so the UI doesn't freeze when a
            // large folder produces thousands of duplicate candidates.
            if ((hits.size() + skippedMissing) % 400 == 0)
                QApplication::processEvents();
        }
        sqlite3_finalize(s);

        // Regroup AFTER filtering so vanished files never count as groups.
        int groupCount = 0;
        QString lastHash;
        for (const QString& hs : hashes) {
            if (hs != lastHash) { ++groupCount; lastHash = hs; }
        }

        if (hits.isEmpty()) {
            QMessageBox::information(this, "Duplicates",
                skippedMissing > 0
                    ? QStringLiteral(
                        "No duplicates among your remaining documents.\n\n"
                        "%1 stale index entries (files already deleted from "
                        "disk) were ignored. They disappear permanently after "
                        "the next re-scan.").arg(skippedMissing)
                    : QStringLiteral(
                        "No duplicate documents found.\n\n"
                        "Duplicate search covers documents only: "
                        ".pdf .doc/.docx .xls/.xlsx .ppt/.pptx .rtf .csv\n\n"
                        "Make sure 'Compute file hashes' is enabled in "
                        "Settings → Performance, then re-scan your folders."));
            return;
        }

        resultsPane_->setResults(hits);
        statusBar()->showMessage(
            skippedMissing > 0
                ? QString("Found %1 duplicate groups (%2 files); %3 deleted files ignored.")
                    .arg(groupCount).arg(hits.size()).arg(skippedMissing)
                : QString("Found %1 duplicate groups (%2 files)")
                    .arg(groupCount).arg(hits.size()),
            8000);
    } catch (const std::exception& e) {
        DS_ERROR("Duplicates", QString("Failed: %1").arg(e.what()));
        statusBar()->showMessage("Duplicate detection failed.", 3000);
    } catch (...) {
        statusBar()->showMessage("Duplicate detection failed.", 3000);
    }
}

} // namespace DocuSearch
