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
#include "../ocr/OcrWorkerPool.h"
#include "../ocr/WindowsOcrEngine.h"
#include "../monitoring/FileWatcher.h"
#include "../documents/DocumentExtractorRegistry.h"
#include "../preview/FilePreviewPane.h"
#include "../embeddings/BgeService.h"
#include "../search/HybridSearchEngine.h"
#include "../settings/SettingsManager.h"

#ifdef DOCUSEARCH_HAS_PDFIUM
#  include "../pdf/PdfiumDocument.h"
#endif

#include <QApplication>
#include <QEventLoop>
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
#include <QStandardPaths>
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
#include <QLocale>
#include <QDialog>
#include <QDialogButtonBox>
#include <QRadioButton>
#include <QPushButton>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPalette>
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QHash>
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
#include <algorithm>   // std::clamp (OCR pool size from settings)

namespace DocuSearch {

// ── Brand logo pixmap ─────────────────────────────────────────
// The title bar must carry the REAL DocuSearch logo (the same artwork
// the taskbar icon uses: :/icons/DocuSearch-256.png) — not a generic
// white magnifier glyph. The old title-bar logo was a plain Lucide
// "search" icon that did not match the app's branding ("top-left logo
// is not correct"). The PNG is 256x256 with its own rounded corners and
// transparent margins; it is downscaled with smooth transforms to the
// label size (28 logical px) at the current device pixel ratio, so it
// stays crisp on HiDPI displays.
inline QPixmap appLogoPixmap(qreal dpr = 1.0) {
    const int px = qMax(1, qRound(28.0 * dpr));
    QPixmap pm(QStringLiteral(":/icons/DocuSearch-256.png"));
    if (pm.isNull()) {
        // Fallback if the resource is ever missing: draw the same
        // white search glyph the old code used, so the spot never
        // renders as an empty hole.
        return loadLucidePixmap("search", QColor("#ffffff"), 28, dpr);
    }
    pm = pm.scaled(px, px, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    pm.setDevicePixelRatio(dpr);
    return pm;
}

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

    // v1.7.4: The splash screen is visible while this constructor runs, but
    // app.exec() has not started yet — nothing animates unless we pump the
    // event loop by hand. SplashOverlay's animation is time-based now, so
    // each pump below repaints the splash at the correct animation phase.
    // v1.7.7: ONE processEvents call per milestone rendered at most one
    // splash frame, so long constructor steps froze the bar and the phase
    // visibly jumped (the reported "glitch"). Three short paced turns let
    // the 16 ms animation timer fire 2-3 times per milestone — the sweep
    // advances smoothly instead of in jump-cuts, for ~100 ms of extra
    // startup time in total.
    auto pumpSplash = []() {
        for (int i = 0; i < 3; ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 16);
    };

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

    // v1.7.7: one-time cleanup — older versions indexed EVERY file type
    // they walked (md notes, installers, archives, OS junk) because the
    // hourly scan and the full indexer had no extension filter.
    // v1.7.8: THE PURGE NO LONGER RUNS HERE — it was the reason the app
    // could look stuck on the splash forever. Legacy indexes hold tens
    // of thousands of non-document rows (plus their SearchIndex and
    // embedding rows); deleting them all before the window ever showed
    // froze the splash for minutes, and killing the app mid-purge rolled
    // the single transaction back, so the very next launch froze again —
    // a "stuck at splash" loop. The scan gate + watcher gate guarantee
    // non-documents can never be re-added, so the cleanup is cosmetic:
    // it is now scheduled AFTER the window is visible (t+4.5 s, near the
    // t+2 s change scan) and reports its own progress. See
    // purgeNonIndexableRows().
    pumpSplash();  // keep the splash animating during the heavy startup path

    search_  = std::make_unique<SearchEngine>(*db_, *repo_, this);

    loadSettings();
    pumpSplash();

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
    pumpSplash();

    // Main 4-area layout in the middle (sidebar + center + right panel).
    // buildCentral() creates a horizontal layout that holds sidebar +
    // center + right panel and adds it to mainLay.
    buildCentral();
    pumpSplash();

    // Status bar at bottom (created by QMainWindow::statusBar()).
    buildStatusBar();
    pumpSplash();

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
    // App-wide filter: gives every tooltip window translucency so its QSS
    // border-radius produces REAL rounded corners (see eventFilter).
    qApp->installEventFilter(this);
    pumpSplash();  // v1.7.7: OCR probe + QSS install can take a beat

    // v1.7.9: WIRE THE OCR POOL — declared since the beginning, shut down
    // in the destructor, but NEVER constructed and NEVER enqueued. Every
    // file the extractors flagged needs_ocr (scanned PDFs, garbled text
    // layers) and every image sat stranded forever, and "Extract" kept
    // reporting nothing to do while whole folders were unprocessed. The
    // pool runs its own per-worker WindowsOcrEngine, renders PDF pages
    // via PDFium (v1.7.2 pool support + v1.7.6 auto-orientation) and
    // emits taskCompleted; onOcrTaskCompleted() writes the results.
    // v1.7.11: pool size comes from Settings → Performance → "Worker
    // threads" (was hardcoded 2, making the spinbox a placebo). Clamped
    // to 1..4: each worker spawns its own ocr helper process + PDFium
    // rasterizer, and >4 gives no throughput win on consumer disks.
    // Changing it takes effect on the next launch (the pool's thread
    // count is fixed at construction; throttle settings stay live via
    // setAppSettings).
    ocrPool_ = std::make_unique<OcrWorkerPool>(
        std::clamp(settings_.maxWorkerThreads, 1, 4), this);
    ocrPool_->setAppSettings(settings_);
    connect(ocrPool_.get(), &OcrWorkerPool::taskCompleted,
            this, &MainWindow::onOcrTaskCompleted);
    connect(ocrPool_.get(), &OcrWorkerPool::logMessage, this,
            [](const QString& m) { DS_INFO("OCR", m); });

    // v1.7.10 FIRST-RUN EXTRACT-ALL: until the very first full extraction
    // drain completes (firstRunDone), extraction sessions run 200 files
    // and re-arm in 3 s, so a fresh index fully extracts itself right
    // after the first Add-Folder scan — the user never has to keep
    // clicking Extract to see content search working.
    extractAllMode_ = !settings_.firstRunDone;
    if (extractAllMode_)
        DS_INFO("Extract", "First run — extraction will drain the whole "
                           "queue automatically.");

    // DEFER semantic search init to after the window is shown.
    // initializeSemanticSearch() creates a BgeService + QtConcurrent::run
    // which, even though it runs on a worker thread, still allocates memory
    // and loads the ONNX model path check on the main thread. Deferring it
    // means the window appears faster.
    QTimer::singleShot(100, this, [this]() {
        initializeSemanticSearch();
    });

    applyTheme();
    pumpSplash();

    // --- Signals (only the ones that don't need crash-prone subsystems) ---
    connect(searchBar_, &SearchBar::searchRequested,
            this, &MainWindow::onSearch);
    connect(searchBar_, &SearchBar::savedSearchSelected,
            this, &MainWindow::onSavedSearchSelected);
    connect(searchBar_, &SearchBar::addFolderRequested,
            this, &MainWindow::onAddFolder);
    // v1.7.11: SearchBar::refreshRequested removed - the refresh button no
    // longer exists and F5 triggers onRefresh() directly; the connect was
    // a dead wire on a signal nothing can ever emit.
    connect(searchBar_, &SearchBar::extractRequested,
            this, &MainWindow::onExtract);
    connect(searchBar_, &SearchBar::filtersRequested,
            this, &MainWindow::onFilters);

    connect(resultsPane_, &ResultsPane::fileSelected,
            this, &MainWindow::onFileSelected);
    connect(resultsPane_, &ResultsPane::fileActivated,
            this, &MainWindow::onFileActivated);
    // v1.7.13: "Delete duplicate copies" header action on duplicates
    // results (armed by onDetectDuplicates, disarmed by setResults).
    connect(resultsPane_, &ResultsPane::actionRequested,
            this, &MainWindow::onDeleteDuplicateCopies);

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

    // Route the custom title-bar close button through QWidget::close()
    // (NOT QApplication::quit()). quit() tears the event loop down without
    // delivering QCloseEvent, so closeEvent() never ran from the PRIMARY
    // close path of this frameless window — window geometry, splitter
    // sizes and settings were silently lost, and the "indexing still
    // running" confirmation was skipped.
    connect(titleCloseBtn_, &QPushButton::clicked,
            this, &QWidget::close);
    // Use explicit lambdas — defensive against Qt 6 member-function-pointer
    // ambiguity on QWidget slots. (User reported minimize not working.)
    connect(titleMinBtn_, &QPushButton::clicked,
            this, [this]{ this->showMinimized(); });
    connect(titleMaxBtn_, &QPushButton::clicked,
            this, [this]{
        if (isMaximized()) showNormal();
        else showMaximized();
    });
    // Theme toggle lives in the status bar (themeToggleBtn_ →
    // onToggleTheme, wired in setupStatusBar).

    // Search is triggered ONLY when the user presses Enter or clicks
    // the search input. No live/auto search while typing.
    // (liveSearchTimer_ kept for potential future use but not started.)

    // Auto-scan timer: 1 hour interval, runs on MAIN THREAD.
    // v1.7.3: no tick-level busy guard here — the function itself retries
    // 10 min later when the pipeline is busy instead of losing the tick.
    autoScanTimer_ = new QTimer(this);
    autoScanTimer_->setInterval(3600 * 1000);  // 1 hour
    connect(autoScanTimer_, &QTimer::timeout, this, [this]{
        autoScanIndexedFolders();
    });
    autoScanTimer_->start();

    // Live index stats: refresh the "N indexed" badge every 20 s so the
    // number always reflects the database — it used to change only when a
    // specific event happened to call updateIndexStats, which read as a
    // frozen ("hard coded") figure while extraction was running.
    auto* statsRefreshTimer = new QTimer(this);
    statsRefreshTimer->setInterval(20 * 1000);
    connect(statsRefreshTimer, &QTimer::timeout,
            this, &MainWindow::updateIndexStats);
    statsRefreshTimer->start();

    // Startup diff: check for files that changed while app was closed.
    QTimer::singleShot(2000, this, [this]() {
        if (!contentExtractionRunning_) {
            statusBar()->showMessage("Checking for file changes...", 3000);
            autoScanIndexedFolders();
        }
    });

    // v1.7.8: one-time non-document purge, scheduled AFTER the window is
    // visible — never inside the constructor (see the v1.7.8 note at the
    // top of this ctor for why). 4.5 s lands past the splash window and
    // just behind the t+2 s change scan; the purge commits in 500-row
    // batches and pumps the event loop between them, so it can slow
    // nothing down and freeze nothing.
    QTimer::singleShot(4500, this, [this]() { purgeNonIndexableRows(); });

    // v1.7.9: STARTUP INTEGRITY PASS (t+6.5 s, window already visible).
    // Requeues fake-done rows (scans older than v1.7.3 stamped
    // content_done without extracting — "Extract" then had nothing to do
    // forever) and backfills missing hashes so the duplicates finder has
    // something to group. See runStartupIntegrityPass().
    QTimer::singleShot(6500, this, [this]() { runStartupIntegrityPass(); });

    // v1.7.4: AUTO-EXTRACT — 60 s after launch, start extracting pending
    // files automatically (user request: "automatically start extracting
    // after 1 min of app opening"). The Extract button switches to
    // "Stop Extracting" while it runs. If the startup scan is still
    // walking folders, requestAutoExtract() retries every 30 s instead of
    // fighting it for the database; the scan's own completion handler
    // would wake extraction anyway when it finds work.
    autoExtractRetryLeft_ = 20;   // 20 x 30 s = up to 10 min of patience
    QTimer::singleShot(60 * 1000, this, [this]() {
        requestAutoExtract();
    });

    // Phase 9: Wire up FileWatcher with debounce.
    // The watcher monitors indexed folders in real-time. Events are
    // debounced (500ms) to merge rapid add+modify sequences.
    watcher_ = std::make_unique<FileWatcher>(this);

    // v1.7.4: a FileWatcher whose kernel change buffer overflowed used to
    // DIE silently (stopping=true + break) — live tracking silently ended
    // for that root and the index went stale. The watcher now stays alive
    // and asks for a reconciling scan instead. Throttled to one rescan per
    // minute so a storm of overflows cannot hammer the disk.
    connect(watcher_.get(), &FileWatcher::rescanRequested, this,
            [this](const QString& root) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - lastWatcherRescanMs_ < 60 * 1000) return;
        lastWatcherRescanMs_ = now;
        DS_WARN("Watcher", QString("Change buffer overflowed for %1 - "
                                   "running reconciling scan").arg(root));
        statusBar()->showMessage(
            QStringLiteral("Many files changed at once under %1 — "
                           "reconciling the index...").arg(root), 6000);
        autoScanIndexedFolders();
    });

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

    // Start watching indexed folders — but ONLY if the user left
    // "Monitor indexed drives for live changes" enabled (v1.7.11: the
    // checkbox was never consulted anywhere; the watcher always ran).
    if (settings_.monitorFileChanges && !settings_.indexedDrives.isEmpty()) {
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

    pumpSplash();  // v1.7.7: geometry restore + saved-searches ran unpumped

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
    // v1.7.11: join the BGE init worker — it captures `this` and uses
    // bgeService_, which is about to be destroyed with the window.
    if (bgeInitFuture_.isValid()) bgeInitFuture_.waitForFinished();
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

    // App logo (28x28 — the real DocuSearch brand mark, same artwork as
    // the taskbar icon; v1.7.5 replaced the generic Lucide magnifier).
    appLogoLbl_ = new QLabel(titleBar_);
    appLogoLbl_->setObjectName("appLogo");
    appLogoLbl_->setFixedSize(28, 28);
    appLogoLbl_->setPixmap(appLogoPixmap(devicePixelRatio()));
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
    // v1.7.14: the single "N indexed" number grew into the full
    // breakdown the user asked for — three pill chips in one row:
    //   [Indexed N]  files searchable right now (content done or staged)
    //   [Extracted N] of those, files whose text was actually pulled out
    //   [Embedded N]  of those, files the AI holds a vector for
    // plus the thin progress bar, which still only appears while busy.
    auto* statusBadge = new QWidget(sidebar_);
    statusBadge->setObjectName("indexedStatus");
    auto* sbLay = new QHBoxLayout(statusBadge);
    sbLay->setContentsMargins(8, 4, 8, 4);
    sbLay->setSpacing(6);
    indexedInfoLbl_ = new QLabel("Indexed 0", statusBadge);
    indexedInfoLbl_->setObjectName("indexedInfo");
    indexedInfoLbl_->setToolTip(
        "Total indexed — files currently searchable in your offline index "
        "(content extracted or metadata staged).\n"
        "The small bar appears only while files are being processed and "
        "disappears when the queue is done.");
    sbLay->addWidget(indexedInfoLbl_);

    extractedInfoLbl_ = new QLabel("Extracted 0", statusBadge);
    extractedInfoLbl_->setObjectName("extractedInfo");
    extractedInfoLbl_->setToolTip(
        "Total extracted — files whose text was actually pulled out "
        "(native text or OCR) and is full-text searchable.\n"
        "Empty-but-valid files (scanned images with no readable text) "
        "are not counted here.");
    sbLay->addWidget(extractedInfoLbl_);

    embeddedInfoLbl_ = new QLabel("Embedded 0", statusBadge);
    embeddedInfoLbl_->setObjectName("embeddedInfo");
    embeddedInfoLbl_->setToolTip(
        "Total embedded — files the AI (semantic search) holds a vector "
        "for, counted once whether chunked or whole-document.\n"
        "Semantic search only sees these files; the number grows while "
        "the AI chip shows a progress count.");
    sbLay->addWidget(embeddedInfoLbl_);

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
    QString tooltipBorder;
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
            tooltipBg  = "#1f2633";   // soft slate glass (was near-black)
            tooltipText = "#e8edf5";
            tooltipBorder = "#3d4960";
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
            tooltipBg  = "#f7f9fc";   // porcelain glass (was dark navy)
            tooltipText = "#1c2430";
            tooltipBorder = "#d5dde7";
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
    // Tooltip BORDER token feeds the QSS rule only (palette above blends
    // the square corners outside the tooltip's border-radius).
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
        {"@tooltipBorder@",tooltipBorder},
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
    // App logo: the real DocuSearch brand mark (set in ctor, but re-set
    // here in case the device pixel ratio changed).
    appLogoLbl_->setPixmap(appLogoPixmap(devicePixelRatio()));

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
    // v1.7.6: pastelTheme_ is what applyTheme() actually renders. It used
    // to stay 0 (Light) forever — the saved dark mode was IGNORED at
    // startup and the toggle never persisted. Keep the two in sync at
    // every site that changes the theme.
    pastelTheme_ = darkMode_ ? 1 : 0;
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
namespace {
// v1.7.11: Normalize the user's "Excluded Extensions" list into a fast
// lookup set - trimmed, lowercased, leading dot stripped (".ISO", "iso"
// and " Iso " all mean the same thing). Shared by every ingest gate
// (Add-Folder scan, hourly scan, live watcher) so the setting finally
// takes effect: before this, the list was saved and round-tripped but
// consulted by NOTHING.
QSet<QString> normalizedExtSet(const QStringList& exts) {
    QSet<QString> out;
    out.reserve(exts.size());
    for (QString e : exts) {
        e = e.trimmed().toLower();
        while (e.startsWith('.')) e.remove(0, 1);
        if (!e.isEmpty()) out.insert(e);
    }
    return out;
}

// v1.7.4: True when the STORAGE ROOT of an absolute path is reachable.
// Used to tell "this file was deleted" apart from "the whole drive is
// offline": an unplugged USB drive or disconnected network share must
// NEVER cause index purges (the hourly scan skips unavailable folders
// for exactly the same reason). Only hide results for offline roots.
bool storageRootReachable(const QString& path) {
    if (path.isEmpty()) return false;
    const QString abs = QDir::toNativeSeparators(
        QFileInfo(path).absoluteFilePath());
    if (abs.startsWith(QLatin1String("\\\\"))) {
        // UNC: \\server\share\... — the share is the storage root.
        const QStringList parts = abs.split('\\', Qt::SkipEmptyParts);
        if (parts.size() < 2) return false;
        const QString root = QLatin1String("\\\\") + parts.at(0)
                           + QLatin1Char('\\') + parts.at(1);
        return QFileInfo::exists(root);
    }
    if (abs.size() >= 3 && abs.at(1) == QLatin1Char(':')) {
        return QFileInfo::exists(abs.left(3));   // e.g. "D:\"
    }
    return QFileInfo::exists(abs);
}

// v1.7.3: hide results whose file no longer exists (deleted or moved while
// the app was closed — the FileWatcher only catches changes while we run).
// The hourly scan prunes them from the index; this covers the gap until
// the next scan so users never click a result that opens to nothing.
// v1.7.4: returns the hidden paths so the caller can PURGE the rows whose
// drive is still reachable (self-healing index); rows on offline roots are
// only hidden — purging those would wipe live data.
QStringList hideStaleResults(QList<SearchHit>& hits) {
    QList<SearchHit> kept;
    kept.reserve(hits.size());
    QStringList removedPaths;
    for (const SearchHit& h : hits) {
        if (!h.path.isEmpty() && !QFileInfo::exists(h.path)) {
            removedPaths.append(h.path);
            continue;
        }
        kept.append(h);
    }
    hits = kept;
    return removedPaths;
}
} // namespace

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
                // Semantic-only hits (AI found the document, keywords did
                // not) carry NO metadata through the fusion layer: no
                // extension, no size, no date. They used to render with an
                // empty badge ("unrecognized") and "0 B" even though the
                // file was perfectly fine. Backfill straight from disk.
                if (h.size <= 0 || h.extension.isEmpty()
                    || !h.modifiedDate.isValid()) {
                    const QFileInfo fi(h.path);
                    if (h.extension.isEmpty())
                        h.extension = fi.suffix().toLower();
                    if (h.size <= 0)
                        h.size = fi.size();
                    if (!h.modifiedDate.isValid())
                        h.modifiedDate = fi.lastModified();
                }
                merged.append(h);
            }
            // v1.7.3/1.7.4: hide stale entries (file deleted/moved while the
            // app was closed) before display, then PURGE the rows whose
            // drive is still reachable so they never come back.
            const QStringList stalePaths = hideStaleResults(merged);
            const int staleHidden = stalePaths.size();
            const int stalePurged = purgeStaleRows(stalePaths, QStringLiteral("hybrid search"));
            resultsPane_->setResults(merged);
            if (staleHidden > 0) {
                statusBar()->showMessage(
                    QStringLiteral("%1 stale result%2 hidden (file deleted "
                                   "or moved)%3")
                        .arg(staleHidden)
                        .arg(staleHidden == 1 ? "" : "s")
                        .arg(stalePurged > 0
                            ? QStringLiteral(" — %1 stale index entr%2 removed")
                                  .arg(stalePurged)
                                  .arg(stalePurged == 1 ? "y" : "ies")
                            : QString()),
                    6000);
            }
            // Phase 4: surface the AI contribution honestly. The fusion
            // is now strictly ADDITIVE — keyword results are never
            // reordered or dropped — so the status line reports the
            // keyword count and the AI-only additions separately.
            int aiContribCount = 0;
            int aiOnlyCount = 0;
            for (const auto& hr : hybridResults) {
                if (hr.semanticScore > 0.01f) ++aiContribCount;
                if (hr.semanticScore > 0.01f && hr.keywordScore < 0.01f) ++aiOnlyCount;
            }
            statusBar()->showMessage(
                QString("%1 result%2 · keyword %3 · AI-found %4 · %5 ms")
                    .arg(merged.size())
                    .arg(merged.size() == 1 ? "" : "s")
                    .arg(merged.size() - aiOnlyCount)
                    .arg(aiOnlyCount)
                    .arg(t.elapsed()));
            // Persistent summary pill directly above the results list —
            // the status-bar toast disappears, this stays until the next
            // search so users can actually SEE what AI did.
            if (aiOnlyCount > 0) {
                resultsPane_->setAiSummary(QString(
                    "<b>AI added %1 document%2</b> that keyword search "
                    "missed (listed after your %3 keyword result%4). "
                    "Keyword order is never changed.")
                    .arg(aiOnlyCount)
                    .arg(aiOnlyCount == 1 ? "" : "s")
                    .arg(merged.size() - aiOnlyCount)
                    .arg(merged.size() - aiOnlyCount == 1 ? "" : "s"));
            } else if (aiContribCount > 0) {
                // AI confirmed keyword hits only — no new documents
                // cleared the similarity bar this pass.
                const int confirmed = aiContribCount - aiOnlyCount;
                resultsPane_->setAiSummary(QString(
                    "<b>AI confirmed %1 keyword match%2</b> — no new "
                    "documents scored above the similarity bar. Keyword "
                    "order is never changed.")
                    .arg(confirmed)
                    .arg(confirmed == 1 ? "" : "es"));
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
                    // Nothing was comparable this pass. Only blame the
                    // chunk backfill while work is genuinely pending —
                    // the old wording claimed "chunk index building"
                    // even when the real problem was that semantic
                    // search never ran (see the onBgeReady ordering
                    // fix) or no comparable embedding exists.
                    const qint64 pendingDocs   = countMissingEmbeddings();
                    const qint64 pendingChunks = countMissingChunkDocs();
                    if (pendingDocs > 0 || pendingChunks > 0) {
                        resultsPane_->setAiSummary(QString(
                            "<b>AI index warming up.</b> %1 document%2 "
                            "embedded; %3 still pending in the AI index "
                            "build — results below are keyword-only for "
                            "now. This is one-time background work.")
                            .arg(stats.total)
                            .arg(stats.total == 1 ? " is" : "s are")
                            .arg(pendingDocs + pendingChunks));
                    } else {
                        resultsPane_->setAiSummary(QString(
                            "<b>Semantic scan found nothing to compare.</b> "
                            "%1 embedding%2 exist but the last query "
                            "compared none — check the log (BGE) for "
                            "tokenizer/model errors.")
                            .arg(stats.total)
                            .arg(stats.total == 1 ? "" : "s"));
                    }
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
            // v1.7.3/1.7.4: hide stale entries, purge the purgeable ones.
            const QStringList stalePaths = hideStaleResults(hits);
            const int staleHidden = stalePaths.size();
            purgeStaleRows(stalePaths, QStringLiteral("keyword search"));
            resultsPane_->setResults(hits);
            resultsPane_->setAiSummary(QString());
            statusBar()->showMessage(
                staleHidden > 0
                    ? QStringLiteral("%1 result%2 in %3 ms · %4 stale hidden")
                          .arg(hits.size())
                          .arg(hits.size() == 1 ? "" : "s")
                          .arg(t.elapsed())
                          .arg(staleHidden)
                    : QString("%1 result%2 in %3 ms")
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

    // v1.7.4 SELF-HEAL: a result row whose file no longer exists used to
    // land here and surface as "File not found or locked" / "Cannot open
    // PDF" in the preview ("error in opening pdf preview"). When the
    // storage root is still reachable the row is a genuine ghost — remove
    // it from the index right now and say so. On an offline root we keep
    // the row (the file may simply be unplugged).
    if (!path.isEmpty() && !QFileInfo::exists(path)) {
        if (storageRootReachable(path)) {
            const QStringList one{ path };
            purgeStaleRows(one, QStringLiteral("file selection"));
            statusBar()->showMessage(
                QStringLiteral("File no longer on disk — stale index entry "
                               "removed: %1").arg(path), 6000);
        } else {
            statusBar()->showMessage(
                QStringLiteral("File is on a drive that is currently "
                               "unavailable: %1").arg(path), 6000);
        }
    }

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
    // images. v1.7.7: this private list is GONE — scanFolderFast now
    // consults the SAME central allowlist (Constants::isIndexableExtension)
    // as the hourly scan, the watcher and the full re-index, so every
    // ingest path agrees on what may enter the index.
    //
    // v1.7.10: this scan also computes the CONTENT HASH. The INSERT
    // below never wrote the hash column, so every row added through
    // "Add Folder" — the most common ingest path — had hash='' and the
    // duplicate finder (which groups on hash) honestly reported "no
    // duplicate documents found" for an index FULL of duplicates. Now
    // the index is duplicates-ready the moment the scan finishes,
    // matching what the hourly walk already does.
    const bool hashEnabled = settings_.hashLargeFiles;
    int count = 0, skipped = 0, hashed = 0;
    // v1.7.11: honor Settings → Indexing. The walk used to receive an
    // EMPTY exclude list ("emptyExcludes"), so Excluded Folders was
    // silently ignored by the most common ingest path (Add Folder /
    // newly added drives), and Excluded Extensions was consulted by
    // nothing at all — both settings looked broken to the user.
    const QSet<QString> userExcludedExts =
        normalizedExtSet(settings_.excludedExtensions);
    FileUtils::walkDirectory(folder, settings_.excludedFolders,
                             [&](const QFileInfo& fi) -> bool {
        const QString ext = FileUtils::extensionOf(fi.absoluteFilePath()).toLower();
        if (!Constants::isIndexableExtension(ext)) { ++skipped; return true; }
        if (userExcludedExts.contains(ext))        { ++skipped; return true; }

        const QString path = FileUtils::toNative(fi.absoluteFilePath());
        const QString filename = fi.fileName();
        const qint64 size = fi.size();
        const qint64 created = fi.birthTime().toSecsSinceEpoch();
        const qint64 modified = fi.lastModified().toSecsSinceEpoch();
        const char* ocrStat = (Constants::kDocumentExtensions.contains(ext) ||
                               Constants::kImageExtensions.contains(ext))
                              ? "pending" : "not_needed";

        // Same 64 MB cap as the hourly walk, so both paths store the
        // SAME fingerprint for the SAME file and duplicates group
        // correctly no matter which scanner saw the file first.
        QString hash;
        if (hashEnabled) {
            hash = FileUtils::sha256OfFile(path, 64 * 1024 * 1024);
            if (!hash.isEmpty()) ++hashed;
        }

        sqlite3_stmt* s = nullptr;
        sqlite3_prepare_v2(raw,
            "INSERT INTO Files (path, filename, extension, size, "
            "  created_date, modified_date, hash, indexing_status, ocr_status) "
            "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, 'metadata_only', ?8) "
            "ON CONFLICT(path) DO UPDATE SET "
            "  filename=excluded.filename, extension=excluded.extension, "
            "  size=excluded.size, modified_date=excluded.modified_date, "
            "  hash=CASE WHEN excluded.hash != '' "
            "            THEN excluded.hash ELSE Files.hash END;",
            -1, &s, nullptr);
        if (s) {
            sqlite3_bind_text(s, 1, path.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(s, 2, filename.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(s, 3, ext.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(s, 4, size);
            sqlite3_bind_int64(s, 5, created);
            sqlite3_bind_int64(s, 6, modified);
            sqlite3_bind_text(s, 7, hash.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(s, 8, ocrStat, -1, SQLITE_TRANSIENT);
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
        QString("Scan complete: %1 files indexed, %2 skipped%3")
            .arg(count).arg(skipped)
            .arg(hashed > 0 ? QString(
                ", %1 fingerprint%2 computed")
                .arg(hashed).arg(hashed == 1 ? "" : "s") : QString()), 5000);
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

        // v1.7.4: watch the folder LIVE. addWatches() ran once at startup
        // only, so folders added through this button were scanned but not
        // monitored — later changes in them went unseen until the next
        // hourly scan (another "deleted files still show up" contributor).
        if (watcher_ && !watcher_->isWatched(folder)) {
            watcher_->addWatch(folder);
        }

        // v1.7.14: persist the folder into the SAME list the Settings →
        // Indexing panel displays (indexedDrives). Without this, folders
        // added from the dash were invisible in Settings AND silently
        // dropped from every settings-driven loop: startup watches
        // (addWatches(indexedDrives)), the hourly auto-scan (which
        // early-returns when the list is empty), and the per-folder
        // integrity pass — the folder was indexed once and then on its own.
        // Nesting is deduped: a folder already covered by an existing
        // entry is not re-added, and existing entries INSIDE the new
        // folder are folded into it (the parent covers them).
        {
            const QString canon = QDir::cleanPath(folder);
            bool covered = false;
            QStringList kept;
            kept.reserve(settings_.indexedDrives.size());
            for (const QString& root : settings_.indexedDrives) {
                if (root.trimmed().isEmpty()) continue;
                const QString rp = QDir::cleanPath(root);
                if (rp.compare(canon, Qt::CaseInsensitive) == 0 ||
                    canon.startsWith(rp + QDir::separator(),
                                     Qt::CaseInsensitive)) {
                    covered = true;            // already listed / parent covers it
                    kept << root;
                } else if (!rp.startsWith(canon + QDir::separator(),
                                          Qt::CaseInsensitive)) {
                    kept << root;              // unrelated — keep
                }
                // else: rp sits inside the new folder — the parent covers it
            }
            if (!covered) {
                settings_.indexedDrives = kept;
                settings_.indexedDrives << canon;
                // Config::save is a member of the singleton (CI #317:
                // C2352 when called as if static).
                Config::instance().save(settings_);
            }
        }

        scanFolderFast(folder);

        // Auto-start extraction immediately after scanning.
        // This extracts text from all newly-indexed files in the
        // background (QTimer, 200 files per session) so the user
        // doesn't need to manually click Extract.
        statusBar()->showMessage("Scan complete. Starting auto-extraction...", 3000);
        QApplication::processEvents();
        QTimer::singleShot(500, this, [this]() {
            autoExtractRetryLeft_ = 20;  // fresh budget (see requestAutoExtract)
            requestAutoExtract();
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
    // QSignalBlocker prevents the reset from re-entering this slot via
    // currentRowChanged (the sidebar connects that signal). Without it,
    // any stray setCurrentRow(N) here would literally CLICK nav item N
    // again — that is exactly how clicking Help used to fire the
    // Duplicates finder right after the help box (row 0 = Duplicates).
    {
        const QSignalBlocker block(sidebarList_);
        sidebarList_->setCurrentRow(-1);
    }
    if (page == "Settings") {
        onOpenSettings();
    } else if (page == "About") {
        onAbout();
    } else if (page == "Help") {
        // v1.7.11: the quick-reference stays inline, but the full guide
        // (HELP.md, bundled next to the exe by CI) is one click away —
        // help content is maintained in ONE place instead of drifting
        // between this dialog and the shipped docs.
        QMessageBox box(this);
        box.setWindowTitle("How to Search");
        box.setTextFormat(Qt::RichText);
        box.setText(
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
        box.addButton(QMessageBox::Ok);
        const QString helpPath =
            QCoreApplication::applicationDirPath() + "/HELP.md";
        QPushButton* fullBtn = nullptr;
        if (QFileInfo::exists(helpPath)) {
            fullBtn = box.addButton("Open Full Guide",
                                    QMessageBox::ActionRole);
        }
        box.exec();
        if (fullBtn && box.clickedButton() == fullBtn) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(helpPath));
        }
        // NOTE: no selection reset here — the QSignalBlocker above already
        // cleared the strip. The old setCurrentRow(0) re-fired the sidebar
        // slot through currentRowChanged and row 0 is "Duplicates", so
        // every Help click also launched the duplicate finder.
    } else if (page == "Stats") {
        QMessageBox::information(this, "Index Statistics",
            QString("Total files: %1\nDatabase size: %2")
                .arg(repo_ ? repo_->totalFiles() : 0)
                .arg([&]{
                    // v1.7.11: include the -wal/-shm sidecars — mid-scan
                    // the WAL can be hundreds of MB, and reporting only
                    // the main .db badly under-stated real disk usage.
                    const QString base = Config::instance().dbPath();
                    qint64 total = 0;
                    const QStringList parts{
                        base,
                        base + QStringLiteral("-wal"),
                        base + QStringLiteral("-shm") };
                    for (const QString& p : parts) {
                        QFile f(p);
                        if (f.exists()) total += f.size();
                    }
                    return Utils::formatFileSize(total);
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
    QList<TodoItem> todo;      // text extraction (inline pipeline)
    QList<TodoItem> ocrTodo;   // v1.7.9: OCR pipeline (needs_ocr + images)
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

        // v1.7.9: gather OCR work — this used to be collected by NOBODY.
        // needs_ocr rows (scanned PDFs, garbled text layers flagged by the
        // extractors) and images (ocr_status 'pending' from the scan) were
        // invisible to this function, so "Extract" said there was nothing
        // to do while those files never became searchable. They run on the
        // OCR worker pool (PDFium page renders + Windows OCR), not the
        // text-extractor pipeline.
        sqlite3_stmt* o = nullptr;
        const char* ocrSql =
            "SELECT id, path, extension FROM Files "
            "WHERE (indexing_status = 'needs_ocr' "
            "       AND extension IN ('pdf','doc','docx',"
            "                        'xls','xlsx','xlsm','ppt','pptx')) "
            "   OR (extension IN ('jpg','jpeg','png','tif','tiff',"
            "                     'bmp','gif','webp') "
            "       AND indexing_status IN ('metadata_only','needs_ocr') "
            "       AND ocr_status IN ('pending','needs_ocr')) "
            "ORDER BY id;";
        if (sqlite3_prepare_v2(raw, ocrSql, -1, &o, nullptr) == SQLITE_OK) {
            while (sqlite3_step(o) == SQLITE_ROW) {
                TodoItem it;
                it.fileId = sqlite3_column_int64(o, 0);
                const unsigned char* p = sqlite3_column_text(o, 1);
                const unsigned char* e = sqlite3_column_text(o, 2);
                it.path = p ? QString::fromUtf8(reinterpret_cast<const char*>(p)) : QString();
                it.ext  = e ? QString::fromUtf8(reinterpret_cast<const char*>(e)) : QString();
                ocrTodo.append(it);
            }
            sqlite3_finalize(o);
        }
    }

    if (todo.isEmpty() && ocrTodo.isEmpty()) {
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
    //
    // v1.7.10 FIRST-RUN MODE: until the very first full extraction drain
    // finishes (settings_.firstRunDone == false), sessions are 200 files
    // and re-arm after 3 s instead of 60 s — a brand-new index extracts
    // itself end-to-end without the user babysitting the Extract button
    // ("on first run how about extract all files?"). The 200 ms per-file
    // pacing is untouched, so stability is preserved.
    const int sessionCap = extractAllMode_ ? 200 : 30;
    const int maxFilesThisSession = qMin(total, sessionCap);
    statusBar()->showMessage(
        QString("Extracting %1 of %2 files... (click Stop Extracting to cancel)")
            .arg(maxFilesThisSession).arg(total));

    // Show progress bar
    if (extractionProgressBar_) {
        extractionProgressBar_->setRange(0, maxFilesThisSession);
        extractionProgressBar_->setValue(0);
        extractionProgressBar_->setVisible(true);
    }

    // v1.7.9: hand the OCR work to the pool. The session stays open until
    // the pool drains (onOcrTaskCompleted finishes it), so the button's
    // "Stop Extracting" cancel covers OCR too.
    ocrReceived_ = 0;
    ocrExpected_ = 0;
    if (ocrPool_ && !ocrTodo.isEmpty()) {
        QList<OcrTask> tasks;
        tasks.reserve(ocrTodo.size());
        for (const TodoItem& it : ocrTodo)
            tasks.append(OcrTask{it.fileId, it.path, it.ext});
        ocrPool_->enqueueBatch(tasks);
        ocrExpected_ = ocrTodo.size();
        statusBar()->showMessage(
            QString("OCR queued for %1 scanned/image file%2...")
                .arg(ocrTodo.size()).arg(ocrTodo.size() == 1 ? "" : "s"), 5000);
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
        // v1.7.10: the database was removed (Settings → Remove Database)
        // while this session ran — kill the session instead of writing
        // into the freshly created database.
        if (dbResetting_) {
            timer->stop();
            timer->deleteLater();
            return;
        }
        // Check cancel flag.
        if (extractCancelFlag_.load()) {
            timer->stop();
            timer->deleteLater();
            if (ocrPool_) ocrPool_->clearQueue();   // v1.7.9: cancel OCR too
            ocrExpected_ = 0;
            ocrReceived_ = 0;
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

            // v1.7.9: OCR work runs in the pool — keep the session open
            // until it drains; the last taskCompleted tears the session
            // down and re-arms auto-extraction for any remaining batches.
            if (ocrWorkOutstanding()) {
                const int left = ocrExpected_ - ocrReceived_
                                 + (ocrPool_ ? ocrPool_->queueSize() : 0);
                statusBar()->showMessage(
                    QString("Text extraction done — OCR running: %1 file(s) "
                            "left (click Stop Extracting to cancel).")
                        .arg(left), 6000);
                if (extractionProgressBar_) {
                    extractionProgressBar_->setRange(0, qMax(1, ocrExpected_));
                    extractionProgressBar_->setValue(ocrReceived_);
                    extractionProgressBar_->setVisible(true);
                }
                return;
            }

            contentExtractionRunning_ = false;
            if (searchBar_) searchBar_->setExtracting(false);  // Phase 1.4
            if (extractionProgressBar_) extractionProgressBar_->setVisible(false);
            updateIndexStats();
            refreshPreviewForSelectedFile();
            if (state->idx >= total) {
                statusBar()->showMessage(
                    QString("Extraction complete: %1 succeeded, %2 failed (out of %3).")
                        .arg(state->done).arg(state->failed).arg(total), 8000);

                // v1.7.10: the first FULL drain is done — first run is
                // over. Persist the flag so later sessions return to the
                // conservative 30-file/60 s cadence. (OCR tasks may still
                // be in flight; they complete on the pool independently.)
                if (extractAllMode_) {
                    extractAllMode_ = false;
                    settings_.firstRunDone = true;
                    saveSettings();
                    DS_INFO("Extract", "First-run extraction drain complete.");
                }

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
                    QString("Extracted %1 of %2 — next batch runs automatically "
                            "%3 (Extract = start now).")
                        .arg(state->done + state->failed).arg(total)
                        .arg(extractAllMode_ ? QStringLiteral("in 3 seconds")
                                             : QStringLiteral("in 1 minute")), 8000);
                // Auto-continue: drain the queue without the user having to
                // click Extract after every 30-file batch. The 200ms per-file
                // pacing keeps UI responsive exactly as before; this only
                // removes the mandatory click between batches.
                // v1.7.10: first-run mode re-arms after 3 s so a new index
                // drains continuously instead of taking minutes per batch.
                QTimer::singleShot(extractAllMode_ ? 3 * 1000 : 60 * 1000,
                                   this, [this]() {
                    // v1.7.4: fresh patience budget for this wake so a scan
                    // that happens to be running can never starve the queue.
                    autoExtractRetryLeft_ = 20;
                    requestAutoExtract();
                });
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

            if (ok && raw) {
                // v1.7.9: write DocumentText EVEN when the extracted text
                // is empty — an empty-but-valid document is DONE; leaving
                // no row made the integrity pass requeue it on every
                // launch. SearchIndex still only gets real content.
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

                if (!extractedText.isEmpty()) {
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

        // v1.7.4: refresh the "N indexed" badge after EVERY file so the
        // counter visibly climbs while extraction runs (the 20 s poll
        // alone read as a frozen number).
        updateIndexStats();

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

    // v1.7.3: the old guard DROPPED the hourly tick silently whenever an
    // extraction session or a full re-index was busy - with long runs that
    // made the scan "never happen". Retry shortly after instead of losing
    // the tick.
    const bool busy = contentExtractionRunning_;
    if (busy) {
        QTimer::singleShot(10 * 60 * 1000, this, [this]{
            autoScanIndexedFolders();
        });
        return;
    }

    // v1.7.3 watchdog: a scan stuck for >30 min (network share gone
    // silent, dead drive) used to block EVERY future scan via
    // autoScanRunning_. Re-arm and let a fresh scan proceed.
    if (autoScanRunning_) {
        const qint64 elapsedMs =
            QDateTime::currentMSecsSinceEpoch() - autoScanStartedMs_;
        if (elapsedMs < 30 * 60 * 1000) return;   // previous scan still OK
        DS_WARN("Scan", "Auto-scan watchdog: previous scan stuck >30 min - re-arming");
        autoScanRunning_ = false;
    }

    // CRITICAL: Only scan the folders the user explicitly added via Settings
    // -> Indexing -> Indexed Drives (see v1.6 notes for why DB-derived
    // folders were wrong).
    if (settings_.indexedDrives.isEmpty()) {
        autoScanRunning_ = false;
        return;
    }

    autoScanRunning_ = true;
    autoScanStartedMs_ = QDateTime::currentMSecsSinceEpoch();
    statusBar()->showMessage("Auto-scanning indexed folders...");

    struct ScanStats {
        int newFiles = 0;
        int updatedFiles = 0;
        int removedFiles = 0;
        int unavailable = 0;
    };
    auto stats = std::make_shared<ScanStats>();

    const QStringList folderList = settings_.indexedDrives;
    // v1.7.11: the hourly walk used to pass an EMPTY exclude list to
    // walkDirectory and never consulted excludedExtensions — so a folder
    // the user explicitly excluded in Settings kept being re-indexed
    // every hour. Both lists now gate the walk; excluded files also drop
    // out of `seen`, so the Pass-2 prune below cleans up rows that were
    // indexed BEFORE the user excluded their folder/extension.
    const QStringList excludedFolders = settings_.excludedFolders;
    const QSet<QString> userExcludedExts =
        normalizedExtSet(settings_.excludedExtensions);
    QString dbPath = Config::instance().dbPath();
    bool hashEnabled = settings_.hashLargeFiles;

    QFuture<void> future = QtConcurrent::run(
        [folderList, excludedFolders, userExcludedExts,
         dbPath, hashEnabled, stats]() {
        sqlite3* workerDb = nullptr;
        if (sqlite3_open_v2(dbPath.toUtf8().constData(), &workerDb,
                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX,
                            nullptr) != SQLITE_OK) {
            return;
        }
        // v1.7.11: the main connection sets busy_timeout (Database::open);
        // this raw worker connection did not, so a concurrent UI-thread
        // write could hand it SQLITE_BUSY and silently skip its work.
        sqlite3_exec(workerDb, "PRAGMA busy_timeout = 5000;",
                     nullptr, nullptr, nullptr);

        for (const auto& folder : folderList) {
            try {
                const QString root = FileUtils::toNative(folder);

                // v1.7.3: if the folder is temporarily unavailable
                // (unplugged drive, disconnected share) DO NOT walk and
                // DO NOT prune - pruning would wipe the entire index for
                // it and the files would have to be re-extracted from
                // scratch on return. Skip and report instead.
                if (!QDir(root).exists()) { ++stats->unavailable; continue; }

                // ---- Pass 1: walk + upsert, remembering what we saw ----
                QSet<QString> seen;              // case-folded native paths
                seen.reserve(1024);
                FileUtils::walkDirectory(folder, excludedFolders,
                    [&](const QFileInfo& fi) -> bool {
                        // v1.7.7: THE extension allowlist gate. Checked
                        // BEFORE the path enters `seen`, so the Pass-2
                        // prune below treats every non-indexable file
                        // (md notes, txt, logs, installers, archives...)
                        // as unseen and deletes any row an older version
                        // indexed — the index self-heals to documents +
                        // images only.
                        // v1.7.11: the user's Excluded Extensions list is
                        // an additional gate on top of the allowlist.
                        const QString ext =
                            FileUtils::extensionOf(fi.absoluteFilePath());
                        if (!Constants::isIndexableExtension(ext))
                            return true;
                        if (userExcludedExts.contains(ext.toLower()))
                            return true;

                        const QString path =
                            FileUtils::toNative(fi.absoluteFilePath());
                        seen.insert(path.toLower());

                        // Read the existing row (if any) so we can tell
                        // "same file, metadata refresh" from "file CHANGED".
                        qint64 oldSize = -1, oldModified = -1;
                        QString oldHash;
                        bool isNew = true;
                        sqlite3_stmt* chk = nullptr;
                        if (sqlite3_prepare_v2(workerDb,
                                "SELECT id, size, modified_date, hash FROM "
                                "Files WHERE path = ?1;",
                                -1, &chk, nullptr) == SQLITE_OK) {
                            sqlite3_bind_text(chk, 1,
                                              path.toUtf8().constData(), -1,
                                              SQLITE_TRANSIENT);
                            if (sqlite3_step(chk) == SQLITE_ROW) {
                                isNew       = false;
                                oldSize     = sqlite3_column_int64(chk, 1);
                                oldModified = sqlite3_column_int64(chk, 2);
                                const unsigned char* h =
                                    sqlite3_column_text(chk, 3);
                                oldHash = h ? QString::fromUtf8(
                                    reinterpret_cast<const char*>(h))
                                            : QString();
                            }
                            sqlite3_finalize(chk);
                        }

                        const qint64 size = fi.size();
                        const qint64 modified =
                            fi.lastModified().toSecsSinceEpoch();
                        // Same size + same mtime = untouched file.
                        const bool changed =
                            !isNew && (size != oldSize ||
                                       modified != oldModified);

                        // (Re)compute the hash only when it can have
                        // changed - brand-new, modified, or rows whose hash
                        // was never computed (backfill so the duplicates
                        // finder stays meaningful). Otherwise preserve the
                        // stored hash: re-hashing every unchanged file on
                        // every scan would hammer the disk for nothing.
                        QString hash;
                        if (!hashEnabled) {
                            hash = oldHash;              // preserve whatever exists
                        } else if (isNew || changed || oldHash.isEmpty()) {
                            hash = FileUtils::sha256OfFile(
                                path, 64 * 1024 * 1024);
                        } else {
                            hash = oldHash;
                        }

                        if (isNew) {
                            sqlite3_stmt* upd = nullptr;
                            sqlite3_prepare_v2(workerDb,
                                "INSERT INTO Files (path, filename, "
                                "  extension, size, created_date, "
                                "  modified_date, hash, indexing_status, "
                                "  ocr_status) "
                                "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)",
                                -1, &upd, nullptr);
                            if (upd) {
                                sqlite3_bind_text(upd, 1,
                                                  path.toUtf8().constData(),
                                                  -1, SQLITE_TRANSIENT);
                                sqlite3_bind_text(upd, 2, fi.fileName()
                                                          .toUtf8()
                                                          .constData(),
                                                  -1, SQLITE_TRANSIENT);
                                sqlite3_bind_text(upd, 3,
                                                  ext.toUtf8().constData(),
                                                  -1, SQLITE_TRANSIENT);
                                sqlite3_bind_int64(upd, 4, size);
                                sqlite3_bind_int64(
                                    upd, 5, fi.birthTime().toSecsSinceEpoch());
                                sqlite3_bind_int64(upd, 6, modified);
                                sqlite3_bind_text(
                                    upd, 7, hash.toUtf8().constData(), -1,
                                    SQLITE_TRANSIENT);
                                sqlite3_bind_text(upd, 8, "metadata_only",
                                                  -1, SQLITE_TRANSIENT);
                                const char* ocrStat =
                                    (Constants::kDocumentExtensions.contains(
                                         ext) ||
                                     Constants::kImageExtensions.contains(ext))
                                        ? "pending" : "not_needed";
                                sqlite3_bind_text(upd, 9, ocrStat, -1,
                                                  SQLITE_TRANSIENT);
                                sqlite3_step(upd);
                                sqlite3_finalize(upd);
                            }
                            ++stats->newFiles;
                        } else if (changed) {
                            // v1.7.3 CRITICAL FIX: the old upsert set
                            // indexing_status='content_done' for EVERY
                            // existing row on EVERY scan - silently
                            // "completing" files that were still queued
                            // (metadata_only) or waiting for OCR
                            // (needs_ocr) without doing any work. That
                            // froze visible progress and read as "hourly
                            // scanning is not happening". Now: refresh
                            // metadata, and ONLY when the file actually
                            // changed re-queue it for extraction/OCR.
                            sqlite3_stmt* upd = nullptr;
                            sqlite3_prepare_v2(workerDb,
                                "UPDATE Files SET filename = ?2, "
                                "  extension = ?3, size = ?4, "
                                "  modified_date = ?6, hash = ?7, "
                                "  indexing_status = 'metadata_only', "
                                "  ocr_status = ?9 "
                                "WHERE id = (SELECT id FROM Files "
                                "            WHERE path = ?1)",
                                -1, &upd, nullptr);
                            if (upd) {
                                sqlite3_bind_text(upd, 1,
                                                  path.toUtf8().constData(),
                                                  -1, SQLITE_TRANSIENT);
                                sqlite3_bind_text(upd, 2, fi.fileName()
                                                          .toUtf8()
                                                          .constData(),
                                                  -1, SQLITE_TRANSIENT);
                                sqlite3_bind_text(upd, 3,
                                                  ext.toUtf8().constData(),
                                                  -1, SQLITE_TRANSIENT);
                                sqlite3_bind_int64(upd, 4, size);
                                sqlite3_bind_int64(upd, 6, modified);
                                sqlite3_bind_text(
                                    upd, 7, hash.toUtf8().constData(), -1,
                                    SQLITE_TRANSIENT);
                                const char* ocrStat =
                                    (Constants::kDocumentExtensions.contains(
                                         ext) ||
                                     Constants::kImageExtensions.contains(ext))
                                        ? "pending" : "not_needed";
                                sqlite3_bind_text(upd, 9, ocrStat, -1,
                                                  SQLITE_TRANSIENT);
                                sqlite3_step(upd);
                                sqlite3_finalize(upd);
                            }
                            ++stats->updatedFiles;
                        } else if (hashEnabled && !hash.isEmpty()) {
                            // Unchanged file: refresh metadata + (preserved
                            // or backfilled) hash. NEVER touch
                            // indexing_status/ocr_status here - queued
                            // (metadata_only), needs-OCR and failed rows
                            // must survive scans untouched.
                            sqlite3_stmt* upd = nullptr;
                            sqlite3_prepare_v2(workerDb,
                                "UPDATE Files SET filename = ?2, "
                                "  extension = ?3, size = ?4, "
                                "  modified_date = ?6, hash = ?7 "
                                "WHERE id = (SELECT id FROM Files "
                                "            WHERE path = ?1)",
                                -1, &upd, nullptr);
                            if (upd) {
                                sqlite3_bind_text(upd, 1,
                                                  path.toUtf8().constData(),
                                                  -1, SQLITE_TRANSIENT);
                                sqlite3_bind_text(upd, 2, fi.fileName()
                                                          .toUtf8()
                                                          .constData(),
                                                  -1, SQLITE_TRANSIENT);
                                sqlite3_bind_text(upd, 3,
                                                  ext.toUtf8().constData(),
                                                  -1, SQLITE_TRANSIENT);
                                sqlite3_bind_int64(upd, 4, size);
                                sqlite3_bind_int64(upd, 6, modified);
                                sqlite3_bind_text(
                                    upd, 7, hash.toUtf8().constData(), -1,
                                    SQLITE_TRANSIENT);
                                sqlite3_step(upd);
                                sqlite3_finalize(upd);
                            }
                        } else {
                            // Unchanged, no hash refresh: filename/ext
                            // metadata only.
                            sqlite3_stmt* upd = nullptr;
                            sqlite3_prepare_v2(workerDb,
                                "UPDATE Files SET filename = ?2, "
                                "  extension = ?3, size = ?4, "
                                "  modified_date = ?6 "
                                "WHERE id = (SELECT id FROM Files "
                                "            WHERE path = ?1)",
                                -1, &upd, nullptr);
                            if (upd) {
                                sqlite3_bind_text(upd, 1,
                                                  path.toUtf8().constData(),
                                                  -1, SQLITE_TRANSIENT);
                                sqlite3_bind_text(upd, 2, fi.fileName()
                                                          .toUtf8()
                                                          .constData(),
                                                  -1, SQLITE_TRANSIENT);
                                sqlite3_bind_text(upd, 3,
                                                  ext.toUtf8().constData(),
                                                  -1, SQLITE_TRANSIENT);
                                sqlite3_bind_int64(upd, 4, size);
                                sqlite3_bind_int64(upd, 6, modified);
                                sqlite3_step(upd);
                                sqlite3_finalize(upd);
                            }
                        }
                        return true;
                    });

                // ---- Pass 2: prune rows the walk did not see ----
                // The FileWatcher only removes rows for deletions that
                // happen WHILE the app runs. Files deleted or moved while
                // it was closed stayed in the index forever - showing up
                // in search results and skewing the duplicates finder and
                // the "N indexed" badge. Reconcile now: any row under this
                // folder whose case-folded path was not seen is gone.
                // (Hidden/system files are also skipped by the walk; a
                // row for one would be pruned and re-added next scan -
                // accepted churn, far better than permanent ghosts.)
                QString prefix = root;
                while (prefix.endsWith('\\')) prefix.chop(1);
                prefix += QLatin1Char('\\');

                QList<qint64> staleIds;
                sqlite3_stmt* q = nullptr;
                if (sqlite3_prepare_v2(workerDb,
                        "SELECT id, path FROM Files "
                        "WHERE upper(substr(path, 1, ?1)) = upper(?2);",
                        -1, &q, nullptr) == SQLITE_OK) {
                    const QByteArray prefixUtf8 = prefix.toUtf8();
                    sqlite3_bind_int(q, 1, prefix.length());
                    sqlite3_bind_text(q, 2, prefixUtf8.constData(),
                                      -1, SQLITE_TRANSIENT);
                    while (sqlite3_step(q) == SQLITE_ROW) {
                        const qint64 id = sqlite3_column_int64(q, 0);
                        const unsigned char* p = sqlite3_column_text(q, 1);
                        const QString rowPath = p
                            ? QString::fromUtf8(
                                  reinterpret_cast<const char*>(p))
                            : QString();
                        if (seen.contains(rowPath.toLower())) continue;
                        staleIds.append(id);
                    }
                    sqlite3_finalize(q);
                }
                if (!staleIds.isEmpty()) {
                    sqlite3_exec(workerDb, "BEGIN;", nullptr, nullptr,
                                 nullptr);
                    for (const qint64 id : staleIds) {
                        // Mirror FileRepository::deleteFile: cascade
                        // handles Tags/Notes/DocumentText; SearchIndex and
                        // BgeEmbeddings need explicit deletes.
                        static const char* kDelSql[] = {
                            "DELETE FROM Files WHERE id = ?1;",
                            "DELETE FROM SearchIndex WHERE file_id = ?1;",
                            "DELETE FROM BgeEmbeddings WHERE file_id = ?1;",
                        };
                        for (const char* sql : kDelSql) {
                            sqlite3_stmt* d = nullptr;
                            if (sqlite3_prepare_v2(workerDb, sql, -1, &d,
                                                   nullptr) == SQLITE_OK) {
                                sqlite3_bind_int64(d, 1, id);
                                sqlite3_step(d);
                                sqlite3_finalize(d);
                            }
                        }
                        ++stats->removedFiles;
                    }
                    sqlite3_exec(workerDb, "COMMIT;", nullptr, nullptr,
                                 nullptr);
                }
            } catch (...) {}
        }

        sqlite3_close(workerDb);
    });

    auto* watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this,
            [this, watcher, stats]() {
        autoScanRunning_ = false;
        updateIndexStats();

        const QString unavailableNote = stats->unavailable > 0
            ? QStringLiteral(" (%1 folder%2 unavailable)")
                  .arg(stats->unavailable)
                  .arg(stats->unavailable == 1 ? "" : "s")
            : QString();

        if (stats->newFiles > 0 || stats->updatedFiles > 0) {
            statusBar()->showMessage(
                QStringLiteral("Auto-scan complete: %1 new, %2 changed, %3 "
                               "removed%4")
                    .arg(stats->newFiles)
                    .arg(stats->updatedFiles)
                    .arg(stats->removedFiles)
                    .arg(unavailableNote),
                8000);
            // Only wake the extraction pipeline when there is actual work;
            // waking it on every idle tick was pure noise.
            QTimer::singleShot(500, this, [this]() {
                autoExtractRetryLeft_ = 20;  // fresh budget (see requestAutoExtract)
                requestAutoExtract();
            });
        } else {
            statusBar()->showMessage(
                QStringLiteral("Auto-scan complete: index up to date "
                               "(%1 removed%2)")
                    .arg(stats->removedFiles).arg(unavailableNote),
                5000);
        }
        watcher->deleteLater();
    });
    watcher->setFuture(future);
}

// ============================================================
// v1.7.4: AUTO extraction wake (never cancels, yields to scans)
// ============================================================
void MainWindow::requestAutoExtract() {
    if (!repo_ || !db_) return;
    // A run already in flight — the Extract button is its "Stop Extracting"
    // control. Auto-wakes must NEVER touch the cancel flag (the old inline
    // auto-wakes called onExtract() directly, and two wakes landing close
    // together meant the second one CANCELLED the run the first had just
    // started).
    if (contentExtractionRunning_) return;
    // The startup/hourly scan walks the very files we would extract and
    // writes to its own DB connection. Rather than racing it, wait; its
    // finished handler wakes extraction when work exists anyway.
    if (autoScanRunning_) {
        if (autoExtractRetryLeft_ > 0) {
            --autoExtractRetryLeft_;
            QTimer::singleShot(30 * 1000, this, [this]() {
                requestAutoExtract();
            });
        }
        return;
    }
    onExtract();
}

// ============================================================
// v1.7.9: OCR pool results → DB. DocumentText is ALWAYS written on
// success (empty OCR text = genuinely blank image — done, never
// re-OCRed); SearchIndex only receives real content. The session
// accounting here ends the extraction session once the last queued
// task lands, and re-arms auto-extraction for remaining 30-file text
// batches.
// ============================================================
void MainWindow::onOcrTaskCompleted(qint64 fileId, const QString& text, bool ok) {
    // v1.7.10: the database is being removed/rebuilt — this is a late
    // result for a row that no longer exists. Drop it instead of writing
    // stale text into the fresh database.
    if (dbResetting_) return;
    if (!db_ || fileId <= 0) return;
    sqlite3* raw = db_->raw();
    if (!raw) return;

    QString t = text;
    if (t.size() > Constants::kMaxExtractTextChars)
        t = t.left(Constants::kMaxExtractTextChars) +
            QStringLiteral("\n\n[... text truncated for memory ...]");

    if (ok) {
        const QByteArray textBytes = t.toUtf8();
        const QByteArray srcBytes  = QByteArray("ocr");
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
            sqlite3_bind_int64(upd, 1, fileId);
            sqlite3_bind_text(upd, 2, textBytes.constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(upd, 3, srcBytes.constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(upd, 4, t.size());
            sqlite3_bind_int64(upd, 5, QDateTime::currentSecsSinceEpoch());
            sqlite3_step(upd);
            sqlite3_finalize(upd);
        }
        sqlite3_exec(raw,
            QString("UPDATE Files SET indexing_status='content_done', "
                    "ocr_status='not_needed' WHERE id=%1;")
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

        if (!t.isEmpty()) {
            QString fn, pth, ext;
            sqlite3_stmt* f = nullptr;
            if (sqlite3_prepare_v2(raw,
                    "SELECT filename, path, extension FROM Files WHERE id=?1;",
                    -1, &f, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(f, 1, fileId);
                if (sqlite3_step(f) == SQLITE_ROW) {
                    const unsigned char* a = sqlite3_column_text(f, 0);
                    const unsigned char* b = sqlite3_column_text(f, 1);
                    const unsigned char* c = sqlite3_column_text(f, 2);
                    fn  = a ? QString::fromUtf8(reinterpret_cast<const char*>(a)) : QString();
                    pth = b ? QString::fromUtf8(reinterpret_cast<const char*>(b)) : QString();
                    ext = c ? QString::fromUtf8(reinterpret_cast<const char*>(c)) : QString();
                }
                sqlite3_finalize(f);
            }
            sqlite3_stmt* ins = nullptr;
            sqlite3_prepare_v2(raw,
                "INSERT INTO SearchIndex (filename, content, path, extension, file_id) "
                "VALUES (?1, ?2, ?3, ?4, ?5);",
                -1, &ins, nullptr);
            if (ins) {
                const QByteArray fnb  = fn.toUtf8();
                const QByteArray pthb = pth.toUtf8();
                const QByteArray extb = ext.toUtf8();
                sqlite3_bind_text(ins, 1, fnb.constData(),  -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(ins, 2, textBytes.constData(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(ins, 3, pthb.constData(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(ins, 4, extb.constData(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(ins, 5, fileId);
                sqlite3_step(ins);
                sqlite3_finalize(ins);
            }
        }
    } else {
        // OCR failed (no language packs, unreadable image...). 'failed'
        // is honest; the next content change re-queues the file.
        sqlite3_exec(raw,
            QString("UPDATE Files SET indexing_status='failed' WHERE id=%1;")
                .arg(fileId).toUtf8().constData(),
            nullptr, nullptr, nullptr);
    }

    updateIndexStats();  // badge climbs while OCR runs (same as text path)

    // Session accounting — a cancel/reset already cleared the counters,
    // so late results from a cancelled queue land here harmlessly.
    if (contentExtractionRunning_ && ocrExpected_ > 0 &&
        ocrReceived_ < ocrExpected_) {
        ++ocrReceived_;
        if (extractionProgressBar_) {
            extractionProgressBar_->setRange(0, qMax(1, ocrExpected_));
            extractionProgressBar_->setValue(ocrReceived_);
        }
        if (ocrReceived_ >= ocrExpected_ &&
            (!ocrPool_ || ocrPool_->queueSize() == 0)) {
            const int finished = ocrExpected_;
            contentExtractionRunning_ = false;
            extractCancelFlag_.store(false);
            ocrExpected_ = 0;
            ocrReceived_ = 0;
            if (searchBar_) searchBar_->setExtracting(false);
            if (extractionProgressBar_) extractionProgressBar_->setVisible(false);
            updateIndexStats();
            refreshPreviewForSelectedFile();
            statusBar()->showMessage(
                QString("OCR complete (%1 file%2) — checking for more work...")
                    .arg(finished).arg(finished == 1 ? "" : "s"), 6000);
            // Re-arm extraction: any remaining text batches continue.
            QTimer::singleShot(1500, this, [this]() {
                autoExtractRetryLeft_ = 20;
                requestAutoExtract();
            });
        }
    }
}

bool MainWindow::ocrWorkOutstanding() const {
    return ocrExpected_ > 0 &&
           (ocrReceived_ < ocrExpected_ ||
            (ocrPool_ && ocrPool_->queueSize() > 0));
}

// ============================================================
// v1.7.9: STARTUP INTEGRITY PASS (t+6.5 s, window already visible).
// (1) Requeue FAKE-DONE documents: scans older than v1.7.3 stamped
//     indexing_status='content_done' onto every row on every pass
//     WITHOUT extracting; those rows looked finished forever, "Extract"
//     reported nothing to do, and content search stayed empty. A
//     document is only truly done when its text exists in DocumentText.
// (2) Backfill missing hashes (bounded per launch, respects the
//     Settings switch) so the duplicates finder has something to
//     group — leftover rows are hashed on the next launch or by the
//     hourly scan, which backfills too.
// ============================================================
void MainWindow::runStartupIntegrityPass() {
    if (!db_) return;
    sqlite3* raw = db_->raw();
    if (!raw) return;

    sqlite3_exec(raw,
        "UPDATE Files SET indexing_status='metadata_only' "
        "WHERE indexing_status='content_done' "
        "AND extension IN ('pdf','doc','docx','xls','xlsx','xlsm',"
        "'ppt','pptx') "
        "AND id NOT IN (SELECT file_id FROM DocumentText);",
        nullptr, nullptr, nullptr);
    const int requeued = sqlite3_changes(raw);

    // v1.7.10: give FAILED rows one honest retry per launch. Rotated
    // scans that OCR'd under the broken auto-orientation (or while no
    // OCR language pack was installed) are parked at 'failed' forever —
    // nothing ever re-queued them, so "OCR returning nothing" stuck
    // even after the engine improved. Documents drop back to
    // metadata_only (the text pipeline re-runs); images get their OCR
    // status reset to pending (the pool re-runs). Bounded at 500 per
    // launch so a folder of permanently unreadable files can't stall
    // startup; rows that fail again simply wait for the next launch.
    int failedRequeued = 0;
    {
        QList<qint64> failIds;
        sqlite3_stmt* fq = nullptr;
        if (sqlite3_prepare_v2(raw,
                "SELECT id, extension FROM Files "
                "WHERE indexing_status='failed' "
                "AND extension IN ('pdf','doc','docx','xls','xlsx','xlsm',"
                "'ppt','pptx','jpg','jpeg','png','tif','tiff',"
                "'bmp','gif','webp') "
                "AND id NOT IN (SELECT file_id FROM DocumentText) "
                "LIMIT 500;",
                -1, &fq, nullptr) == SQLITE_OK) {
            while (sqlite3_step(fq) == SQLITE_ROW)
                failIds.append(sqlite3_column_int64(fq, 0));
            sqlite3_finalize(fq);
        }
        for (const qint64 id : failIds) {
            sqlite3_exec(raw,
                QString("UPDATE Files SET indexing_status='metadata_only', "
                        "ocr_status='pending' WHERE id=%1;").arg(id)
                    .toUtf8().constData(),
                nullptr, nullptr, nullptr);
            ++failedRequeued;
            if ((failedRequeued % 100) == 0)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 8);
        }
    }

    int hashed = 0;
    if (settings_.hashLargeFiles) {
        QList<qint64> ids;
        QStringList paths;
        sqlite3_stmt* q = nullptr;
        if (sqlite3_prepare_v2(raw,
                "SELECT id, path FROM Files "
                "WHERE (hash IS NULL OR hash = '') "
                "AND extension IN ('pdf','doc','docx','xls','xlsx','xlsm',"
                "'ppt','pptx','jpg','jpeg','png','tif','tiff',"
                "'bmp','gif','webp') "
                "LIMIT 4000;",
                -1, &q, nullptr) == SQLITE_OK) {
            while (sqlite3_step(q) == SQLITE_ROW) {
                ids.append(sqlite3_column_int64(q, 0));
                const unsigned char* p = sqlite3_column_text(q, 1);
                paths.append(p ? QString::fromUtf8(
                    reinterpret_cast<const char*>(p)) : QString());
            }
            sqlite3_finalize(q);
        }
        for (int i = 0; i < ids.size(); ++i) {
            const QString h =
                FileUtils::sha256OfFile(paths.at(i), 64 * 1024 * 1024);
            if (h.isEmpty()) continue;   // unreadable now — retry next launch
            sqlite3_stmt* u = nullptr;
            if (sqlite3_prepare_v2(raw,
                    "UPDATE Files SET hash=?1 WHERE id=?2;",
                    -1, &u, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(u, 1, h.toUtf8().constData(), -1,
                                  SQLITE_TRANSIENT);
                sqlite3_bind_int64(u, 2, ids.at(i));
                sqlite3_step(u);
                sqlite3_finalize(u);
                ++hashed;
            }
            if ((i % 25) == 0)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 8);
        }
    }

    if (requeued > 0 || hashed > 0 || failedRequeued > 0) {
        DS_INFO("Index", QString("Startup integrity pass: %1 fake-done rows "
                                 "requeued for extraction, %2 failed rows "
                                 "retried, %3 hashes backfilled.")
                             .arg(requeued).arg(failedRequeued).arg(hashed));
        statusBar()->showMessage(
            QString("Index repair: %1 file%2 requeued for extraction, "
                    "%3 hash%4 computed.")
                .arg(requeued + failedRequeued)
                .arg(requeued + failedRequeued == 1 ? "" : "s")
                .arg(hashed)
                .arg(hashed == 1 ? "" : "es"), 10000);
        updateIndexStats();
        if (requeued > 0 || failedRequeued > 0) {
            // Fresh work exists — wake extraction (requestAutoExtract
            // yields on its own if the startup scan is still running).
            autoExtractRetryLeft_ = 20;
            QTimer::singleShot(2500, this, [this]() { requestAutoExtract(); });
        }
    }
}

// ============================================================
// v1.7.4: purge every index row under a folder (Settings removal)
// ============================================================
void MainWindow::purgeFolderFromIndex(const QString& folder) {
    if (!db_) return;
    sqlite3* raw = db_->raw();
    if (!raw) return;

    QString prefix = FileUtils::toNative(folder);
    while (prefix.endsWith('\\')) prefix.chop(1);
    if (prefix.isEmpty()) return;
    prefix += QLatin1Char('\\');

    QList<qint64> ids;
    sqlite3_stmt* q = nullptr;
    if (sqlite3_prepare_v2(raw,
            "SELECT id FROM Files "
            "WHERE upper(substr(path, 1, ?1)) = upper(?2);",
            -1, &q, nullptr) == SQLITE_OK) {
        const QByteArray prefixUtf8 = prefix.toUtf8();
        sqlite3_bind_int(q, 1, prefix.length());
        sqlite3_bind_text(q, 2, prefixUtf8.constData(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(q) == SQLITE_ROW) {
            ids.append(sqlite3_column_int64(q, 0));
        }
        sqlite3_finalize(q);
    }
    if (ids.isEmpty()) return;

    // Mirror FileRepository::deleteFile (Files + SearchIndex +
    // BgeEmbeddings + EmbeddingChunks; cascades cover Tags/Notes/Text).
    sqlite3_exec(raw, "BEGIN;", nullptr, nullptr, nullptr);
    static const char* kDelSql[] = {
        "DELETE FROM Files WHERE id = ?1;",
        "DELETE FROM SearchIndex WHERE file_id = ?1;",
        "DELETE FROM BgeEmbeddings WHERE file_id = ?1;",
        "DELETE FROM EmbeddingChunks WHERE file_id = ?1;",
    };
    for (const qint64 id : ids) {
        for (const char* sql : kDelSql) {
            sqlite3_stmt* d = nullptr;
            if (sqlite3_prepare_v2(raw, sql, -1, &d, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(d, 1, id);
                sqlite3_step(d);
                sqlite3_finalize(d);
            }
        }
    }
    sqlite3_exec(raw, "COMMIT;", nullptr, nullptr, nullptr);

    updateIndexStats();
    DS_INFO("Settings", QString("Purged %1 index rows under removed folder %2")
                          .arg(ids.size()).arg(folder));
}

// ============================================================
// v1.7.4: self-healing purge of rows whose file is gone
// ============================================================
int MainWindow::purgeStaleRows(const QStringList& paths, const QString& context) {
    if (!repo_ || paths.isEmpty()) return 0;
    int purged = 0;
    for (const QString& path : paths) {
        if (path.isEmpty()) continue;
        // The CALLER has already decided this row is purgeable (file
        // missing AND storage root reachable). Double-check defensively:
        // if the file reappeared between check and purge, keep it.
        if (QFileInfo::exists(path)) continue;
        if (repo_->deleteByPath(path)) ++purged;
    }
    if (purged > 0) {
        updateIndexStats();
        DS_INFO("Index", QString("[%1] purged %2 stale index row(s)")
                             .arg(context).arg(purged));
    }
    return purged;
}

// ============================================================
// v1.7.7: one-time cleanup of every row whose extension is not in
// kIndexableExtensions. Older versions indexed everything they
// walked — Markdown notes, installers, archives, DLLs — so existing
// databases carry thousands of rows the app can neither open, nor
// preview, nor meaningfully search. This purge (plus the allowlist
// gates on ALL ingest paths) makes "results only ever show
// documents and images" true immediately, without waiting for a
// full re-scan. Unconditional by design: a gated walk can never
// re-add these rows, so even an offline drive loses nothing that
// would come back.
// ============================================================
int MainWindow::purgeNonIndexableRows() {
    if (!db_) return 0;
    sqlite3* raw = db_->raw();
    if (!raw) return 0;

    QString list;
    for (const QString& ext : Constants::kIndexableExtensions) {
        if (!list.isEmpty()) list += QLatin1Char(',');
        list += QString("'%1'").arg(ext.toLower());
    }

    QList<qint64> ids;
    sqlite3_stmt* q = nullptr;
    // NULL-extension rows (should not exist, but older scans could write
    // them) must purge too — "NOT IN" alone would keep them via 3-valued
    // logic, so the IS NULL case is spelled out.
    const QString sql = QString(
        "SELECT id FROM Files "
        "WHERE extension IS NULL "
        "   OR lower(trim(extension)) NOT IN (%1);").arg(list);
    if (sqlite3_prepare_v2(raw, sql.toUtf8().constData(), -1,
                           &q, nullptr) == SQLITE_OK) {
        while (sqlite3_step(q) == SQLITE_ROW)
            ids.append(sqlite3_column_int64(q, 0));
        sqlite3_finalize(q);
    }
    if (ids.isEmpty()) return 0;

    // v1.7.8 REWRITE — this used to run INSIDE the constructor and was
    // the reason the app could look stuck on the splash forever:
    //   • it re-PREPARED the same 4 DELETE statements for EVERY row
    //     (4 × N prepares — and N was huge on indexes built by versions
    //     that indexed every file type they walked),
    //   • it deleted ALL rows in ONE transaction — kill the app mid-purge
    //     and everything rolled back, so the next launch started the
    //     identical purge from zero (a "stuck at splash" loop),
    //   • it never pumped the event loop, so the splash froze.
    // Now: each statement is prepared ONCE, rows are committed in
    // 500-row batches (progress survives a kill), and the event loop is
    // pumped between batches. It is also scheduled AFTER the window is
    // visible, so startup can never block on it.
    static const char* kDelSql[] = {
        "DELETE FROM Files WHERE id = ?1;",
        "DELETE FROM SearchIndex WHERE file_id = ?1;",
        "DELETE FROM BgeEmbeddings WHERE file_id = ?1;",
        "DELETE FROM EmbeddingChunks WHERE file_id = ?1;",
    };
    sqlite3_stmt* del[4] = {nullptr, nullptr, nullptr, nullptr};
    bool prepared = true;
    for (int i = 0; i < 4 && prepared; ++i)
        prepared = sqlite3_prepare_v2(raw, kDelSql[i], -1,
                                      &del[i], nullptr) == SQLITE_OK;
    if (!prepared) {
        for (sqlite3_stmt* d : del) if (d) sqlite3_finalize(d);
        return 0;
    }

    constexpr int kPurgeBatch = 500;
    int purged = 0;
    const qsizetype total = ids.size();
    for (qsizetype start = 0; start < total; start += kPurgeBatch) {
        const qsizetype end = qMin(start + kPurgeBatch, total);
        sqlite3_exec(raw, "BEGIN;", nullptr, nullptr, nullptr);
        for (qsizetype i = start; i < end; ++i) {
            for (sqlite3_stmt* d : del) {
                sqlite3_reset(d);
                sqlite3_clear_bindings(d);
                sqlite3_bind_int64(d, 1, ids.at(i));
                sqlite3_step(d);
            }
        }
        sqlite3_exec(raw, "COMMIT;", nullptr, nullptr, nullptr);
        purged = end;

        // Keep the UI alive during a long cleanup — the window is
        // already on screen by the time this runs (v1.7.8 scheduling).
        QCoreApplication::processEvents(QEventLoop::AllEvents, 8);
    }
    for (sqlite3_stmt* d : del) if (d) sqlite3_finalize(d);

    DS_INFO("Index", QString("Startup purge: removed %1 non-document "
                             "index entries (md/txt/exe/archive/...)")
                         .arg(purged));
    updateIndexStats();
    statusBar()->showMessage(
        QString("Index cleanup: removed %1 non-document entries "
                "(md, txt, exe, archives...) — only documents and "
                "images are indexed now.").arg(purged), 10000);
    return purged;
}

// ============================================================
// v1.7.10: REMOVE DATABASE (Settings → Backup & Restore tab).
// The user asked for a clean-slate option: wipe the index database and
// start over. Flow — cancel in-flight extraction/OCR, close the DB,
// delete docusearch.db (+ WAL/SHM sidecars), reopen a fresh file,
// re-create the schema, and kick the auto-scan so the configured
// folders re-index immediately. dbResetting_ makes any late OCR result
// or extraction-timer tick a no-op instead of writing into the new
// (empty) database.
// ============================================================
void MainWindow::removeAndRebuildDatabase() {
    if (!db_) return;

    // 1) Stop everything that could touch the database.
    extractCancelFlag_.store(true);
    if (ocrPool_) ocrPool_->clearQueue();
    ocrExpected_ = 0;
    ocrReceived_ = 0;
    contentExtractionRunning_ = false;
    if (searchBar_) searchBar_->setExtracting(false);
    if (extractionProgressBar_) extractionProgressBar_->setVisible(false);
    dbResetting_ = true;   // late taskCompleted / timer ticks become no-ops

    // 2) Close and delete.
    const QString dbPath = Config::instance().dbPath();
    db_->close();
    bool removed = true;
    QStringList failedRemovals;
    for (const QString& f : { dbPath, dbPath + QStringLiteral("-wal"),
                              dbPath + QStringLiteral("-shm") }) {
        QFileInfo info(f);
        if (!info.exists()) continue;
        if (QFile::remove(f)) {
            DS_INFO("Database", "Removed " + f);
        } else {
            removed = false;
            failedRemovals << f;
            DS_WARN("Database", "Could not remove " + f);
        }
    }

    // 3) Reopen + schema.
    QString err;
    if (!db_->open(dbPath, &err)) {
        dbResetting_ = false;
        DS_ERROR("Database", "Reopen after remove failed: " + err);
        QMessageBox::critical(this, "Remove Database",
            QStringLiteral("The database was removed but reopening "
                           "failed:\n\n%1\n\nRestart DocuSearch — a fresh "
                           "database will be created.").arg(err));
        return;
    }
    Schema::initialize(*db_);

    dbResetting_ = false;

    // 4) Refresh every cache that mirrors index content.
    updateIndexStats();
    refreshSavedSearches();
    refreshPreviewForSelectedFile();

    if (!removed) {
        QMessageBox::warning(this, "Remove Database",
            QStringLiteral("The database was reset, but these files could "
                           "not be deleted (still in use by another "
                           "program?):\n\n%1")
                .arg(failedRemovals.join(QStringLiteral("\n"))));
    }

    statusBar()->showMessage(
        "Database removed. Index is empty — the configured folders are "
        "being re-scanned now.", 10000);
    DS_INFO("Database", "Database removed and recreated (reset).");

    // 5) Rebuild: rescan the configured folders, then wake extraction so
    // the fresh index fills up again on its own.
    autoScanIndexedFolders();
    autoExtractRetryLeft_ = 20;
    QTimer::singleShot(4000, this, [this]() { requestAutoExtract(); });
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
    // Translucent tooltip windows — REAL rounded corners.
    // Qt creates tooltips as QTipLabel: a square native top-level window.
    // Our QSS draws a border-radius on it, but without translucency the
    // pixels outside the radius show the raw ToolTipBase fill, so every
    // corner still looked clipped/square. Flipping the window translucent
    // here (before Qt creates its native window — QEvent::Show arrives
    // first) makes everything outside the QSS radius fully transparent.
    if (e->type() == QEvent::Show && obj->isWidgetType()) {
        auto* w = static_cast<QWidget*>(obj);
        if (w->windowType() == Qt::ToolTip && w->inherits("QTipLabel")
            && !w->testAttribute(Qt::WA_TranslucentBackground)) {
            w->setAttribute(Qt::WA_TranslucentBackground);
        }
    }
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

        // v1.7.11 lifetime safety: keep the future — the lambda captures
        // `this` and dereferences bgeService_, so ~MainWindow must join
        // it before members are destroyed (exit during model load was a
        // use-after-free window).
        bgeInitFuture_ = QtConcurrent::run([this, dbPath, modelPath]() {
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
    // CRITICAL FIX: attach the service to the hybrid engine BEFORE the
    // auto-toggle below. setChecked(true) fires onSemanticToggled()
    // synchronously, and setSemanticEnabled(true) used to evaluate while
    // the engine's service pointer was still null — permanently disabling
    // semantic search for the whole session (every query keyword-only,
    // "AI refined 0", plus a misleading "chunk index building" banner).
    if (hybridSearch_) {
        hybridSearch_->setBgeService(bgeService_.get());
        hybridSearch_->setSemanticEnabled(semanticEnabled_);
    }
    if (aiSwitch_) {
        aiSwitch_->setEnabled(true);
        // Phase 1.3: Auto-enable AI when BGE is ready.
        // Users shouldn't have to manually find and click the AI toggle.
        if (!aiSwitch_->isChecked()) {
            aiSwitch_->setChecked(true);
        }
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
    if (aiBackfillRunning_ || embeddingRebuildPurging_
        || !bgeService_ || !bgeService_->isReady() || !db_)
        return;
    sqlite3* raw = db_->raw();
    if (!raw) return;

    // ── Phase 0 (v1.7.14): stale-embedding invalidation ─────────────
    // Extraction can rewrite DocumentText for a file that ALREADY has AI
    // vectors (file edited and re-extracted, OCR re-run, integrity
    // requeue). The backfill below only ever selected files with NO
    // embedding row, so vectors computed from the OLD text were searched
    // forever — semantic results silently drifted away from the file's
    // real content. Files whose text is newer than their whole-document
    // embedding get every vector row dropped here; Phase A below then
    // re-enqueues them in this very pass, so the index heals itself with
    // no user action. Bounded per tick to keep the tick cheap.
    {
        sqlite3_stmt* sel = nullptr;
        sqlite3_prepare_v2(raw,
            "SELECT dt.file_id "
            "FROM DocumentText dt "
            "JOIN BgeEmbeddings e ON e.file_id = dt.file_id "
            "WHERE dt.updated_at > e.updated_at "
            "LIMIT 200;",
            -1, &sel, nullptr);
        if (sel) {
            QList<int> stale;
            while (sqlite3_step(sel) == SQLITE_ROW) {
                stale.append(static_cast<int>(sqlite3_column_int64(sel, 0)));
            }
            sqlite3_finalize(sel);
            if (!stale.isEmpty()) {
                bool ok = true;
                sqlite3_stmt* delE = nullptr;
                sqlite3_stmt* delC = nullptr;
                sqlite3_prepare_v2(raw,
                    "DELETE FROM BgeEmbeddings WHERE file_id = ?1;",
                    -1, &delE, nullptr);
                sqlite3_prepare_v2(raw,
                    "DELETE FROM EmbeddingChunks WHERE file_id = ?1;",
                    -1, &delC, nullptr);
                db_->begin();
                for (const int fid : stale) {
                    if (delE) {
                        sqlite3_bind_int64(delE, 1, fid);
                        if (sqlite3_step(delE) != SQLITE_DONE) ok = false;
                        sqlite3_reset(delE);
                    }
                    if (delC) {
                        sqlite3_bind_int64(delC, 1, fid);
                        if (sqlite3_step(delC) != SQLITE_DONE) ok = false;
                        sqlite3_reset(delC);
                    }
                }
                if (ok) db_->commit(); else db_->rollback();
                if (delE) sqlite3_finalize(delE);
                if (delC) sqlite3_finalize(delC);
                if (ok) {
                    DS_INFO("BGE", QString(
                        "Invalidated %1 stale embedding set(s) whose "
                        "extracted text changed — re-embedding now.")
                        .arg(stale.size()));
                }
            }
        }
    }

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
            "ORDER BY dt.file_id LIMIT 300;",
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

void MainWindow::startEmbeddingRebuild() {
    if (!bgeService_ || !bgeService_->isReady() || !db_) {
        QMessageBox::information(this, "AI Search",
            "AI search is not ready, so there is nothing to rebuild.\n\n"
            "Make sure the AI model is installed at:\n"
            "  models/bge-small-en-v1.5/model.onnx\n"
            "  models/bge-small-en-v1.5/vocab.txt");
        return;
    }
    if (aiBackfillRunning_ || embeddingRebuildPurging_) {
        statusBar()->showMessage(
            "AI is already working — wait for the current batch to "
            "finish, then rebuild.", 6000);
        return;
    }
    sqlite3* raw = db_->raw();
    if (!raw) return;

    // Only run when there is something to rebuild; otherwise the user
    // would watch a silent purge that accomplishes nothing.
    qint64 existing = 0;
    sqlite3_stmt* s = nullptr;
    if (sqlite3_prepare_v2(raw,
            "SELECT (SELECT COUNT(*) FROM EmbeddingChunks)"
            "     + (SELECT COUNT(*) FROM BgeEmbeddings);",
            -1, &s, nullptr) == SQLITE_OK) {
        if (sqlite3_step(s) == SQLITE_ROW)
            existing = sqlite3_column_int64(s, 0);
        sqlite3_finalize(s);
    }
    if (existing == 0) {
        statusBar()->showMessage(
            "No embeddings stored yet — use 'Generate AI Embeddings for "
            "All Documents' instead.", 6000);
        return;
    }

    embeddingRebuildPurging_ = true;
    embeddingRebuildRetries_ = 0;
    statusBar()->showMessage(QString(
        "AI: rebuilding embeddings — clearing %1 stored row%2 ...")
        .arg(existing).arg(existing == 1 ? "" : "s"));
    QTimer::singleShot(25, this, [this]() { purgeEmbeddingsTick(); });
}

void MainWindow::purgeEmbeddingsTick() {
    if (!db_ || !embeddingRebuildPurging_) return;
    sqlite3* raw = db_->raw();
    if (!raw) {
        embeddingRebuildPurging_ = false;
        return;
    }

    // Chunks first so phase B of the backfill sees a clean slate the
    // moment doc-level embeddings start refilling. DELETE with LIMIT
    // is not compiled into every SQLite build, so batch via a subquery
    // instead — portable and just as fast at these sizes.
    static const char* const kDeletes[] = {
        "DELETE FROM EmbeddingChunks WHERE chunk_id IN "
        "(SELECT chunk_id FROM EmbeddingChunks LIMIT 1000);",
        "DELETE FROM BgeEmbeddings WHERE file_id IN "
        "(SELECT file_id FROM BgeEmbeddings LIMIT 500);"
    };
    int deleted = 0;
    bool sqlError = false;
    for (const char* sql : kDeletes) {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(raw, sql, -1, &st, nullptr) == SQLITE_OK
            && sqlite3_step(st) == SQLITE_DONE) {
            deleted += sqlite3_changes(raw);
        } else {
            sqlError = true;   // transient lock / I/O hiccup
        }
        if (st) sqlite3_finalize(st);
    }

    if (sqlError) {
        // busy_timeout (5-10 s) makes this nearly impossible; still,
        // never abandon the chain on the first hiccup — retry a while,
        // then give up loudly rather than half-purging in silence.
        if (++embeddingRebuildRetries_ <= 50) {
            QTimer::singleShot(100, this,
                [this]() { purgeEmbeddingsTick(); });
            return;
        }
        embeddingRebuildPurging_ = false;
        statusBar()->showMessage(
            "AI rebuild stopped — the database stayed locked. Close other "
            "DocuSearch windows and try again.", 8000);
        return;
    }
    embeddingRebuildRetries_ = 0;

    if (deleted > 0) {
        qint64 remaining = 0;
        sqlite3_stmt* s = nullptr;
        if (sqlite3_prepare_v2(raw,
                "SELECT (SELECT COUNT(*) FROM EmbeddingChunks)"
                "     + (SELECT COUNT(*) FROM BgeEmbeddings);",
                -1, &s, nullptr) == SQLITE_OK) {
            if (sqlite3_step(s) == SQLITE_ROW)
                remaining = sqlite3_column_int64(s, 0);
            sqlite3_finalize(s);
        }
        statusBar()->showMessage(QString(
            "AI: clearing old embeddings — %1 row%2 left ...")
            .arg(remaining).arg(remaining == 1 ? "" : "s"));
        QTimer::singleShot(25, this,
            [this]() { purgeEmbeddingsTick(); });
        return;
    }

    // Purge complete — hand over to the standard two-phase backfill.
    // Every remaining DocumentText row now lacks an embedding, so the
    // existing chain (doc-level first, then chunks) rebuilds the whole
    // library from FULL document text with live status-chip progress.
    embeddingRebuildPurging_ = false;
    statusBar()->showMessage(
        "AI: old embeddings cleared — rebuilding from full document "
        "text. Progress: 'Embedding documents: X/Y'.", 8000);
    ensureEmbeddingsBackfill();
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
        // 25 ms gap: just enough for the event loop to breathe between
        // batches. The old 250 ms delay added up over dozens of batches
        // and stretched the backlog out for no benefit — inference runs
        // on the worker pool either way, so the UI stays responsive.
        QTimer::singleShot(25, this, [this]() { ensureEmbeddingsBackfill(); });
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
            // v1.7.11: WAL mode keeps -wal/-shm siblings next to the db that
            // can hold hundreds of MB mid-scan; a "Total size" that ignored
            // them under-reported exactly when the index was busiest.
            const QString dbPath = Config::instance().dbPath();
            QFile f(dbPath);
            if (f.exists()) dbSize = f.size();
            QFile wal(dbPath + QStringLiteral("-wal"));
            if (wal.exists()) dbSize += wal.size();
            QFile shm(dbPath + QStringLiteral("-shm"));
            if (shm.exists()) dbSize += shm.size();
        }
        if (indexedInfoLbl_) {
            // "Indexed" counts documents actually searchable (content
            // extracted or metadata staged). The old figure was totalFiles()
            // — every row in Files, including skipped formats and rows
            // whose file is already gone — so the number barely moved and
            // read as hard coded. This one changes as work progresses.
            const qint64 indexedNow = contentDone + metaOnly;

            // v1.7.14: full breakdown — Indexed / Extracted / Embedded.
            // One extra single-round-trip query per stats tick (20 s);
            // both counts are plain COUNT(*)-class aggregates.
            qint64 extracted = 0, embedded = 0;
            if (repo_->countExtractedAndEmbedded(extracted, embedded)) {
                if (extractedInfoLbl_) {
                    extractedInfoLbl_->setText(
                        QString("Extracted %1")
                            .arg(QLocale::c().toString(extracted)));
                }
                if (embeddedInfoLbl_) {
                    embeddedInfoLbl_->setText(
                        QString("Embedded %1")
                            .arg(QLocale::c().toString(embedded)));
                }
                indexedInfoLbl_->setToolTip(
                    QStringLiteral(
                        "Total indexed: %1\n"
                        "Total extracted: %2\n"
                        "Total embedded: %3\n\n"
                        "Indexed = files searchable (content extracted or "
                        "metadata staged). Files tracked: %4 (skipped "
                        "formats and deleted files are not counted).\n"
                        "Live value — refreshes automatically.")
                        .arg(indexedNow).arg(extracted).arg(embedded)
                        .arg(total));
            } else {
                indexedInfoLbl_->setToolTip(
                    QStringLiteral(
                        "Total indexed: %1 (files tracked: %2)\n"
                        "Live value — refreshes automatically.")
                        .arg(indexedNow).arg(total));
            }
            indexedInfoLbl_->setText(QString("Indexed %1")
                                         .arg(QLocale::c().toString(indexedNow)));
        }
        if (indexedBar_) {
            // Progress = content_done / total. Only VISIBLE while indexing
            // or extraction is actively running — a permanent partial bar
            // read as "my index is incomplete" (it was also the #1 support
            // question). At idle the badge is just "N indexed".
            const bool busy = contentExtractionRunning_;
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
// Indexing progress display
// ============================================================
// v1.7.11: the legacy Indexer subsystem (never constructed since the
// direct-scan pipeline replaced it) and its four dead slots
// (onStart/Stop/Pause/ResumeIndexing) are DELETED. Scanning is done by
// scanFolderFast/autoScanIndexedFolders; extraction by the timer loop
// in onExtract; OCR by OcrWorkerPool. The progress widget below stays:
// updateIndexStats() feeds it live counters.

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
        if (!extractAndIndexFile(path)) return;
        const QFileInfo fi(path);
        statusBar()->showMessage("New file indexed: " + fi.fileName(), 3000);
        updateIndexStats();
        DS_INFO("Watcher", "Added + extracted: " + path);
    } catch (...) {
        DS_INFO("Watcher", "Failed to add: " + path);
    }
}

// Shared single-file pipeline for the watcher handlers (file ADDED and
// file MODIFIED). The old onFileModified routed through indexer_ — which
// is never constructed in this build ("indexer disabled in this build")
// — so EVERY live modify event was silently dropped: the FTS row, the
// extracted text and the AI embeddings all stayed stale until the next
// hourly scan reconciled them. v1.7.5 routes both events through this
// one pipeline and additionally invalidates the old AI embeddings so a
// modified file is re-embedded from its new text via the background
// backfill queue (ONNX never runs on the main thread — the old inline
// embedDocument call crashed with SEH exceptions that bypass catch(...)).
bool MainWindow::extractAndIndexFile(const QString& path) {
    if (!repo_ || !db_) return false;

    // SAFETY: only files under one of the user's indexed folders. The
    // file watcher should only fire for these, but this is a defensive
    // check in case a watch was added for a folder the user later
    // removed from Settings.
    bool underIndexed = false;
    for (const QString& drive : settings_.indexedDrives) {
        if (path.startsWith(drive, Qt::CaseInsensitive)) {
            underIndexed = true;
            break;
        }
    }
    if (!underIndexed) return false;

    if (FileUtils::isUnderAny(path, settings_.excludedFolders)) return false;

    // Check if extension is supported.
    // v1.7.7: the old private list here still allowed md/txt/csv/rtf
    // — a Markdown note saved into an indexed folder was indexed AND
    // extracted immediately, which is exactly how .md kept showing up
    // in results. One central allowlist now rules every ingest path.
    const QString ext = FileUtils::extensionOf(path).toLower();
    if (!Constants::isIndexableExtension(ext)) return false;
    // v1.7.11: honor the user's Excluded Extensions list here too, so a
    // live file event can't sneak an excluded type past the scan gates.
    if (normalizedExtSet(settings_.excludedExtensions).contains(ext))
        return false;

    const QFileInfo fi(path);
    if (!fi.exists()) return false;

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

    // Extract text immediately for the new/changed file (single-file
    // extraction — fast and non-blocking enough for the main thread).
    if (fi.size() <= Constants::kMaxFilesizeToExtract) {
        auto& registry = DocumentExtractorRegistry::instance();
        try {
            auto result = registry.extractByExtension(path, ext);
            QString extractedText = result.text;
            if (extractedText.size() > Constants::kMaxExtractTextChars) {
                extractedText = extractedText.left(Constants::kMaxExtractTextChars);
            }

            sqlite3* raw = db_->raw();
            if (!raw) return true;

            FileRecord rec;
            if (!repo_->getByPath(r.path, rec)) return true;  // row vanished
            const qint64 fileId = rec.id;

            if (!extractedText.isEmpty()) {
                const qint64 now = QDateTime::currentSecsSinceEpoch();
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
                    QByteArray textBytes = extractedText.toUtf8();
                    QByteArray srcBytes = (result.source.isEmpty() ? "native" : result.source).toUtf8();
                    sqlite3_bind_int64(upd, 1, fileId);
                    sqlite3_bind_text(upd, 2, textBytes.constData(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(upd, 3, srcBytes.constData(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int64(upd, 4, extractedText.size());
                    sqlite3_bind_int64(upd, 5, now);
                    sqlite3_step(upd);
                    sqlite3_finalize(upd);

                    // Update Files status.
                    sqlite3_exec(raw,
                        QString("UPDATE Files SET indexing_status='content_done' WHERE id=%1;")
                            .arg(fileId).toUtf8().constData(),
                        nullptr, nullptr, nullptr);

                    // Update SearchIndex (delete + insert = clean FTS row).
                    sqlite3_stmt* del = nullptr;
                    sqlite3_prepare_v2(raw, "DELETE FROM SearchIndex WHERE file_id=?1;", -1, &del, nullptr);
                    if (del) { sqlite3_bind_int64(del, 1, fileId); sqlite3_step(del); sqlite3_finalize(del); }

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
                        sqlite3_bind_int64(ins, 5, fileId);
                        sqlite3_step(ins);
                        sqlite3_finalize(ins);
                    }
                }
            } else if (result.needsOcr) {
                // Scanned PDF / image — mark as needs_ocr.
                sqlite3_exec(raw,
                    QString("UPDATE Files SET indexing_status='needs_ocr' WHERE id=%1;")
                        .arg(fileId).toUtf8().constData(),
                    nullptr, nullptr, nullptr);
            }

            // v1.7.5: the file's content just changed — any stored AI
            // embedding was computed from the OLD text and must not
            // survive. Drop both embedding tables for this file; the
            // background backfill queue re-embeds it from the new text
            // (ensureEmbeddingsBackfill runs ONNX off the main thread).
            for (const char* delSql : {
                     "DELETE FROM BgeEmbeddings WHERE file_id=?1;",
                     "DELETE FROM EmbeddingChunks WHERE file_id=?1;" }) {
                sqlite3_stmt* delE = nullptr;
                if (sqlite3_prepare_v2(raw, delSql, -1, &delE, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int64(delE, 1, fileId);
                    sqlite3_step(delE);
                    sqlite3_finalize(delE);
                }
            }
            ensureEmbeddingsBackfill();
        } catch (...) {
            // Extraction failed — file stays as 'pending', user can retry.
            DS_INFO("Watcher", "Extraction failed for: " + path);
        }
    }
    return true;
}
void MainWindow::onFileModified(const QString& path) {
    if (!repo_ || !db_) return;
    try {
        // v1.7.5: the old code routed through indexer_->reindexFile(),
        // but indexer_ is never constructed in this build — the guard
        // below returned for EVERY event and live file edits were
        // silently dropped until the next hourly scan. Reuse the shared
        // add/modify pipeline instead (it also invalidates the stale AI
        // embedding so the modified file is re-embedded from new text).
        if (!extractAndIndexFile(path)) return;
        const QFileInfo fi(path);
        statusBar()->showMessage("File updated: " + fi.fileName(), 3000);
        updateIndexStats();
        DS_INFO("Watcher", "Reindexed modified: " + path);

        // Keep the visible results honest: if the user is looking at a
        // search that includes this file, re-run it so the new snippet
        // and metadata show up immediately.
        if (!searchBar_->text().trimmed().isEmpty()) {
            onSearch(searchBar_->text());
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
#ifdef DOCUSEARCH_HAS_PDFIUM
    else if (isPdf) {
        // For PDFs: render each page to image via PDFium, save as temp
        // PNG, then OCR each page via the helper exe.
        try {
            statusBar()->showMessage("OCR: opening PDF...", 0);
            QApplication::processEvents();

            PdfiumDocument doc;
            if (!doc.loadFromFile(filePath) || doc.pageCount() == 0) {
                statusBar()->showMessage(
                    doc.lastError().isEmpty()
                        ? QStringLiteral("OCR: failed to open PDF.")
                        : QStringLiteral("OCR: %1.").arg(doc.lastError()),
                    5000);
                return;
            }

            const int dpi = 96;  // lower DPI for OCR speed
            const int pageTotal = doc.pageCount();
            const int maxPages = (pageTotal < 10) ? pageTotal : 10;  // max 10 pages

            for (int i = 0; i < maxPages; ++i) {
                statusBar()->showMessage(
                    QString("OCR: page %1/%2...").arg(i + 1).arg(maxPages), 0);
                QApplication::processEvents();

                try {
                    const QImage qimg = doc.renderPage(i, dpi);
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
                // v1.7.11: route through the SAME apply path as OK.
                // The old inline handler only saved + re-themed: it never
                // pushed CPU throttle settings into the OCR pool and never
                // diffed indexedDrives — folders added/removed via Apply
                // did nothing. Worse, it overwrote settings_, so a later
                // OK diffed new-vs-new and silently skipped the
                // scan/watch/purge for those folders ("Apply then OK"
                // lost folder changes entirely).
                applyNewSettings(s);
                statusBar()->showMessage("Settings applied.", 3000);
            });

        QObject::connect(&dlg, &SettingsDialog::removeDatabaseRequested,
            this, [this](){
                // v1.7.10: Settings → "Remove Database (Reset)". The
                // dialog already double-confirmed; do the wipe+rebuild.
                removeAndRebuildDatabase();
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

        // "Rebuild All AI Embeddings" — purge every stored embedding in
        // small batches, then re-run the shared two-phase backfill so
        // the whole library is recomputed from full document text.
        QObject::connect(&dlg, &SettingsDialog::rebuildEmbeddingsRequested,
            this, &MainWindow::startEmbeddingRebuild);

        const int rc = dlg.exec();
        refreshSavedSearches();
        // The user may have just closed the dialog after running an
        // external install script. Refresh the OCR status indicator.
        updateOcrStatusIndicator();
        if (rc == QDialog::Accepted) {
            // v1.7.11: same shared path as the Apply button. Because
            // applyNewSettings() diffs against the live settings_, an
            // earlier Apply click is naturally idempotent here — folders
            // it already scanned/purged are seen as unchanged.
            applyNewSettings(dlg.result());
        }
    } catch (...) {
        statusBar()->showMessage("Settings dialog failed.", 3000);
    }
}

// v1.7.11: THE single settings-apply path (Apply button AND OK button).
// Diffs against the current settings_ so it is safe to call repeatedly.
void MainWindow::applyNewSettings(const AppSettings& s) {
    const AppSettings oldSettings = settings_;
    settings_ = s;

    // CPU throttle / pause-on-load settings reach the OCR pool (v1.7.9,
    // previously OK-only — the Apply button never delivered them).
    if (ocrPool_) ocrPool_->setAppSettings(settings_);

    darkMode_ = settings_.darkMode;
    pastelTheme_ = darkMode_ ? 1 : 0;   // v1.7.6 sync
    saveSettings();
    applyTheme();
    updateIndexStats();
    updateOcrStatusIndicator();  // refresh in case OCR setup changed

    // ---- Live-monitoring master switch (v1.7.11) ----
    // The "Monitor indexed drives for live changes" checkbox existed in
    // Settings since day one but was consumed by NOTHING — the watcher
    // always ran. Honor it: OFF stops every watch thread; ON re-arms
    // watches for all indexed folders.
    if (watcher_) {
        if (!settings_.monitorFileChanges && oldSettings.monitorFileChanges) {
            watcher_->stop();
            DS_INFO("Watcher", "Live monitoring disabled in Settings.");
        } else if (settings_.monitorFileChanges &&
                   !oldSettings.monitorFileChanges) {
            if (!settings_.indexedDrives.isEmpty())
                watcher_->addWatches(settings_.indexedDrives);
            DS_INFO("Watcher", "Live monitoring re-enabled in Settings.");
        }
    }

    // v1.7.4: case-fold the folder lists before diffing — Windows
    // paths are case-insensitive and QStringList::contains is not,
    // so "D:\Docs" vs "d:\docs" used to look like two folders.
    auto toFolded = [](const QStringList& list) {
        QSet<QString> folded;
        folded.reserve(list.size());
        for (const QString& f : list)
            folded.insert(FileUtils::toNative(f).toLower());
        return folded;
    };
    const QSet<QString> oldFolded = toFolded(oldSettings.indexedDrives);
    const QSet<QString> newFolded = toFolded(settings_.indexedDrives);

    // ---- NEWLY EXCLUDED folders: purge their rows right away ----
    // v1.7.11: adding "D:\Movies" to Excluded Folders now takes effect
    // immediately — its already-indexed rows are removed on the spot
    // instead of lingering (searchable!) until the next hourly scan's
    // prune pass finally dropped them.
    {
        const QSet<QString> oldExcluded = toFolded(oldSettings.excludedFolders);
        for (const QString& ex : settings_.excludedFolders) {
            if (oldExcluded.contains(FileUtils::toNative(ex).toLower()))
                continue;  // was already excluded
            statusBar()->showMessage(
                QStringLiteral("Removing excluded folder '%1' from the index...")
                    .arg(ex));
            QApplication::processEvents();
            purgeFolderFromIndex(ex);
        }
    }

    // ---- REMOVED folders: stop watching + purge their rows ----
    // v1.7.4 fix for "removed the folders from settings menu,
    // nothing happens": the rows used to stay in Files/SearchIndex
    // forever (still searchable, still in duplicates) and the
    // watcher kept watching the removed root. Now the whole index
    // footprint of the folder is deleted on the spot.
    int removedFolders = 0;
    for (const QString& drive : oldSettings.indexedDrives) {
        if (newFolded.contains(FileUtils::toNative(drive).toLower()))
            continue;  // still indexed
        ++removedFolders;
        statusBar()->showMessage(
            QStringLiteral("Removing '%1' from the index...")
                .arg(drive));
        QApplication::processEvents();
        if (watcher_) watcher_->removeWatch(drive);
        purgeFolderFromIndex(drive);
    }

    // ---- ADDED folders: watch live, scan now, auto-extract ----
    // v1.7.4 fix: a newly added folder was scanned but NEVER
    // watched (addWatches ran once at startup only), so live
    // changes in it went unnoticed until the next hourly scan.
    for (const QString& drive : settings_.indexedDrives) {
        if (oldFolded.contains(FileUtils::toNative(drive).toLower()))
            continue;  // unchanged
        if (settings_.monitorFileChanges &&
            watcher_ && !watcher_->isWatched(drive))
            watcher_->addWatch(drive);
        statusBar()->showMessage("Scanning " + drive + " ...");
        QApplication::processEvents();
        scanFolderFast(drive);
        // Auto-extract after scanning new drives
        QTimer::singleShot(500, this, [this]() {
            autoExtractRetryLeft_ = 20;  // fresh budget
            requestAutoExtract();
        });
    }

    if (removedFolders > 0) {
        statusBar()->showMessage(
            QStringLiteral("%1 folder%2 removed from the index — "
                           "their files no longer appear in search")
                .arg(removedFolders)
                .arg(removedFolders == 1 ? "" : "s"), 6000);
        // Drop rows from the removed folder out of the current
        // result list immediately.
        const QString currentQuery = searchBar_->text();
        if (!currentQuery.isEmpty()) onSearch(currentQuery);
    }
}

void MainWindow::onToggleTheme() {
    try {
        // Fluent Design — toggle between Light (0) and Dark (1) only.
        // Was previously cycling 4 Pastel Pop themes — too many options.
        // v1.7.6: persist the ACTUAL setting (darkMode) and re-derive the
        // render index from it. Before this, the toggle only flipped the
        // in-memory pastelTheme_ and saved the stale darkMode value, so
        // the chosen theme was lost on every restart.
        settings_.darkMode = !settings_.darkMode;
        darkMode_ = settings_.darkMode;
        pastelTheme_ = darkMode_ ? 1 : 0;
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

// ============================================================
// Duplicate detection
//
// REWRITE. The finder used to be a pure database query:
// it grouped on the `hash` column and simply believed whatever was
// stored there. Three separate properties of that design made it
// report "No duplicate files found." on indexes that were FULL of
// duplicates:
//
//   (1) NO HASH = INVISIBLE. Rows whose hash column was empty could
//       never group. Hashes are written by the scanners and by a
//       bounded startup backfill (4000 rows/launch), so on any real
//       index a large slice of rows is unhashed at any moment — and
//       every one of them was silently dropped from the comparison.
//       The user was told to "re-scan your folders", which only
//       moves the backfill forward a little.
//   (2) THE STALE-HASH GATE DELETED CANDIDATES. Any row whose size
//       or mtime no longer matched the indexed values was *dropped*
//       rather than re-hashed. Copying a file (which is exactly how
//       duplicates come into existence) usually gives the copy a new
//       mtime, so freshly created duplicates were the FIRST thing
//       thrown away.
//   (3) THE SETTINGS SWITCH COULD DISABLE IT ENTIRELY. With
//       "Compute file hashes" off, nothing ever wrote a hash, so the
//       finder had nothing to group and said "no duplicates" —
//       while pointing at the very setting it needed.
//
// The finder now COMPUTES what it needs, on demand, at the moment
// the user asks:
//
//   • Candidates are pre-grouped by FILE SIZE, which is free (it
//     comes from the row) and is a perfect necessary condition:
//     two files with different sizes can never be byte-identical.
//     Sizes that occur only once are discarded without any I/O.
//   • Only the survivors are fingerprinted, and a stored hash is
//     reused ONLY when size + mtime still match the file on disk.
//     Everything else — unhashed rows, stale rows, all rows when
//     the Settings switch is off — is hashed live, right here.
//   • Freshly computed hashes are written back to the index, so the
//     next run is fast and the rest of the app benefits too.
//
// The result: the answer no longer depends on how much backfill has
// happened to run. "No duplicates" now means the bytes really do
// differ.
// ============================================================
// v1.7.13: ONE identity function for "which physical file is this?".
// Used by the same-file collapse (pass 2) AND by the final guard before
// display, so the two passes can never disagree. Covers the spellings
// that used to slip through and make one physical file pair with
// itself ("single file is showing as duplicates"):
//   - canonical resolution (junctions, symlinks, mapped drives)
//   - extended-length prefixes: \\\\?\D:\... and \\\\?\UNC\\server\\...
//     (the UNC form used to become "UNC\\server\\..." after the prefix
//     strip and never matched the plain \\\\server\\... spelling)
//   - dot segments and mixed separators (cleanPath on the fallback)
//   - Windows case-insensitivity
static QString pathIdentityKey(const QString& p) {
    QString key = QFileInfo(p).canonicalFilePath();
    if (key.isEmpty()) key = QDir::cleanPath(p);
#ifdef Q_OS_WIN
    if (key.startsWith(QStringLiteral("\\\\?\\")) ||
        key.startsWith(QStringLiteral("//?/"))) {
        key.remove(0, 4);
        if (key.startsWith(QStringLiteral("UNC")) && key.size() > 3 &&
            (key.at(3) == QLatin1Char('\\') || key.at(3) == QLatin1Char('/')))
            key = QStringLiteral("//") + key.mid(4);
    }
    key.replace(QLatin1Char('\\'), QLatin1Char('/'));
    key = key.toLower();
#endif
    return key;
}

void MainWindow::onDetectDuplicates() {
    if (!repo_ || !db_) return;
    // The hashing loop below pumps the event loop, so the user can
    // click Duplicates again (or the sidebar can re-enter this slot)
    // while it is still running. Two concurrent runs would fight over
    // the same results pane and hash the same files twice.
    static bool running = false;
    if (running) return;
    running = true;
    struct Guard {
        bool& f;
        ~Guard() { f = false; }
    } guard{running};
    try {
        sqlite3* raw = db_->raw();
        if (!raw) return;

        // Duplicate detection covers everything the app indexes:
        // documents AND images (identical scanned jpg/png/tif pairs
        // are real duplicates, and are the most common kind users
        // actually have).
        QString typeList;
        QString docTypeHelp;
        for (const QString& t : Constants::kIndexableExtensions) {
            if (!typeList.isEmpty()) typeList += QLatin1Char(',');
            typeList += QString("'%1'").arg(t.toLower());
            docTypeHelp += QStringLiteral(" .%1").arg(t.toLower());
        }

        // ---- Pass 1: pull every indexable row (hashed or not) ----
        // No `hash != ''` filter any more: an unhashed row is a
        // perfectly good duplicate candidate, we just have to do the
        // work ourselves below.
        struct Cand {
            qint64  id = 0;
            QString path, filename, extension;
            qint64  size = 0;
            qint64  rowMtime = 0;
            QString storedHash;
        };
        QList<Cand> cands;

        sqlite3_stmt* s = nullptr;
        const QString sql = QString(
            "SELECT id, path, filename, extension, size, "
            "       modified_date, COALESCE(hash, '') "
            "FROM Files WHERE lower(extension) IN (%1);").arg(typeList);
        if (sqlite3_prepare_v2(raw, sql.toUtf8().constData(),
                               -1, &s, nullptr) != SQLITE_OK) {
            statusBar()->showMessage("Duplicate detection failed.", 3000);
            return;
        }

        int skippedMissing = 0;
        // v1.7.4: collected DURING the walk, purged AFTER finalize —
        // never delete from Files while a SELECT on it is stepping.
        QStringList stalePaths;
        int scanned = 0;

        while (sqlite3_step(s) == SQLITE_ROW) {
            Cand c;
            c.id = sqlite3_column_int64(s, 0);
            auto col = [&](int i) {
                const unsigned char* p = sqlite3_column_text(s, i);
                return p ? QString::fromUtf8(
                    reinterpret_cast<const char*>(p)) : QString();
            };
            c.path       = col(1);
            c.filename   = col(2);
            c.extension  = col(3);
            c.size       = sqlite3_column_int64(s, 4);
            c.rowMtime   = sqlite3_column_int64(s, 5);
            c.storedHash = col(6);

            // Index rows can outlive their files (deleted after
            // scanning, or MOVED and the old row not yet pruned).
            // A "duplicate" pointing at nothing helps nobody.
            if (!QFileInfo::exists(c.path)) {
                ++skippedMissing;
                if (storageRootReachable(c.path)) stalePaths.append(c.path);
                continue;
            }
            cands.append(c);

            if ((++scanned % 500) == 0) {
                statusBar()->showMessage(
                    QString("Checking for duplicates... %1 files")
                        .arg(scanned));
                QApplication::processEvents();
            }
        }
        sqlite3_finalize(s);

        // v1.7.4: purge the ghost rows gathered above (drive reachable
        // = a genuine deletion/move; offline roots are left untouched).
        if (!stalePaths.isEmpty())
            purgeStaleRows(stalePaths, QStringLiteral("duplicates"));

        // ---- Pass 2: collapse rows that are the SAME physical file ----
        // The same file can sit in Files more than once — after
        // aggressive re-scans, or under two spellings of one path:
        //   • mixed separators   D:\Docs\a.pdf  vs  D:/Docs/a.pdf
        //   • dot segments       D:\Docs\a.pdf  vs  D:\Docs\.\a.pdf
        //   • junctions/symlinks D:\Real\a.pdf  vs  D:\Link\a.pdf
        //   • overlapping roots  (a folder added as root AND as child)
        // Canonicalization resolves all four. One physical file must
        // never pass as a "group of two" with itself.
        int staleRows = 0;
        {
            // v1.7.13: collapse now runs through pathIdentityKey(), so
            // the extended-length prefixes, dot segments and separator
            // spellings this pass used to miss cannot self-pair either.
            QSet<QString> seenPaths;
            QList<Cand> unique;
            unique.reserve(cands.size());
            for (const Cand& c : cands) {
                const QString key = pathIdentityKey(c.path);
                if (seenPaths.contains(key)) { ++staleRows; continue; }
                seenPaths.insert(key);
                unique.append(c);
            }
            cands = std::move(unique);
        }

        // ---- Pass 3: size pre-grouping (free, and exact) ----
        // Two files of different sizes cannot be byte-identical, so a
        // size that occurs exactly once needs no I/O at all. On a
        // typical index this removes 90 %+ of the work, which is what
        // makes live hashing affordable.
        // Use the CURRENT on-disk size, not the indexed one: a row
        // whose file changed since scanning must still be grouped
        // (previously such rows were dropped outright).
        QHash<qint64, int> sizeCount;
        QList<qint64> liveSize;
        QList<qint64> liveMtime;
        liveSize.reserve(cands.size());
        liveMtime.reserve(cands.size());
        for (const Cand& c : cands) {
            const QFileInfo fi(c.path);
            const qint64 sz = fi.size();
            liveSize.append(sz);
            liveMtime.append(fi.lastModified().toSecsSinceEpoch());
            ++sizeCount[sz];
        }

        // ---- Pass 4: fingerprint the survivors ----
        QList<SearchHit> hits;
        QStringList hashes;
        int hashedNow = 0, unreadable = 0;

        QList<int> toHash;
        for (int i = 0; i < cands.size(); ++i)
            if (sizeCount.value(liveSize[i], 0) >= 2) toHash.append(i);

        // Range must never be 0..0 — that is QProgressDialog's "busy"
        // mode, which shows a spinner forever for a job with nothing
        // to do.
        QProgressDialog prog(
            QStringLiteral("Comparing file contents..."),
            QStringLiteral("Cancel"), 0,
            qMax(1, static_cast<int>(toHash.size())), this);
        prog.setWindowTitle(QStringLiteral("Duplicates"));
        prog.setWindowModality(Qt::WindowModal);
        // Don't flash a dialog for a job that finishes instantly.
        prog.setMinimumDuration(400);
        bool cancelled = false;

        for (int n = 0; n < toHash.size(); ++n) {
            const int i = toHash[n];
            const Cand& c = cands[i];

            // Reuse the stored fingerprint ONLY if it still describes
            // the file on disk (2 s mtime tolerance for FAT's coarse
            // timestamps). Otherwise re-hash — a stale hash used to
            // mean "drop this file", which silently hid every freshly
            // copied duplicate.
            QString h;
            // v1.7.13: a row with NO mtime is never trusted either —
            // size alone cannot distinguish an edited file from an
            // untouched one. The write-back below heals such rows on
            // this very run, so the cost is one hashing pass, once.
            const bool storedIsFresh =
                !c.storedHash.isEmpty() &&
                c.rowMtime > 0 &&
                liveSize[i] == c.size &&
                qAbs(liveMtime[i] - c.rowMtime) <= 2;
            if (storedIsFresh) {
                h = c.storedHash;
            } else {
                // Same 64 MB cap as every other hashing path, so a
                // fingerprint computed here is comparable with one
                // written by the scanners.
                h = FileUtils::sha256OfFile(c.path, 64 * 1024 * 1024);
                if (h.isEmpty()) { ++unreadable; continue; }
                ++hashedNow;
                // Write the whole fingerprint triple back — hash AND
                // the live size/mtime it was computed from. Writing
                // only the hash left drifted rows stale (the row still
                // claimed the old size), so storedIsFresh stayed false
                // and every run re-hashed the same files; the row also
                // disagreed with itself for any future consumer.
                sqlite3_stmt* u = nullptr;
                if (sqlite3_prepare_v2(raw,
                        "UPDATE Files SET hash = ?1, size = ?2, "
                        "modified_date = ?3 WHERE id = ?4;",
                        -1, &u, nullptr) == SQLITE_OK) {
                    sqlite3_bind_text(u, 1, h.toUtf8().constData(), -1,
                                      SQLITE_TRANSIENT);
                    sqlite3_bind_int64(u, 2, liveSize[i]);
                    sqlite3_bind_int64(u, 3, liveMtime[i]);
                    sqlite3_bind_int64(u, 4, c.id);
                    sqlite3_step(u);
                    sqlite3_finalize(u);
                }
            }

            SearchHit sh;
            sh.fileId       = c.id;
            sh.path         = c.path;
            sh.filename     = c.filename;
            sh.extension    = c.extension;
            sh.size         = liveSize[i];
            sh.modifiedDate = QDateTime::fromSecsSinceEpoch(liveMtime[i]);
            hits.append(sh);
            // Group key = exact size + fingerprint. The fingerprint is
            // capped at 64 MB (same cap every hashing path uses), so
            // two DIFFERENT files larger than the cap that happen to
            // share their first 64 MB — e.g. two long videos-of-scans
            // exported from the same tool, or two PDFs with identical
            // front matter — would otherwise collide into a false
            // "duplicate". Qualifying the key with the byte-exact size
            // makes that impossible without re-reading whole files.
            hashes.append(QStringLiteral("%1:%2")
                              .arg(liveSize[i]).arg(h));

            if ((n % 16) == 0) {
                prog.setValue(n);
                QApplication::processEvents();
                if (prog.wasCanceled()) { cancelled = true; break; }
            }
        }
        prog.setValue(prog.maximum());   // closes the dialog

        // ---- Pass 5: keep only hashes with >= 2 SURVIVING files ----
        // A lone file whose partner was deleted must never render as
        // a "duplicate" of something that no longer exists.
        int droppedSingletons = 0;
        {
            QHash<QString, int> groupSize;
            for (const QString& hs : hashes) ++groupSize[hs];
            QList<SearchHit> kept;
            QStringList keptHashes;
            for (int i = 0; i < hits.size(); ++i) {
                if (groupSize.value(hashes[i], 0) >= 2) {
                    kept.append(hits[i]);
                    keptHashes.append(hashes[i]);
                } else {
                    ++droppedSingletons;
                }
            }
            hits   = std::move(kept);
            hashes = std::move(keptHashes);
        }

        // Display-time re-verification: processEvents() above lets the
        // user (or a sync client) move/delete files WHILE the walk
        // runs. Re-verify and re-run the survivors-only grouping so a
        // file whose partner vanished mid-walk can never render as a
        // pair whose second file is gone.
        // v1.7.13: the SAME identity check also runs here as a last
        // line of defense — if two surviving rows still resolve to one
        // physical file (a spelling canonicalization cannot unify),
        // the shadow row is dropped and the recount below makes sure
        // a group reduced to one file disappears instead of showing
        // "a duplicate" that has no partner.
        {
            QList<SearchHit> verified;
            QStringList verifiedHashes;
            verified.reserve(hits.size());
            QSet<QString> seenIdentity;
            for (int i = 0; i < hits.size(); ++i) {
                if (!QFileInfo::exists(hits[i].path)) {
                    ++droppedSingletons;   // keep the summary honest
                    continue;
                }
                const QString ident = pathIdentityKey(hits[i].path);
                if (seenIdentity.contains(ident)) {
                    ++staleRows;           // one file, two rows: shadow
                    continue;
                }
                seenIdentity.insert(ident);
                verified.append(hits[i]);
                verifiedHashes.append(hashes[i]);
            }
            QHash<QString, int> aliveSize;
            for (const QString& hs : verifiedHashes) ++aliveSize[hs];
            QList<SearchHit> paired;
            QStringList pairedHashes;
            for (int i = 0; i < verified.size(); ++i) {
                if (aliveSize.value(verifiedHashes[i], 0) >= 2) {
                    paired.append(verified[i]);
                    pairedHashes.append(verifiedHashes[i]);
                }
            }
            hits   = std::move(paired);
            hashes = std::move(pairedHashes);
        }

        // Order the output so members of a group sit together.
        {
            QList<int> idx;
            idx.reserve(hits.size());
            for (int i = 0; i < hits.size(); ++i) idx.append(i);
            std::sort(idx.begin(), idx.end(), [&](int a, int b) {
                if (hashes[a] != hashes[b]) return hashes[a] < hashes[b];
                return hits[a].filename.localeAwareCompare(
                           hits[b].filename) < 0;
            });
            QList<SearchHit> sortedHits;
            QStringList sortedHashes;
            sortedHits.reserve(hits.size());
            for (const int i : idx) {
                sortedHits.append(hits[i]);
                sortedHashes.append(hashes[i]);
            }
            hits   = std::move(sortedHits);
            hashes = std::move(sortedHashes);
        }

        int groupCount = 0;
        QString lastHash;
        for (const QString& hs : hashes) {
            if (hs != lastHash) { ++groupCount; lastHash = hs; }
        }

        if (hits.isEmpty()) {
            // Empty the results list too, so the listing from a
            // PREVIOUS duplicate check can't stay on screen behind
            // the message box.
            resultsPane_->setResults({});
            // v1.7.13: nothing to delete, and no stale caption/pill from
            // an earlier run (or from search) may survive either.
            dupResults_.clear();
            dupKeys_.clear();
            resultsPane_->setAction(QString());
            resultsPane_->setAiSummary(QString());

            QString detail;
            if (cancelled) {
                detail = QStringLiteral(
                    "The check was cancelled before it finished, so "
                    "some files were never compared.");
            } else {
                // Every comparable file WAS compared byte-for-byte
                // this time — say so plainly instead of blaming a
                // setting the finder no longer depends on.
                detail = QString(
                    "%1 indexed file%2 compared by content"
                    "%3.\n\nDuplicate search covers documents and "
                    "images:%4")
                    .arg(cands.size())
                    .arg(cands.size() == 1 ? " was" : "s were")
                    .arg(hashedNow > 0
                        ? QString(" (%1 fingerprint%2 computed now)")
                            .arg(hashedNow)
                            .arg(hashedNow == 1 ? "" : "s")
                        : QString())
                    .arg(docTypeHelp);
            }
            QStringList notes;
            if (skippedMissing > 0)
                notes << QString("%1 index entr%2 pointed at files that "
                                 "no longer exist")
                             .arg(skippedMissing)
                             .arg(skippedMissing == 1 ? "y" : "ies");
            if (staleRows > 0)
                notes << QString("%1 duplicate index row%2 for the same "
                                 "physical file %3 collapsed")
                             .arg(staleRows)
                             .arg(staleRows == 1 ? "" : "s")
                             .arg(staleRows == 1 ? "was" : "were");
            if (droppedSingletons > 0)
                notes << QString("%1 file%2 lost its duplicate partner "
                                 "during the check")
                             .arg(droppedSingletons)
                             .arg(droppedSingletons == 1 ? "" : "s");
            if (unreadable > 0)
                notes << QString("%1 file%2 could not be read")
                             .arg(unreadable)
                             .arg(unreadable == 1 ? "" : "s");

            QMessageBox::information(this, "Duplicates",
                QString("No duplicate files found.\n\n%1%2")
                    .arg(detail)
                    .arg(notes.isEmpty()
                        ? QString()
                        : QStringLiteral("\n\n") + notes.join(", ")
                              + QStringLiteral(".")));
            statusBar()->showMessage(
                cancelled ? "Duplicate check cancelled."
                          : "No duplicate files found.", 5000);
            return;
        }

        resultsPane_->setResults(hits);
        // v1.7.13: arm the cleanup action + remember what the pane is
        // showing (the delete slot re-validates against disk anyway).
        dupResults_ = hits;
        dupKeys_    = hashes;
        resultsPane_->setAction(
            QStringLiteral("Delete duplicates..."));
        // A cancelled check must not pass as complete: keep the note on
        // screen as long as the results are (not an 8 s status toast).
        resultsPane_->setAiSummary(cancelled
            ? QStringLiteral("Check cancelled early - the list is "
                             "partial; some files were never compared.")
            : QString());
        statusBar()->showMessage(
            QString("Found %1 duplicate group%2 (%3 files)%4%5%6")
                .arg(groupCount)
                .arg(groupCount == 1 ? "" : "s")
                .arg(hits.size())
                .arg(hashedNow > 0
                    ? QString("; %1 fingerprint%2 computed now")
                        .arg(hashedNow).arg(hashedNow == 1 ? "" : "s")
                    : QString())
                .arg(staleRows > 0
                    ? QString("; %1 duplicate index row%2 collapsed")
                        .arg(staleRows).arg(staleRows == 1 ? "" : "s")
                    : QString())
                .arg(cancelled ? QStringLiteral("; check cancelled early")
                               : QString()),
            8000);
        if (hashedNow > 0) updateIndexStats();
    } catch (const std::exception& e) {
        DS_ERROR("Duplicates", QString("Failed: %1").arg(e.what()));
        statusBar()->showMessage("Duplicate detection failed.", 3000);
    } catch (...) {
        statusBar()->showMessage("Duplicate detection failed.", 3000);
    }
}

// v1.7.14: move a file into a user-chosen folder, keeping its name
// ("name (2).ext" on collision). QFile::rename fails across volumes
// (ERROR_NOT_SAME_DEVICE), so fall back to copy-then-remove — and the
// copy is size-verified before the original is unlinked, so a partial
// copy can never destroy the only other copy of the content.
static bool moveFileKeepingName(const QString& src, const QString& destDir) {
    const QFileInfo fi(src);
    if (!fi.exists() || !QFileInfo(destDir).isDir()) return false;
    const QString base   = fi.completeBaseName();
    const QString suffix = fi.suffix();
    QString candidate = destDir + "/" + fi.fileName();
    if (QFileInfo::exists(candidate)) {
        for (int n = 2; ; ++n) {
            if (n > 999) return false;   // pathological — never spin forever
            candidate = destDir + "/" + base + " (" + QString::number(n) + ")" +
                        (suffix.isEmpty() ? QString() : "." + suffix);
            if (!QFileInfo::exists(candidate)) break;
        }
    }
    if (QFile::rename(src, candidate)) return true;
    if (!QFile::copy(src, candidate))  return false;
    if (QFileInfo(candidate).size() != fi.size()) {
        QFile::remove(candidate);        // copy unverifiable — keep original
        return false;
    }
    return QFile::remove(src);
}

// v1.7.13: "Delete duplicate copies" — cleanup for the duplicates
// results. For every group with >= 2 files that still exist, the
// NEWEST copy is kept and the rest are removed. v1.7.14: the user
// chooses the destination — Recycle Bin (restorable) or a folder they
// pick (a keep-on-disk quarantine move). Moved rows are purged, stats
// refreshed, and the duplicates check re-runs so the pane shows the
// truth after. The list is re-validated against disk FIRST: files
// moved/deleted since the check, and groups whose partner is gone,
// are skipped — a group with one survivor is never touched at all.
void MainWindow::onDeleteDuplicateCopies() {
    if (dupResults_.isEmpty() || dupKeys_.size() != dupResults_.size())
        return;

    // Group only by members that still exist on disk.
    QHash<QString, QList<int>> groups;
    for (int i = 0; i < dupResults_.size(); ++i) {
        if (!QFileInfo::exists(dupResults_[i].path)) continue;
        groups[dupKeys_[i]].append(i);
    }

    QList<int> doomed;
    qint64 reclaimBytes = 0;
    int groupsActed = 0;
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        const QList<int>& members = it.value();
        if (members.size() < 2) continue;      // never the last copy
        // Keep the NEWEST copy (highest live mtime; tie -> first).
        int keep = members[0];
        qint64 keepMtime = QFileInfo(dupResults_[keep].path)
                               .lastModified().toSecsSinceEpoch();
        for (int m = 1; m < members.size(); ++m) {
            const qint64 mt = QFileInfo(dupResults_[members[m]].path)
                                  .lastModified().toSecsSinceEpoch();
            if (mt > keepMtime) { keep = members[m]; keepMtime = mt; }
        }
        for (int m : members) {
            if (m == keep) continue;
            reclaimBytes += QFileInfo(dupResults_[m].path).size();
            doomed.append(m);
        }
        ++groupsActed;
    }
    if (doomed.isEmpty()) {
        QMessageBox::information(this, "Delete duplicate copies",
            "Nothing to delete: no group still has two or more files on "
            "disk. Re-run Detect Duplicates to refresh the list.");
        return;
    }

    // v1.7.14: destination choice. A plain Yes/No box could only offer
    // the Recycle Bin; the user asked for a keep-on-disk alternative —
    // moving duplicates into one folder they pick (e.g. a USB drive or
    // a "ToReview" folder) instead of deleting them at all.
    QDialog dlg(this);
    dlg.setWindowTitle("Delete duplicate copies");
    dlg.setMinimumWidth(420);
    auto* vLay = new QVBoxLayout(&dlg);
    auto* introLbl = new QLabel(
        QString("Move %1 duplicate cop%2 (%3)?\n\n"
                "The newest copy in each of the %4 group%5 is kept.")
            .arg(doomed.size())
            .arg(doomed.size() == 1 ? "y" : "ies")
            .arg(Utils::formatFileSize(reclaimBytes))
            .arg(groupsActed)
            .arg(groupsActed == 1 ? "" : "s"),
        &dlg);
    introLbl->setWordWrap(true);
    vLay->addWidget(introLbl);

    auto* recycleRb = new QRadioButton(
        QStringLiteral("Recycle Bin (restorable)"), &dlg);
    recycleRb->setChecked(true);
    vLay->addWidget(recycleRb);

    auto* folderRb = new QRadioButton(
        QStringLiteral("Move to a folder I choose (kept on disk; "
                       "renamed if a name is already taken)"), &dlg);
    vLay->addWidget(folderRb);

    auto* folderRow = new QHBoxLayout();
    auto* folderEd  = new QLineEdit(&dlg);
    folderEd->setPlaceholderText(QStringLiteral("No folder chosen yet"));
    folderEd->setEnabled(false);
    auto* browseBtn = new QPushButton(QStringLiteral("Browse..."), &dlg);
    browseBtn->setEnabled(false);
    folderRow->addWidget(folderEd, 1);
    folderRow->addWidget(browseBtn);
    vLay->addLayout(folderRow);

    // Honest warning: a destination inside an indexed folder gets
    // re-discovered by the hourly scan — the moved files return as new
    // index rows (still duplicates by content).
    auto* warnLbl = new QLabel(&dlg);
    warnLbl->setWordWrap(true);
    warnLbl->setStyleSheet(QStringLiteral("color:#b45309;"));
    warnLbl->setVisible(false);
    vLay->addWidget(warnLbl);

    auto syncFolderRow = [folderRb, folderEd, browseBtn]() {
        const bool on = folderRb->isChecked();
        folderEd->setEnabled(on);
        browseBtn->setEnabled(on);
    };
    connect(folderRb, &QRadioButton::toggled, &dlg, syncFolderRow);
    connect(browseBtn, &QPushButton::clicked, &dlg, [&]() {
        const QString dir = QFileDialog::getExistingDirectory(
            &dlg, QStringLiteral("Choose destination folder"),
            folderEd->text());
        if (dir.isEmpty()) return;
        folderEd->setText(QDir::toNativeSeparators(dir));
        bool insideIndexed = false;
        for (const QString& root : settings_.indexedDrives) {
            if (root.trimmed().isEmpty()) continue;
            const QString rp = QDir(root).absolutePath() + QDir::separator();
            if (dir.startsWith(rp, Qt::CaseInsensitive)) {
                insideIndexed = true;
                break;
            }
        }
        warnLbl->setText(insideIndexed
            ? QStringLiteral(
                "Heads-up: this folder is inside an indexed folder - the "
                "moved files will be scanned into the index again and "
                "would show up as duplicates once more.")
            : QString());
        warnLbl->setVisible(insideIndexed);
    });

    auto* btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    btns->button(QDialogButtonBox::Ok)->setText(
        QString("Move %1 cop%2")
            .arg(doomed.size())
            .arg(doomed.size() == 1 ? "y" : "ies"));
    connect(btns, &QDialogButtonBox::accepted, &dlg, [&]() {
        if (folderRb->isChecked() && folderEd->text().trimmed().isEmpty()) {
            QMessageBox::information(
                &dlg, "Delete duplicate copies",
                "Choose a destination folder first, or pick Recycle Bin.");
            return;
        }
        dlg.accept();
    });
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    vLay->addWidget(btns);

    if (dlg.exec() != QDialog::Accepted) return;
    const bool    toRecycleBin = recycleRb->isChecked();
    const QString destDir      = toRecycleBin
        ? QString()
        : QDir::fromNativeSeparators(folderEd->text().trimmed());

    int deleted = 0, failed = 0;
    QStringList movedPaths;
    if (!toRecycleBin) QDir().mkpath(destDir);
    for (int idx : doomed) {
        const QString p = dupResults_[idx].path;
        // Re-check right before moving: the dialog pumped the event
        // loop, the user may have acted in the meantime.
        if (!QFileInfo::exists(p)) continue;
        const bool ok = toRecycleBin
            ? QFile::moveToTrash(p)
            : moveFileKeepingName(p, destDir);
        if (ok) {
            ++deleted;
            movedPaths.append(p);
        } else {
            ++failed;
        }
    }

    if (!movedPaths.isEmpty()) {
        purgeStaleRows(movedPaths, QStringLiteral("duplicates delete"));
        updateIndexStats();
    }

    dupResults_.clear();
    dupKeys_.clear();
    resultsPane_->setAction(QString());

    statusBar()->showMessage(
        QString("%1 of %2 cop%3 %4%5")
            .arg(deleted)
            .arg(doomed.size())
            .arg(doomed.size() == 1 ? "y" : "ies")
            .arg(toRecycleBin
                ? QStringLiteral("moved to the Recycle Bin")
                : QStringLiteral("moved to %1").arg(destDir))
            .arg(failed > 0
                ? QString("; %1 could not be moved (missing, in use, "
                          "or the destination rejected them)")
                      .arg(failed)
                : QString()),
        8000);

    // Show the truth: re-run the check. Hashes were just written back,
    // so the reuse path makes this fast.
    onDetectDuplicates();
}

} // namespace DocuSearch
