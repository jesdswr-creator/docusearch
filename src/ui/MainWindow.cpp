// ============================================================
// MainWindow.cpp - Top-level window with custom title bar
// ============================================================

#include "MainWindow.h"
#include "Theme.h"
#include "IconUtils.h"
#include "ModernTooltip.h"
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
#include "../core/TextQuality.h"
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
#include "../core/ExtractionController.h"
#include "../core/MemoryMonitor.h"
#include "../core/GracefulDegradation.h"
#include "../core/SystemProfile.h"
#include "../core/TierConfig.h"
#include "../core/ScanPipelineController.h"
#include "../core/DuplicateScanController.h"
#include "../core/StorageHealth.h"
#include "../embeddings/EmbeddingController.h"
#include "../search/HybridSearchEngine.h"
#include "../settings/SettingsManager.h"
#include "FirstLaunchTierDetection.h"
#include "SystemHealthDashboard.h"
#include "TitleBarWidget.h"

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
#include <QThreadPool>   // v1.7.19: dedicated single-thread semantic-search pool
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
#include <QCheckBox>    // v1.7.17: close-confirm "don't ask again"
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
#include <QCursor>      // v1.7.17: ModernTooltip anchor position
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

// v1.7.26: keyword-coverage gate for AI-added documents. The user asked
// that semantic search only ADD documents that match ALL (or at least
// ~70%) of the typed keywords — a document matching 1 of 5 keywords is
// noise. Semantic-only rows (keywordScore == 0) are checked here against
// the file's filename + extracted text and dropped when too few of the
// query keywords actually appear. A file with no indexed text matches 0
// keywords (and is therefore dropped) — honest, since it can't keyword-
// match anyway. Returns the count of `keywords` present.
static int keywordMatchCount(sqlite3* raw, qint64 fileId,
                             const QStringList& keywords) {
    if (!raw || keywords.isEmpty()) return 0;

    QString hay;
    sqlite3_stmt* s = nullptr;
    const char* sql =
        "SELECT f.filename, COALESCE(d.extracted_text, '') "
        "FROM Files f LEFT JOIN DocumentText d ON d.file_id = f.id "
        "WHERE f.id = ?1;";
    if (sqlite3_prepare_v2(raw, sql, -1, &s, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(s, 1, fileId);
        if (sqlite3_step(s) == SQLITE_ROW) {
            const unsigned char* fn = sqlite3_column_text(s, 0);
            const unsigned char* tx = sqlite3_column_text(s, 1);
            if (fn) hay += QString::fromUtf8(
                               reinterpret_cast<const char*>(fn));
            hay += QLatin1Char(' ');
            if (tx) hay += QString::fromUtf8(
                               reinterpret_cast<const char*>(tx));
        }
        sqlite3_finalize(s);
    }

    if (hay.trimmed().isEmpty()) return 0;   // no text to verify → 0 matches

    // Case-folded substring match — language-agnostic (works for CJK and
    // other scripts where `\b` word boundaries do not exist) and consistent
    // with the FTS5 trigram tokenizer, which also matches substrings.
    const QString haystack = hay.toLower();
    int matched = 0;
    for (const QString& kw : keywords) {
        const QString k = kw.trimmed().toLower();
        if (!k.isEmpty() && haystack.contains(k)) ++matched;
    }
    return matched;
}

// ============================================================
// Constructor / destructor
// ============================================================
MainWindow::MainWindow(std::unique_ptr<Database> preopened, QWidget* parent)
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
    // v1.7.20: the Database normally arrives PRE-OPENED from main.cpp
    // (sqlite open + Schema::initialize/migrate ran on a background
    // thread while the splash animated). Only a null/unclean handoff
    // falls back to the inline open — identical to the old behavior.
    if (preopened && preopened->isOpen()) {
        db_ = std::move(preopened);
        // Affinity: the Database QObject was created on the UI thread in
        // main.cpp; only its open() RAN on the worker. Adopting the parent
        // here (same thread) is legal.
        db_->setParent(this);
    } else {
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
    }
    if (!repo_)
        repo_ = std::make_unique<FileRepository>(*db_, this);

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
    // v1.7.22: pool size is the MIN of Settings → Performance →
    // "Worker threads" and the detected tier's ocrWorkers (LowEnd=1,
    // Mid=2, HighEnd=4). Clamped to 1..4: each worker spawns its own
    // ocr helper process + PDFium rasterizer. Changing it takes effect
    // on the next launch (the pool's thread count is fixed at
    // construction; throttle settings stay live via setAppSettings).
    // v1.7.23 (audit C2/B3): the tier table is read ONCE here and its
    // feature gates actually take effect — the fields existed since the
    // tier system landed but nothing consumed them.
    const TierConfig tierCfg =
        TierConfigManager::getConfig(SystemProfiler::instance()->tier());
    const int tierOcr = qMax(1, tierCfg.ocrWorkers);
    liveIndexingEnabled_       = tierCfg.enableLiveIndexing;
    autoScanEnabled_           = tierCfg.enableAutoScan;
    duplicateDetectionEnabled_ = tierCfg.enableDuplicateDetection;
    autoBackupEnabled_         = tierCfg.enableBackupAutomatic;
    ocrPool_ = std::make_unique<OcrWorkerPool>(
        std::clamp(qMin(settings_.maxWorkerThreads, tierOcr), 1, 4), this);
    ocrPool_->setAppSettings(settings_);
    connect(ocrPool_.get(), &OcrWorkerPool::taskCompleted,
            this, &MainWindow::onOcrTaskCompleted);
    connect(ocrPool_.get(), &OcrWorkerPool::logMessage, this,
            [](const QString& m) { DS_INFO("OCR", m); });

    // ── v1.7.21: HEADLESS PIPELINE CONTROLLERS ──
    // The extraction + embedding state machines moved out of this
    // god-object into QtCore-only controllers that tst_Wiring can
    // construct headless against a temp database — the wiring test
    // suite that would have caught the "OCR pool declared but never
    // constructed" and "extraction gathered by nobody" bugs (both hid
    // exactly here). Construction is EAGER and audited at startup.
    extractionController_ = std::make_unique<ExtractionController>(this);
    extractionController_->setDatabase(db_.get());
    extractionController_->setFileRepository(repo_.get());
    extractionController_->setWorkerFn([](const QString& path, const QString& ext) {
        return DocumentExtractorRegistry::instance().extractByExtension(path, ext);
    });
    extractionController_->setPathGate([this](const QString& p) -> bool {
        // Settings-driven admissibility (the controller adds the
        // constant extension allowlist + existence checks itself).
        for (const QString& drive : settings_.indexedDrives) {
            if (p.startsWith(drive, Qt::CaseInsensitive)) {
                if (FileUtils::isUnderAny(p, settings_.excludedFolders))
                    return false;
                const QString e = FileUtils::extensionOf(p).toLower();
                return !normalizedExtSet(settings_.excludedExtensions).contains(e);
            }
        }
        return false;
    });
    extractionController_->setOcrHooks(
        [this](const QList<ExtractionController::ExtractionTodo>& l) -> int {
            if (!ocrPool_ || l.isEmpty()) return 0;
            QList<OcrTask> tasks;
            tasks.reserve(l.size());
            for (const auto& t : l)
                tasks.append(OcrTask{t.fileId, t.path, t.ext});
            ocrPool_->enqueueBatch(tasks);
            return tasks.size();
        },
        [this]() { if (ocrPool_) ocrPool_->clearQueue(); },
        [this]() -> int { return ocrPool_ ? ocrPool_->queueSize() : 0; });
    extractionController_->setFirstRunMode(!settings_.firstRunDone);
    // v1.7.23 (audit C2): tier pacing reaches the pipeline now. The
    // first-run session cap is the tier's indexingBatchSize (LowEnd 25 /
    // Mid 100 / HighEnd 200) and the per-file tick gains the tier's
    // indexingPauseMs (LowEnd: 700 ms/file instead of 200). Degradation
    // must restore the SAME base tick after pressure clears, not a
    // hardcoded 200.
    extractionController_->setFirstRunSessionCap(tierCfg.indexingBatchSize);
    extractionController_->setTickIntervalMs(200 + tierCfg.indexingPauseMs);

    embeddingController_ = std::make_unique<EmbeddingController>(this);
    embeddingController_->setDatabase(db_.get());

    // v1.7.24: the scan pipeline + the duplicate scanner — the third
    // and fourth pipelines extracted from this window. Constructed
    // EAGERLY (the wiring contract: everything declared is built, the
    // class of bug the v1.7.21 verifyWiring audit exists for) and
    // wired here so every pipeline→UI wire is visible in one block.
    scanPipeline_ = std::make_unique<ScanPipelineController>(this);
    connect(scanPipeline_.get(), &ScanPipelineController::autoScanFinished,
            this, &MainWindow::onAutoScanFinished);
    connect(scanPipeline_.get(), &ScanPipelineController::folderScanFinished,
            this, &MainWindow::onFolderScanFinished);
    connect(scanPipeline_.get(), &ScanPipelineController::integrityFinished,
            this, &MainWindow::onIntegrityFinished);
    connect(scanPipeline_.get(),
            &ScanPipelineController::purgeNonIndexableFinished,
            this, &MainWindow::onPurgeNonIndexableFinished);

    duplicateScan_ = std::make_unique<DuplicateScanController>(this);
    connect(duplicateScan_.get(), &DuplicateScanController::finished,
            this, &MainWindow::onDuplicateScanFinished);

    // v1.7.22: live RAM/CPU monitor + graceful degradation. Semantic
    // search stays on; OCR/extraction slow or pause under pressure.
    memoryMonitor_ = std::make_unique<MemoryMonitor>(this);
    connect(memoryMonitor_.get(), &MemoryMonitor::pressureWarning,
            this, &MainWindow::onMemoryPressureWarning);
    connect(memoryMonitor_.get(), &MemoryMonitor::pressureCritical,
            this, &MainWindow::onMemoryPressureCritical);
    connect(memoryMonitor_.get(), &MemoryMonitor::pressureRecovered,
            this, &MainWindow::onMemoryPressureRecovered);
    memoryMonitor_->startMonitoring();

    degradation_ = std::make_unique<GracefulDegradation>(this);
    degradation_->setOcrPool(ocrPool_.get());
    degradation_->setExtractionController(extractionController_.get());
    degradation_->setEmbeddingController(embeddingController_.get());
    degradation_->setHealthyTickMs(200 + tierCfg.indexingPauseMs);
    connect(degradation_.get(), &GracefulDegradation::degradationLevelChanged,
            this, [this](DegradationLevel lvl) {
        if (memoryStatusLbl_) {
            memoryStatusLbl_->setText(
                QStringLiteral("RAM: %1").arg(GracefulDegradation::levelName(lvl)));
        }
        statusBar()->showMessage(
            QStringLiteral("System pressure: %1")
                .arg(GracefulDegradation::levelName(lvl)), 4000);
    });
    degradation_->startMonitoring();

    // v1.7.10 FIRST-RUN EXTRACT-ALL: until the very first full extraction
    // drain completes (firstRunDone), extraction sessions run 200 files
    // and re-arm in 3 s, so a fresh index fully extracts itself right
    // after the first Add-Folder scan — the user never has to keep
    // clicking Extract to see content search working.
    // (v1.7.21: the flag lives in ExtractionController — set above.)
    if (extractionController_->firstRunMode())
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
    // v1.7.23 (audit C2): gated by the tier's enableAutoScan (every tier
    // ships with it on; the gate makes the config honest).
    autoScanTimer_ = new QTimer(this);
    autoScanTimer_->setInterval(3600 * 1000);  // 1 hour
    connect(autoScanTimer_, &QTimer::timeout, this, [this]{
        autoScanIndexedFolders();
    });
    if (autoScanEnabled_) {
        autoScanTimer_->start();
    } else {
        DS_INFO("MainWindow", "Auto-scan disabled by system tier config");
    }

    // Live index stats: refresh the "N indexed" badge every 20 s so the
    // number always reflects the database — it used to change only when a
    // specific event happened to call updateIndexStats, which read as a
    // frozen ("hard coded") figure while extraction was running.
    auto* statsRefreshTimer = new QTimer(this);
    statsRefreshTimer->setInterval(20 * 1000);
    connect(statsRefreshTimer, &QTimer::timeout,
            this, &MainWindow::updateIndexStats);
    statsRefreshTimer->start();

    // v1.7.23 B3 (audit): enableBackupAutomatic was dead tier config —
    // no automatic backup existed anywhere. Mid/HighEnd tiers now
    // checkpoint + zip the database into <data>/backups at most once
    // per 24 h (first chance at t+3 min, then every 6 h), keeping the
    // newest 7 zips. The UI thread only runs the fast WAL checkpoint;
    // the archive job runs on QtConcurrent and captures NO `this`, so
    // closing the window mid-backup is safe (the watcher dies with the
    // window, the worker only touches local strings + BackupManager).
    autoBackupWatcher_.setParent(this);
    connect(&autoBackupWatcher_, &QFutureWatcher<bool>::finished, this,
            [this]() {
                autoBackupInFlight_ = false;
                if (autoBackupWatcher_.result())
                    statusBar()->showMessage(
                        "Automatic database backup saved.", 4000);
            });
    if (autoBackupEnabled_) {
        auto* autoBackupTimer = new QTimer(this);
        connect(autoBackupTimer, &QTimer::timeout,
                this, &MainWindow::maybeRunAutomaticBackup);
        autoBackupTimer->start(6 * 3600 * 1000);
        QTimer::singleShot(3 * 60 * 1000, this,
                           &MainWindow::maybeRunAutomaticBackup);
    }

    // Startup diff: check for files that changed while app was closed.
    if (autoScanEnabled_) {
        QTimer::singleShot(2000, this, [this]() {
            if (extractionController_ && extractionController_->isRunning()) {
                return;  // extraction busy — the hourly scan will catch up
            }
            statusBar()->showMessage("Checking for file changes...", 3000);
            autoScanIndexedFolders();
        });
    }

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
    // checkbox was never consulted anywhere; the watcher always ran)
    // AND the tier allows live indexing (v1.7.23 audit C2: LowEnd
    // declares enableLiveIndexing=false — its changes are caught by the
    // hourly/auto scans instead of the watcher).
    if (settings_.monitorFileChanges && liveIndexingEnabled_
        && !settings_.indexedDrives.isEmpty()) {
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

    // ── v1.7.17: FIRST-RUN WELCOME ──
    // A brand-new library shows an empty search page — no hint that the
    // next step is adding a folder, and no hint that AI search arrives
    // LATER than keyword search. On the very first launch a welcome
    // dialog appears (deferred ~700 ms so the window paints first):
    // it offers the Add-Folder action up front and honestly explains
    // the pipeline: indexing fast -> keyword search immediately,
    // extraction + AI embedding take time -> full AI once embedded.
    if (!settings_.welcomeDone) {
        QTimer::singleShot(700, this, [this]() {
            if (!settings_.welcomeDone) showWelcomeDialog();
        });
    }

    // NOTE: Auto-extract on startup is DISABLED to prevent crashes.
    // Extraction only happens after Add Folder or manual Extract button click.

    // ── v1.7.21: controller → UI signal forwarders ──
    // The controllers are headless; every status message, progress
    // update, stats/preview refresh request and re-arm tick arrives
    // here as a signal. This connect block IS the wiring the startup
    // audit below cannot fully see — keep the two in sync.
    connect(extractionController_.get(), &ExtractionController::statusMessage, this,
            [this](const QString& m, int t) { statusBar()->showMessage(m, t); });
    connect(extractionController_.get(), &ExtractionController::extractingChanged, this,
            [this](bool on) { if (searchBar_) searchBar_->setExtracting(on); });
    connect(extractionController_.get(), &ExtractionController::progressUpdated, this,
            [this](int value, int max, bool visible) {
                if (!extractionProgressBar_) return;
                extractionProgressBar_->setRange(0, qMax(1, max));
                extractionProgressBar_->setValue(value);
                extractionProgressBar_->setVisible(visible);
            });
    // v1.7.23 C3 (audit): statsDirty fires per extracted file; each
    // tick used to run 3-5 COUNT queries on the UI thread. Coalesce to
    // one refresh at most every 1.5 s — the badges are progress
    // feedback, not telemetry.
    statsCoalesceTimer_ = new QTimer(this);
    statsCoalesceTimer_->setSingleShot(true);
    statsCoalesceTimer_->setInterval(1500);
    connect(statsCoalesceTimer_, &QTimer::timeout,
            this, &MainWindow::updateIndexStats);
    connect(extractionController_.get(), &ExtractionController::statsDirty,
            statsCoalesceTimer_, qOverload<>(&QTimer::start));
    connect(extractionController_.get(), &ExtractionController::previewDirty, this,
            [this]() { refreshPreviewForSelectedFile(); });
    connect(extractionController_.get(), &ExtractionController::firstRunDrainComplete, this,
            [this]() {
                settings_.firstRunDone = true;
                saveSettings();
            });
    connect(extractionController_.get(), &ExtractionController::backfillWakeRequested, this,
            [this]() {
                if (embeddingController_) embeddingController_->ensureBackfill();
            });
    connect(extractionController_.get(), &ExtractionController::autoRearmRequested, this,
            [this](int delayMs) {
                QTimer::singleShot(delayMs, this, [this]() {
                    // v1.7.4: fresh patience budget for this wake so a
                    // scan that happens to be running can never starve
                    // the queue.
                    autoExtractRetryLeft_ = 20;
                    requestAutoExtract();
                });
            });
    connect(embeddingController_.get(), &EmbeddingController::chipChanged,
            this, &MainWindow::setAiChip);
    connect(embeddingController_.get(), &EmbeddingController::statusMessage, this,
            [this](const QString& m, int t) { statusBar()->showMessage(m, t); });
    connect(embeddingController_.get(), &EmbeddingController::rebuildBlocked, this,
            [this](const QString& why) {
                QMessageBox::information(this, "AI Search", why);
            });

    // ── v1.7.21: STARTUP WIRING AUDIT ──
    // Every declared subsystem must actually be constructed. This one
    // check at startup would have caught the historical "OCR pool
    // declared but never constructed" bug on day one instead of after
    // a release; tst_Wiring asserts the same contract headless.
    {
        const bool wiringOk =
            db_ && repo_ && search_ && ocrPool_ && watcher_ &&
            extractionController_ && extractionController_->verifyWiring() &&
            embeddingController_ && embeddingController_->verifyWiring() &&
            memoryMonitor_ && degradation_;
        if (!wiringOk)
            DS_WARN("Startup", "WIRING AUDIT FAILED — a declared subsystem "
                               "was never constructed (see warnings above).");
        DS_INFO("Startup", QString("Wiring audit: %1")
                               .arg(wiringOk ? "OK" : "FAILED"));
    }

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
    // Stop monitors first so they cannot call into controllers that
    // we are about to shut down. Member dtor order already kills
    // degradation_ before the controllers; this just makes it explicit.
    if (degradation_) degradation_->stopMonitoring();
    if (memoryMonitor_) memoryMonitor_->stopMonitoring();
    if (autoScanTimer_) autoScanTimer_->stop();
    // v1.7.23 B3: an in-flight automatic backup must not outlive the
    // application (its QProcess would race the QCoreApplication
    // teardown). The worker captures no `this`, so a bounded wait is
    // all that is needed; if it truly cannot finish, the worst case is
    // a partial zip that the restore path's header check rejects.
    // (QFutureWatcher::waitForFinished() has no timeout overload, so
    // poll with a deadline instead.)
    if (autoBackupInFlight_ && autoBackupWatcher_.isRunning()) {
        DS_INFO("Backup", "Waiting for the automatic backup to finish...");
        const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 15000;
        while (autoBackupWatcher_.isRunning()
               && QDateTime::currentMSecsSinceEpoch() < deadline) {
            QThread::msleep(100);
        }
    }
    // v1.7.21: the pipelines live in the controllers — invalidate the
    // extraction session FIRST (a result landing during teardown must
    // write nothing; the generation check drops it) and drain its pool
    // while the database is still open.
    if (extractionController_) extractionController_->shutdownForTeardown();
    // v1.7.18: stop + join the background SEMANTIC SEARCH worker before
    // hybridSearch_/bgeService_ die — the worker captures `this` and uses
    // both. Mirrors the bgeInitFuture_ contract below.
    // v1.7.19: also cancel + drain the dedicated search pool — clear()
    // drops queued (not-yet-started) scans, then the join below waits
    // out the one in-flight scan (bounded: cooperative cancel is raised
    // first via searchWorkersStop_ checks in the worker entry).
    searchWorkersStop_.store(true);
    if (activeSemanticCancel_) activeSemanticCancel_->store(true);
    if (searchPool_) searchPool_->clear();
    if (semanticSearchFuture_.isValid()) semanticSearchFuture_.waitForFinished();
    if (searchPool_) searchPool_->waitForDone();
    // v1.7.11: join the BGE init worker — it captures `this` and uses
    // bgeService_, which is about to be destroyed with the window.
    // v1.7.21: that future runs on EmbeddingController's pool — join it
    // first, THEN drain the pool (clear() drops queued embedding
    // batches; the BGE service dtor joins any in-flight batch itself).
    if (bgeInitFuture_.isValid()) bgeInitFuture_.waitForFinished();
    if (embeddingController_) embeddingController_->shutdownForTeardown();
    if (ocrPool_) ocrPool_->shutdown();
    if (watcher_) watcher_->stop();
    if (db_)      db_->close();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    // ── v1.7.17: CONFIRM BEFORE CLOSING ──
    // One accidental click on the title-bar X (or Alt+F4) used to tear
    // everything down instantly — mid-scan, mid-extraction, no take-back.
    // A modern confirm dialog now stands in the way: it states plainly
    // that closing is SAFE (the library is saved, background work resumes
    // next launch), discloses any work still running, and offers a
    // "Don't ask again" switch that persists immediately.
    if (settings_.closeConfirmAsk) {
        QStringList busy;
        if (scanPipeline_ && scanPipeline_->isAutoScanRunning())
                                         busy << "a folder scan";
        if (extractionController_ && extractionController_->isRunning())
                                         busy << "text extraction";
        if (embeddingController_ && embeddingController_->isBackfillRunning())
                                         busy << "AI embedding";
        if (ocrPool_ && ocrWorkOutstanding()) busy << "OCR";

        QDialog dlg(this);
        dlg.setWindowTitle(QStringLiteral("Close DocuSearch?"));
        dlg.setMinimumWidth(440);
        auto* v = new QVBoxLayout(&dlg);
        v->setSpacing(10);

        auto* msg = new QLabel(
            QStringLiteral(
                "Close DocuSearch now?\n\n"
                "Closing is safe: your library is saved, and indexing, "
                "extraction and AI embedding resume automatically the next "
                "time you open DocuSearch."),
            &dlg);
        msg->setWordWrap(true);
        v->addWidget(msg);

        // Honest disclosure when background work is mid-flight.
        QLabel* busyLbl = nullptr;
        if (!busy.isEmpty()) {
            busyLbl = new QLabel(
                QStringLiteral(
                    "Heads-up: %1 %2 still running right now — closing "
                    "pauses %3 until the next launch.")
                    .arg(busy.join(QStringLiteral(", ")))
                    .arg(busy.size() == 1 ? "is" : "are")
                    .arg(busy.size() == 1 ? "it" : "them"),
                &dlg);
            busyLbl->setWordWrap(true);
            busyLbl->setStyleSheet(
                QStringLiteral("color:#b45309; font-weight:600;"));
            v->addWidget(busyLbl);
        }

        auto* dontAsk = new QCheckBox(
            QStringLiteral("Don't ask again — close immediately next time"),
            &dlg);
        v->addWidget(dontAsk);

        auto* row = new QHBoxLayout();
        row->addStretch();
        auto* cancelBtn = new QPushButton(QStringLiteral("Cancel"), &dlg);
        cancelBtn->setObjectName(QStringLiteral("secondaryBtn"));
        auto* closeBtn  = new QPushButton(QStringLiteral("Close"), &dlg);
        closeBtn->setObjectName(QStringLiteral("primaryBtn"));
        closeBtn->setDefault(true);
        row->addWidget(cancelBtn);
        row->addWidget(closeBtn);
        v->addLayout(row);

        connect(closeBtn,  &QPushButton::clicked, &dlg, &QDialog::accept);
        connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

        if (dlg.exec() != QDialog::Accepted) {
            e->ignore();   // stay open — nothing persisted, nothing stopped
            return;
        }
        if (dontAsk->isChecked()) {
            settings_.closeConfirmAsk = false;
            Config::instance().save(settings_);
        }
    }

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

    memoryStatusLbl_ = new QLabel(QStringLiteral("RAM: Healthy"), sb);
    memoryStatusLbl_->setObjectName(QStringLiteral("memoryStatus"));
    memoryStatusLbl_->setToolTip(
        QStringLiteral("Live memory-pressure state. Click Stats for the full health dashboard."));
    sb->addPermanentWidget(memoryStatusLbl_);

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
    // v1.7.17: the custom ModernTooltip reads the EXACT same tokens as the
    // QSS QToolTip rule, so the two tooltip paths stay one visual system.
    ModernTooltip::setStyle(QColor(tooltipBg), QColor(tooltipText),
                            QColor(tooltipBorder));

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
// (v1.7.24: the file-local helpers that used to live here —
// normalizedExtSet, storageRootReachable, hideStaleResults — moved
// to core/StorageHealth.h so the extracted scan/duplicate pipelines
// share ONE definition with the window.)

void MainWindow::onSearch(const QString& query) {
    if (!repo_ || !db_ || !search_) return;
    if (query.isEmpty()) {
        resultsPane_->clear();
        return;
    }
    try {
        // v1.7.18: TWO-STAGE SEARCH. Everything used to run here — FTS,
        // BGE query embedding, the chunk + document cosine scans, stale
        // disk checks, rendering — synchronously on the UI thread. On a
        // real library that froze input for seconds (user-measured
        // 3.6 s: "it defeat the purpose of this app"). Now stage 1 shows
        // KEYWORD results immediately (FTS5 is milliseconds), the heavy
        // semantic scan runs on the global thread pool, and
        // applySemanticResults() merges it in when it lands. searchGen_
        // discards results of superseded queries.
        const quint64 gen = ++searchGen_;
        QElapsedTimer t; t.start();
        lastSearchLatencyMs_ = 0;

        // ---- Stage 1: keyword (FTS5 BM25) results, RIGHT NOW ----
        auto hits = search_->search(query, 50);  // limit to top 50 results

        // v1.7.15: what the AI actually sees is the query's NATURAL-
        // LANGUAGE text (words + quoted phrases only). The raw search-bar
        // string carries FTS5 syntax — type:pdf, AND/OR/NOT, -draft,
        // rail* — that BGE-small-en-v1.5 has never seen; embedding it
        // verbatim steered the query vector away from the words the user
        // actually typed and surfaced unrelated documents as "AI
        // matches". That was the "AI is not listening to my words" report.
        const auto parsed = QueryParser::parse(query);
        const QString semanticQuery = parsed.semanticText;

        // Semantic scan is wanted when AI is on, the BGE service is
        // ready, and the query has natural-language words to embed. A
        // pure-filter query like "type:pdf" has no words — keyword-only.
        const bool wantSemantic = semanticEnabled_ && bgeService_
            && bgeService_->isReady() && hybridSearch_
            && !semanticQuery.isEmpty() && !searchWorkersStop_.load();

        // v1.7.3/1.7.4: hide stale entries (file deleted/moved while the
        // app was closed) before display, then PURGE the rows whose
        // drive is still reachable so they never come back.
        const QStringList stalePaths = hideStaleResults(hits);
        const int staleHidden = stalePaths.size();
        const int stalePurged = purgeStaleRows(stalePaths,
                                               QStringLiteral("keyword search"));
        // v1.7.18: the pane is being reused for search results — drop any
        // duplicates-view state here (ResultsPane disarms its own delete
        // action on setResults). This also lets the late semantic merge
        // detect that the user switched to the duplicates view while the
        // scan was running, and refuse to stomp it (see the guard there).
        dupResults_.clear();
        dupKeys_.clear();
        resultsPane_->setResults(hits);

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

        if (!wantSemantic) {
            lastSearchLatencyMs_ = t.elapsed();
            // Keyword-only search (existing behavior).
            resultsPane_->setAiSummary(QString());
            statusBar()->showMessage(
                QString("%1 result%2 in %3 ms")
                    .arg(hits.size())
                    .arg(hits.size() == 1 ? "" : "s")
                    .arg(lastSearchLatencyMs_));
        } else {
            // ---- Stage 2: semantic scan on the global thread pool. The
            // user is already looking at keyword results; the AI merge
            // arrives via applySemanticResults() when ready. The status
            // line says so, honestly, in real time.
            statusBar()->showMessage(
                QString("%1 result%2 in %3 ms — AI ranking in background…")
                    .arg(hits.size())
                    .arg(hits.size() == 1 ? "" : "s")
                    .arg(t.elapsed()));
            resultsPane_->setAiSummary(QString(
                "<b>Keyword results shown.</b> AI ranking is running in "
                "the background — documents above the similarity bar are "
                "merged in as they arrive. Your keyword order never "
                "changes."));

            // Convert SearchHit → ExistingSearchResult for the engine.
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

            // v1.7.18: the query embedding + chunk/document cosine scans
            // run OFF the UI thread. semanticSearchMutex_ serializes
            // overlapping scans on the shared HybridSearchEngine (a
            // second setTypeFilter+search must never interleave with the
            // first); searchWorkersStop_ + the dtor join make sure a
            // worker can never touch a dying engine — the same lifetime
            // contract as bgeInitFuture_.
            //
            // v1.7.19 latency work, three layers:
            //   1. DEDICATED POOL — the scan runs on a private single-
            //      thread pool, NOT the global QtConcurrent pool it used
            //      to share with extraction/backfill workers (a busy
            //      folder scan used to delay the AI result by seconds
            //      before the scan even started).
            //   2. CANCEL-ON-NEW-QUERY — the previous search's flag is
            //      raised here, so the running/queued superseded scan
            //      aborts within one batch instead of the new query
            //      waiting behind the mutex for the whole old scan.
            //   3. The scans themselves are 10-100x faster (rowid
            //      cursor + dot-only kernel in BgeEmbeddingDb) and the
            //      query embedding is LRU-cached in BgeService.
            if (!searchPool_) {
                searchPool_ = new QThreadPool(this);
                searchPool_->setMaxThreadCount(1);
            }
            if (activeSemanticCancel_) activeSemanticCancel_->store(true);
            activeSemanticCancel_ = std::make_shared<std::atomic<bool>>(false);
            auto cancelFlag = activeSemanticCancel_;

            auto* watcher =
                new QFutureWatcher<std::vector<HybridResult>>(this);
            connect(watcher,
                    &QFutureWatcher<std::vector<HybridResult>>::finished,
                    this,
                    [this, watcher, gen, query, hits, t]() {
                watcher->deleteLater();
                if (searchWorkersStop_.load()) return;
                if (gen != searchGen_) return;  // superseded — newer query owns the UI
                applySemanticResults(query, hits, watcher->result(),
                                     t.elapsed());
            });
            const QString typeFilter = parsed.typeFilter;
            semanticSearchFuture_ = QtConcurrent::run(
                searchPool_,
                [this, semanticQuery, keywordResults, typeFilter, cancelFlag]()
                    -> std::vector<HybridResult> {
                if (cancelFlag->load() || searchWorkersStop_.load()) return {};
                QMutexLocker lock(&semanticSearchMutex_);
                if (cancelFlag->load() || searchWorkersStop_.load()) return {};
                hybridSearch_->setTypeFilter(typeFilter);
                // The AI embeds the CLEAN semantic query text — never
                // the raw search-bar string (see the v1.7.15 note).
                return hybridSearch_->search(semanticQuery, keywordResults,
                                             cancelFlag.get());
            });
            watcher->setFuture(semanticSearchFuture_);
        }

        // Highlight search terms in the extracted text pane (yellow).
        // This makes it easy for users to find the relevant parts of
        // the document after clicking a result.
        if (previewPane_) {
            previewPane_->setSearchQuery(query);
        }
    } catch (...) {
        statusBar()->showMessage("Search error - try a different query");
    }
}

// v1.7.18: landing point of stage 2 (see onSearch). The semantic scan
// finished on the global thread pool; merge its results with the keyword
// list already on screen — keyword order is never touched, AI additions
// are appended after it — and render. Runs on the UI thread, delivered
// by the QFutureWatcher, only when the generation still matches.
void MainWindow::applySemanticResults(const QString& query,
                                      const QList<SearchHit>& keywordHits,
                                      std::vector<HybridResult> hybridResults,
                                      qint64 totalMs) {
    // v1.7.18: if the user switched the pane to the duplicates view while
    // the semantic scan was in flight, never stomp it with a late search
    // result. (Stage 1 of onSearch clears dupKeys_; only a duplicates run
    // started AFTER that can have re-populated it.)
    if (!dupKeys_.isEmpty()) return;
    lastSearchLatencyMs_ = totalMs;

    // v1.7.26: keyword-coverage gate for AI-added documents. Semantic-only
    // rows (keywordScore == 0, semanticScore > 0) are kept only when the
    // file's filename/extracted text actually contains >= 70% of the
    // query's meaningful keywords — the user's "don't bring a single
    // keyword match out of 4-5 keywords". Keyword results (strict-AND
    // matches) always pass; pure-filter queries (no keywords) skip the
    // gate entirely.
    const QStringList keywords = SearchEngine::splitSearchWords(query);
    std::vector<HybridResult> kept;
    kept.reserve(hybridResults.size());
    for (auto& hr : hybridResults) {
        if (!keywords.isEmpty()
            && hr.semanticScore > 0.01f && hr.keywordScore < 0.01f) {
            // ceil(0.70 * N) in integer math: 1→1, 2→2, 3→3, 4→3, 5→4, …
            const int required = (keywords.size() * 7 + 9) / 10;
            const int matched = keywordMatchCount(
                db_ ? db_->raw() : nullptr, hr.fileId, keywords);
            if (matched < required) continue;   // too few keywords → drop
        }
        kept.push_back(std::move(hr));
    }

    QList<SearchHit> merged;
    merged.reserve(static_cast<int>(kept.size()));
    for (const auto& hr : kept) {
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
        // The badge makes the AI contribution visible so the user can
        // SEE when AI is working.
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
        // Phase 2 BUGFIX: keep the AI badge AND append the original
        // keyword snippet so the user sees both.
        for (const auto& orig : keywordHits) {
            if (orig.fileId == hr.fileId) {
                h.size          = orig.size;
                h.modifiedDate  = orig.modifiedDate;
                h.isFavorite    = orig.isFavorite;
                if (!orig.snippet.isEmpty()) {
                    h.snippet = h.snippet + "<br>" + orig.snippet;
                }
                break;
            }
        }
        // Semantic-only hits (AI found the document, keywords did not)
        // carry NO metadata through the fusion layer: no extension, no
        // size, no date. Backfill straight from disk.
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
    // v1.7.3/1.7.4: hide stale entries (a file can vanish while the
    // semantic scan is in flight), then purge the purgeable ones.
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
    // Phase 4: surface the AI contribution honestly. The fusion is
    // strictly ADDITIVE — keyword results are never reordered or
    // dropped — so the status line reports the keyword count and the
    // AI-only additions separately.
    int aiContribCount = 0;
    int aiOnlyCount = 0;
    for (const auto& hr : kept) {
        if (hr.semanticScore > 0.01f) ++aiContribCount;
        if (hr.semanticScore > 0.01f && hr.keywordScore < 0.01f) ++aiOnlyCount;
    }
    statusBar()->showMessage(
        QString("%1 result%2 · keyword %3 · AI-found %4 · %5 ms")
            .arg(merged.size())
            .arg(merged.size() == 1 ? "" : "s")
            .arg(merged.size() - aiOnlyCount)
            .arg(aiOnlyCount)
            .arg(totalMs));
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
            // Nothing was comparable this pass. Only blame the chunk
            // backfill while work is genuinely pending.
            const qint64 pendingDocs   = embeddingController_
                                             ? embeddingController_->countMissingEmbeddings()
                                             : 0;
            const qint64 pendingChunks = embeddingController_
                                             ? embeddingController_->countMissingChunkDocs()
                                             : 0;
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
// Folder scan: v1.7.24 — the walk lives in ScanPipelineController
// (startFolderScan on its own worker + sqlite connection). The old
// synchronous scanFolderFast pumped processEvents every 10 files —
// the app's last big UI-thread re-entrancy surface. The window only
// reports what onFolderScanFinished receives.
// ============================================================

// ============================================================
// v1.7.17: FIRST-RUN WELCOME — onboarding + honest expectations
// ============================================================
// The first launch used to drop the user on an empty search page with
// only a status-bar hint. This dialog does three things:
//   1. Makes the next action obvious: add your first folder.
//   2. Sets expectations BEFORE the user wonders whether something is
//      broken: indexing finishes in seconds and keyword search works
//      immediately, while extraction + AI embedding keep running in
//      the background — full AI search by meaning arrives as vectors
//      complete (the Extracted / Embedded badges show the progress).
//   3. Runs ONCE: welcomeDone is persisted the moment the dialog is
//      answered, whichever button was used.
void MainWindow::showWelcomeDialog() {
    if (settings_.welcomeDone) return;

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Welcome to DocuSearch"));
    dlg.setMinimumWidth(500);
    auto* v = new QVBoxLayout(&dlg);
    v->setContentsMargins(24, 22, 24, 18);
    v->setSpacing(8);

    const SystemProfile profile = SystemProfiler::instance()->profile();
    auto* tierLine = new QLabel(
        QStringLiteral("Detected: %1 · %2 GB RAM · %3 cores%4")
            .arg(SystemProfiler::tierName(profile.tier))
            .arg(profile.totalRAM / (1LL << 30))
            .arg(profile.cpuCores)
            .arg(profile.hasSSD ? QStringLiteral(" · SSD") : QStringLiteral(" · HDD")),
        &dlg);
    tierLine->setWordWrap(true);
    v->addWidget(tierLine);

    auto* head = new QLabel(QStringLiteral("Welcome to DocuSearch"), &dlg);
    head->setStyleSheet(QStringLiteral(
        "font-size:19px; font-weight:800; background:transparent; color:%1;")
        .arg(Theme::active().primaryStrong));
    v->addWidget(head);

    auto* sub = new QLabel(
        QStringLiteral("Add your first folder to build your searchable library."),
        &dlg);
    sub->setWordWrap(true);
    v->addWidget(sub);
    v->addSpacing(6);

    // ---- The pipeline, explained honestly (dot + head + one-liner) ----
    struct Step { QColor dot; QString head; QString body; };
    const QColor primary = Theme::active().primary;
    const QList<Step> steps = {
        { QColor("#10b981"),
          QStringLiteral("Indexing — fast"),
          QStringLiteral("Your files are discovered and listed within "
                         "seconds of adding a folder.") },
        { primary,
          QStringLiteral("Keyword search — works right away"),
          QStringLiteral("Search by file name immediately; full-text "
                         "results keep filling in as text extraction "
                         "progresses automatically.") },
        { QColor("#8b5cf6"),
          QStringLiteral("Extraction + AI embedding — takes time"),
          QStringLiteral("DocuSearch reads every file (OCR included) and "
                         "builds AI vectors in the background. Full AI "
                         "search by meaning turns on as embedding "
                         "completes — the Extracted / Embedded badges in "
                         "the top-right show the progress.") },
    };
    for (const Step& s : steps) {
        auto* rowW = new QWidget(&dlg);
        auto* h = new QHBoxLayout(rowW);
        h->setContentsMargins(0, 2, 0, 2);
        h->setSpacing(10);
        auto* dot = new QLabel(rowW);
        dot->setFixedSize(12, 12);
        dot->setStyleSheet(QStringLiteral(
            "background:%1; border-radius:6px; min-width:12px; max-width:12px;")
            .arg(s.dot.name()));
        dot->setAlignment(Qt::AlignVCenter | Qt::AlignHCenter);
        h->addWidget(dot, 0, Qt::AlignTop);

        auto* txt = new QLabel(
            QStringLiteral("<b>%1</b><br>%2").arg(s.head, s.body), rowW);
        txt->setWordWrap(true);
        txt->setTextFormat(Qt::RichText);
        h->addWidget(txt, 1);
        v->addWidget(rowW);
    }
    v->addSpacing(4);

    auto* foot = new QLabel(
        QStringLiteral("You can add more folders any time from the Search bar."),
        &dlg);
    foot->setStyleSheet(QStringLiteral(
        "font-size:11px; background:transparent; color:%1;")
        .arg(Theme::active().muted));
    v->addWidget(foot);
    v->addSpacing(6);

    auto* row = new QHBoxLayout();
    row->addStretch();
    auto* laterBtn = new QPushButton(QStringLiteral("I'll add a folder later"), &dlg);
    laterBtn->setObjectName(QStringLiteral("secondaryBtn"));
    auto* addBtn   = new QPushButton(QStringLiteral("Add my first folder"), &dlg);
    addBtn->setObjectName(QStringLiteral("primaryBtn"));
    addBtn->setDefault(true);
    row->addWidget(laterBtn);
    row->addWidget(addBtn);
    v->addLayout(row);

    bool addChosen = false;
    connect(addBtn, &QPushButton::clicked, &dlg, [&]() {
        addChosen = true;
        dlg.accept();
    });
    connect(laterBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    dlg.exec();

    // One-time by design: whichever way the dialog closes (button or X),
    // onboarding is complete and never interrupts a launch again.
    settings_.welcomeDone = true;
    Config::instance().save(settings_);

    if (addChosen) onAddFolder();   // straight into the folder picker
}

void MainWindow::onMemoryPressureWarning() {
    if (memoryStatusLbl_)
        memoryStatusLbl_->setText(QStringLiteral("RAM: Warning"));
    statusBar()->showMessage(
        QStringLiteral("Memory is tight — extraction slowed. Search stays live."), 5000);
}

void MainWindow::onMemoryPressureCritical() {
    if (memoryStatusLbl_)
        memoryStatusLbl_->setText(QStringLiteral("RAM: Critical"));
    statusBar()->showMessage(
        QStringLiteral("Low memory — OCR paused. Keyword + AI search still work."), 6000);
}

void MainWindow::onMemoryPressureRecovered() {
    if (memoryStatusLbl_)
        memoryStatusLbl_->setText(QStringLiteral("RAM: Healthy"));
    statusBar()->showMessage(QStringLiteral("Memory recovered — background work resumed."), 4000);
    if (embeddingController_) embeddingController_->ensureBackfill();
}

// v1.7.23 B3 (audit): automatic daily database backup for tiers that
// declare enableBackupAutomatic (Mid/HighEnd). At most one backup per
// 24 h; skipped while a scan or extraction session is running (the
// next 6 h tick retries). The archive job runs off the UI thread -
// BackupManager touches no database handle, keeping the main
// connection UI-thread-only per the audit's threading rules.
void MainWindow::maybeRunAutomaticBackup() {
    if (!autoBackupEnabled_ || autoBackupInFlight_) return;
    if (!db_ || !db_->isOpen()) return;
    if ((scanPipeline_ && scanPipeline_->isAutoScanRunning())
        || (extractionController_ && extractionController_->isRunning())) {
        return;  // busy pipeline - the next tick retries
    }
    QSettings qs(QSettings::IniFormat, QSettings::UserScope,
                 "DocuSearch", "DocuSearch");
    const qint64 last = qs.value("lastAutoBackupMs").toLongLong();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (last > 0 && now - last < 24LL * 3600 * 1000) return;

    const QString dbPath = Config::instance().dbPath();
    if (QFileInfo(dbPath).size() <= 0) return;   // nothing to back up yet

    // Flush the WAL into the main file first (fast, UI thread) so the
    // zipped database carries the most recent writes.
    db_->exec("PRAGMA wal_checkpoint(TRUNCATE);");

    const QString dir = Config::instance().dataDir()
                        + QStringLiteral("/backups");
    autoBackupInFlight_ = true;
    autoBackupWatcher_.setFuture(QtConcurrent::run(
        [dbPath, dir]() -> bool {
            BackupManager bm;
            const QString out = bm.backup(dbPath, dir);
            if (out.isEmpty()) {
                DS_WARN("Backup", "Automatic backup failed");
                return false;
            }
            // Keep only the newest 7 zips so automatic backups can
            // never fill the disk.
            const QStringList all = bm.listBackups(dir);
            for (int i = 7; i < all.size(); ++i)
                QFile::remove(all.at(i));
            // QSettings is reentrant; this instance lives only inside
            // this worker thread.
            QSettings qs(QSettings::IniFormat, QSettings::UserScope,
                         "DocuSearch", "DocuSearch");
            qs.setValue("lastAutoBackupMs",
                        QDateTime::currentMSecsSinceEpoch());
            DS_INFO("Backup", "Automatic backup: " + out);
            return true;
        }));
}

HealthMetrics MainWindow::collectHealthMetrics() const {
    HealthMetrics m;
    if (memoryMonitor_) {
        m.ramFreePercent = memoryMonitor_->percentageFree();
        m.cpuUsagePercent = memoryMonitor_->cpuPercent();
    }
    if (extractionController_)
        m.indexingSpeedFilesPerMin = extractionController_->filesPerMinute();
    m.searchLatencyMs = static_cast<int>(lastSearchLatencyMs_);
    if (repo_) {
        qint64 extracted = -1, embedded = -1;
        repo_->countExtractedAndEmbedded(extracted, embedded);
        if (extracted > 0 && embedded >= 0)
            m.semanticCoveragePercent = static_cast<int>((embedded * 100) / extracted);
    }
    if (degradation_)
        m.currentDegradationLevel = GracefulDegradation::levelName(degradation_->level());
    else
        m.currentDegradationLevel = QStringLiteral("Healthy");
    m.tierName = SystemProfiler::tierName(SystemProfiler::instance()->tier());
    return m;
}

void MainWindow::showStatsAndHealth() {
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Index statistics & system health"));
    dlg.setMinimumWidth(480);
    auto* v = new QVBoxLayout(&dlg);

    qint64 dbBytes = 0;
    {
        const QString base = Config::instance().dbPath();
        for (const QString& p : { base, base + QStringLiteral("-wal"),
                                  base + QStringLiteral("-shm") }) {
            QFile f(p);
            if (f.exists()) dbBytes += f.size();
        }
    }
    auto* summary = new QLabel(
        QStringLiteral("Total files: %1\nDatabase size: %2")
            .arg(repo_ ? repo_->totalFiles() : 0)
            .arg(Utils::formatFileSize(dbBytes)),
        &dlg);
    summary->setWordWrap(true);
    v->addWidget(summary);

    auto* dash = new SystemHealthDashboard(&dlg);
    dash->setMetricsProvider([this]() { return collectHealthMetrics(); });
    dash->updateMetrics(collectHealthMetrics());
    dash->startMonitoring();
    v->addWidget(dash);

    auto* row = new QHBoxLayout();
    row->addStretch();
    auto* profileBtn = new QPushButton(QStringLiteral("System profile…"), &dlg);
    auto* closeBtn = new QPushButton(QStringLiteral("Close"), &dlg);
    closeBtn->setDefault(true);
    row->addWidget(profileBtn);
    row->addWidget(closeBtn);
    v->addLayout(row);

    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(profileBtn, &QPushButton::clicked, &dlg, [this, &dlg]() {
        FirstLaunchTierDetection wizard(SystemProfiler::instance()->profile(), &dlg);
        wizard.exec();
    });

    dlg.exec();
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

        // v1.7.4: watch the folder LIVE. addWatches() ran once at startup
        // only, so folders added through this button were scanned but not
        // monitored — later changes in them went unseen until the next
        // hourly scan (another "deleted files still show up" contributor).
        // v1.7.23: tier gate — LowEnd scans hourly instead (audit C2).
        if (watcher_ && liveIndexingEnabled_ && !watcher_->isWatched(folder)) {
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

        // v1.7.24: the walk runs on the ScanPipelineController worker
        // (own sqlite connection). Completion — the status report and
        // the auto-extract wake — arrives via onFolderScanFinished.
        scanPipeline_->startFolderScan(folder, currentScanParams());
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
        showStatsAndHealth();
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
    // v1.7.21: the session state machine (todo gathering, session cap,
    // 200 ms driver tick, pool hand-off, watcher continuation, cancel,
    // OCR accounting) lives in ExtractionController — headless and
    // wiring-tested in tests/tst_Wiring.cpp. This slot is the Extract
    // button's entry point (it doubles as "Stop Extracting" inside the
    // controller).
    if (extractionController_) extractionController_->startFromDatabase();
}

void MainWindow::autoScanIndexedFolders() {
    if (!scanPipeline_ || !repo_ || !db_) return;

    // v1.7.3: the old guard DROPPED the hourly tick silently whenever an
    // extraction session or a full re-index was busy - with long runs
    // that made the scan "never happen". Retry shortly after instead of
    // losing the tick.
    if (extractionController_ && extractionController_->isRunning()) {
        QTimer::singleShot(10 * 60 * 1000, this, [this]{
            autoScanIndexedFolders();
        });
        return;
    }

    // CRITICAL: Only scan the folders the user explicitly added via
    // Settings -> Indexing -> Indexed Drives (see v1.6 notes for why
    // DB-derived folders were wrong).
    if (settings_.indexedDrives.isEmpty()) return;

    statusBar()->showMessage("Auto-scanning indexed folders...");
    // v1.7.24: the walk (and its >30-min watchdog) live in the
    // controller; the result arrives via onAutoScanFinished.
    scanPipeline_->startAutoScan(currentScanParams());
}

void MainWindow::onAutoScanFinished(const DocuSearch::ScanStats& stats) {
    updateIndexStats();

    const QString unavailableNote = stats.unavailable > 0
        ? QStringLiteral(" (%1 folder%2 unavailable)")
              .arg(stats.unavailable)
              .arg(stats.unavailable == 1 ? "" : "s")
        : QString();

    if (stats.newFiles > 0 || stats.updatedFiles > 0) {
        statusBar()->showMessage(
            QStringLiteral("Auto-scan complete: %1 new, %2 changed, %3 "
                           "removed%4")
                .arg(stats.newFiles)
                .arg(stats.updatedFiles)
                .arg(stats.removedFiles)
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
                .arg(stats.removedFiles).arg(unavailableNote),
            5000);
    }
}

void MainWindow::onFolderScanFinished(int indexed, int skipped, int hashed) {
    updateIndexStats();
    statusBar()->showMessage(
        QString("Scan complete: %1 files indexed, %2 skipped%3 - "
                "starting auto-extraction...")
            .arg(indexed)
            .arg(skipped)
            .arg(hashed > 0 ? QString(
                ", %1 fingerprint%2 computed")
                .arg(hashed).arg(hashed == 1 ? "" : "s") : QString()),
        5000);
    // Both Add-Folder and the Settings new-drive path used to wake the
    // extraction pipeline right after their (synchronous) scan; now
    // the wake lives exactly once, where the scan actually finished.
    QTimer::singleShot(500, this, [this]() {
        autoExtractRetryLeft_ = 20;  // fresh budget (see requestAutoExtract)
        requestAutoExtract();
    });
}

// v1.7.24: the ScanParams bundle, read fresh from the CURRENT settings
// at every call site (the controller receives values only).
DocuSearch::ScanParams MainWindow::currentScanParams() const {
    DocuSearch::ScanParams p;
    p.folders            = settings_.indexedDrives;
    p.excludedFolders    = settings_.excludedFolders;
    p.excludedExtensions = settings_.excludedExtensions;
    p.dbPath             = Config::instance().dbPath();
    p.hashEnabled        = settings_.hashLargeFiles;
    return p;
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
    if (extractionController_ && extractionController_->isRunning()) return;
    // The startup/hourly scan walks the very files we would extract and
    // writes to its own DB connection. Rather than racing it, wait; its
    // finished handler wakes extraction when work exists anyway.
    if (scanPipeline_ && scanPipeline_->isAutoScanRunning()) {
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
// v1.7.21: those writes + accounting live in
// ExtractionController::noteOcrResult (headless, wiring-tested); this
// slot stays as the OCR pool's delivery target and forwards.
// ============================================================
void MainWindow::onOcrTaskCompleted(qint64 fileId, const QString& text, bool ok) {
    if (extractionController_)
        extractionController_->noteOcrResult(fileId, text, ok);
}

bool MainWindow::ocrWorkOutstanding() const {
    return extractionController_ && extractionController_->ocrWorkOutstanding();
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
    if (!scanPipeline_ || !db_) return;
    // v1.7.24: the whole pass (fake-done requeue, failed-row retry,
    // one-time junk-text audit, hash backfill) runs on the controller's
    // worker thread with its own sqlite connection — it used to run on
    // the UI thread, pumping processEvents every 100/500/25 rows.
    // junkTextAuditDone is the caller-owned one-shot flag: the
    // controller reports whether the audit actually scanned; the flag
    // is persisted in onIntegrityFinished.
    scanPipeline_->startIntegrityPass(currentScanParams(),
                                      !settings_.junkTextAuditDone);
}

void MainWindow::onIntegrityFinished(const DocuSearch::IntegrityResult& r) {
    if (r.junkAuditScanned && !settings_.junkTextAuditDone) {
        settings_.junkTextAuditDone = true;
        Config::instance().save(settings_);
        DS_INFO("Index", QString("Junk-text audit complete: %1 poisoned "
                                 "rows requeued for re-extraction (OCR "
                                 "re-reads them with auto-orientation)")
                                 .arg(r.junkRequeued));
    }
    if (r.requeued > 0 || r.hashed > 0 || r.failedRequeued > 0 ||
        r.junkRequeued > 0) {
        statusBar()->showMessage(
            QString("Index repair: %1 file%2 requeued for extraction, "
                    "%3 hash%4 computed.")
                .arg(r.requeued + r.failedRequeued + r.junkRequeued)
                .arg(r.requeued + r.failedRequeued + r.junkRequeued == 1
                         ? "" : "s")
                .arg(r.hashed)
                .arg(r.hashed == 1 ? "" : "es"), 10000);
        updateIndexStats();
        if (r.requeued > 0 || r.failedRequeued > 0 || r.junkRequeued > 0) {
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
    // v1.7.24: the cascade lives in ScanPipelineController (static, runs
    // on the CALLER'S thread over the main connection — the Settings
    // removal action is small and bounded); the window adds the stats
    // refresh and the log the old code emitted after the delete.
    const qint64 purged =
        ScanPipelineController::purgeFolderFromIndex(*db_, folder);
    if (purged > 0) updateIndexStats();
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
void MainWindow::purgeNonIndexableRows() {
    if (!scanPipeline_ || !db_) return;
    // v1.7.24: async on the controller's worker (was a UI-thread batch
    // loop pumping processEvents between 500-row commits). Batched
    // commits survive a kill exactly as before; the result arrives via
    // onPurgeNonIndexableFinished.
    scanPipeline_->startPurgeNonIndexable(Config::instance().dbPath());
}

void MainWindow::onPurgeNonIndexableFinished(int purged) {
    if (purged <= 0) return;
    updateIndexStats();
    statusBar()->showMessage(
        QString("Index cleanup: removed %1 non-document entries "
                "(md, txt, exe, archives...) — only documents and "
                "images are indexed now.").arg(purged), 10000);
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
    //    v1.7.21: the pipelines live in the controllers — invalidate
    //    their sessions and raise the db-reset flag their late results
    //    check, then clear the OCR queue here.
    if (extractionController_) {
        extractionController_->setDbResetting(true);
        extractionController_->invalidateSession();
    }
    if (embeddingController_) embeddingController_->stopAll();
    if (ocrPool_) ocrPool_->clearQueue();
    if (searchBar_) searchBar_->setExtracting(false);
    if (extractionProgressBar_) extractionProgressBar_->setVisible(false);
    dbResetting_ = true;   // late watcher-driven writes become no-ops

    // 1.5) v1.7.24: the scan pipeline holds its OWN sqlite connection
    // to this file — on Windows an open connection blocks the delete
    // below (sharing violation) and the reset would degrade into the
    // "could not be deleted" warning path. Stop the hourly tick (a
    // queued tick must not start a fresh job while we drain), then
    // wait BOUNDED for any running scan job. The pumps keep the
    // window responsive and deliver the controllers' finished
    // signals; a job that refuses to end inside 10 s falls through to
    // the honest warning path instead of hanging the reset forever.
    if (autoScanTimer_) autoScanTimer_->stop();
    if (scanPipeline_ && scanPipeline_->isBusy()) {
        QElapsedTimer drain; drain.start();
        while (scanPipeline_->isBusy() && drain.elapsed() < 10000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        if (scanPipeline_->isBusy())
            DS_WARN("Database", "Scan job still running after 10 s — "
                                "the reset may report a locked file.");
    }

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
        if (extractionController_) extractionController_->setDbResetting(false);
        DS_ERROR("Database", "Reopen after remove failed: " + err);
        QMessageBox::critical(this, "Remove Database",
            QStringLiteral("The database was removed but reopening "
                           "failed:\n\n%1\n\nRestart DocuSearch — a fresh "
                           "database will be created.").arg(err));
        return;
    }
    Schema::initialize(*db_);

    dbResetting_ = false;
    // v1.7.21: re-point the controllers at the fresh database before
    // anything can write again.
    if (extractionController_) {
        extractionController_->setDatabase(db_.get());
        extractionController_->setDbResetting(false);
    }
    if (embeddingController_) embeddingController_->setDatabase(db_.get());

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
    if (autoScanEnabled_ && autoScanTimer_) autoScanTimer_->start();
    autoScanIndexedFolders();
    autoExtractRetryLeft_ = 20;
    QTimer::singleShot(4000, this, [this]() { requestAutoExtract(); });
}

// ============================================================
// OCR status indicator
// ============================================================
QString MainWindow::getExtractionStatusString() {
    // v1.7.21: the query lives in ExtractionController (unit-tested).
    return extractionController_ ? extractionController_->extractionStatusString()
                                 : QString("Database not open.");
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
    // ── v1.7.17: MODERN TOOLTIP PRESENTATION ──
    // The native QTipLabel is a square, system-styled, instant window —
    // it never matched the app's Fluent glass look (the old workaround
    // below only made its corners translucent). We now intercept the
    // ToolTip event app-wide and show our own ModernTooltip instead:
    // rounded glass card, hairline border, soft shadow, fade+slide in,
    // edge-aware positioning. Widgets with an EMPTY toolTip() fall
    // through untouched — that is how QMenu action tooltips (delivered
    // on the menu itself) keep their native path and the QSS chip.
    if (e->type() == QEvent::ToolTip && obj->isWidgetType()) {
        auto* wgt = static_cast<QWidget*>(obj);
        const QString tip = wgt->toolTip();
        if (!tip.trimmed().isEmpty()) {
            if (ModernTooltip::tipVisible()
                && ModernTooltip::currentHost() == wgt
                && ModernTooltip::currentText() == tip) {
                return true;   // already showing exactly this tip
            }
            ModernTooltip::showTip(wgt, tip, QCursor::pos());
            return true;       // suppress the native square QTipLabel
        }
    } else if (ModernTooltip::tipVisible()) {
        // Same moments Qt hides its own tooltip — hide ours identically.
        switch (e->type()) {
            case QEvent::Leave:
            case QEvent::MouseButtonPress:
            case QEvent::Wheel:
            case QEvent::ApplicationDeactivate:
            case QEvent::Close:
                ModernTooltip::hideTip();
                break;
            case QEvent::Hide:
            case QEvent::Destroy:
                if (obj == ModernTooltip::currentHost())
                    ModernTooltip::hideTip();
                break;
            case QEvent::MouseMove:
                // Moving WITHIN the host keeps the tip (native behavior);
                // moving onto any other widget hides it.
                if (obj != ModernTooltip::currentHost())
                    ModernTooltip::hideTip();
                break;
            default:
                break;
        }
    }
    // Translucent tooltip windows — REAL rounded corners.
    // Qt creates tooltips as QTipLabel: a square native top-level window.
    // Our QSS draws a border-radius on it, but without translucency the
    // pixels outside the radius show the raw ToolTipBase fill, so every
    // corner still looked clipped/square. Flipping the window translucent
    // here (before Qt creates its native window — QEvent::Show arrives
    // first) makes everything outside the QSS radius fully transparent.
    // v1.7.17: this path now only serves the fallback cases left to the
    // native tooltip (empty toolTip() above — e.g. QMenu actions).
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

        // v1.7.20 (carried): dedicated EMBEDDING pool so BGE work is
        // never starved by (and never starves) search/extraction/OCR.
        // v1.7.21: the pool lives in EmbeddingController now — attach
        // the service to the controller and plumb the pool into
        // BgeService so ALL BGE background work (model init below +
        // batch embedding) runs on it.
        if (embeddingController_) {
            embeddingController_->attachService(bgeService_.get());
            bgeService_->setWorkerPool(embeddingController_->embeddingPool());
        }

        connect(bgeService_.get(), &BgeService::ready,
                this, &MainWindow::onBgeReady);
        // v1.7.21: BGE batch progress/completion feed the controller's
        // state machine (chip + status flow back through its signals);
        // readiness wakes the backfill directly.
        if (embeddingController_) {
            connect(bgeService_.get(), &BgeService::embeddingProgress,
                    embeddingController_.get(),
                    &EmbeddingController::noteEmbeddingProgress);
            connect(bgeService_.get(), &BgeService::embeddingFinished,
                    embeddingController_.get(),
                    &EmbeddingController::noteEmbeddingFinished);
            connect(bgeService_.get(), &BgeService::ready,
                    embeddingController_.get(),
                    &EmbeddingController::ensureBackfill);
        }

        // v1.7.11 lifetime safety: keep the future — the lambda captures
        // `this` and dereferences bgeService_, so ~MainWindow must join
        // it before members are destroyed (exit during model load was a
        // use-after-free window).
        bgeInitFuture_ = QtConcurrent::run(embeddingController_->embeddingPool(),
                                           [this, dbPath, modelPath]() {
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
    if (embeddingController_) embeddingController_->setAiEnabled(checked);
    if (hybridSearch_) hybridSearch_->setSemanticEnabled(checked);
    setAiChip(checked ? "ON" : "OFF", checked);

    if (checked && bgeService_ && bgeService_->isReady()) {
        // Queue any indexed-but-unembedded files on the background BGE
        // worker (shared with the onBgeReady path; batches chain in the
        // controller until the backlog is drained).
        if (embeddingController_) embeddingController_->ensureBackfill();
        if (!embeddingController_ || !embeddingController_->isBackfillRunning()) {
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
    if (!embeddingController_ || !embeddingController_->isBackfillRunning()) {
        setAiChip("ON", true);
    }
    statusBar()->showMessage(
        "AI search ready — " + bgeService_->getStatus(), 5000);
    // Drain the embedding backlog right away — previously this only ran
    // when the user manually toggled AI on, so files indexed before the
    // service was up stayed invisible to semantic search.
    if (embeddingController_) embeddingController_->ensureBackfill();
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

// ============================================================
// v1.7.21: the AI-embedding backfill + rebuild state machine moved to
// EmbeddingController (headless, wiring-tested in tests/tst_Wiring.cpp):
// ensureBackfill (stale invalidation + phase A/B + chaining),
// startRebuild + the purge chain, the backlog counters, and the batch
// progress/completion handling with its deadlock guard. The window
// wires BgeService's signals into the controller in
// initializeSemanticSearch() and the controller's
// chipChanged/statusMessage/rebuildBlocked back into the UI — every
// wire in that loop is visible in one place.
// ============================================================

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
            // One extra query pair per stats tick (20 s); plain
            // COUNT(*)-class aggregates.
            // v1.7.15: -1 = that sub-count is unknown (its query failed).
            // One broken sub-count no longer blanks the other chip (the
            // old single-statement version died entirely while the
            // EmbeddingChunks table was missing — both chips froze at 0).
            qint64 extracted = -1, embedded = -1;
            repo_->countExtractedAndEmbedded(extracted, embedded);
            if (extracted >= 0 && extractedInfoLbl_) {
                extractedInfoLbl_->setText(
                    QString("Extracted %1")
                        .arg(QLocale::c().toString(extracted)));
            }
            if (embedded >= 0 && embeddedInfoLbl_) {
                embeddedInfoLbl_->setText(
                    QString("Embedded %1")
                        .arg(QLocale::c().toString(embedded)));
            }
            if (extracted >= 0 && embedded >= 0) {
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
            const bool busy = extractionController_ &&
                              extractionController_->isRunning();
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
    // v1.7.21: the single-file pipeline (gate -> upsert -> extract ->
    // FTS refresh -> stale-embedding drop -> backfill wake) moved to
    // ExtractionController::extractAndIndexFile. The settings-driven
    // admissibility gate is injected there (ctor); the controller adds
    // the constant allowlist + existence checks itself.
    return extractionController_ &&
           extractionController_->extractAndIndexFile(path);
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
// ============================================================
// v1.7.24: "OCR this file" no longer runs ON the UI thread. The old
// implementation executed the helper process (and, for PDFs, the
// whole per-page render + OCR loop) synchronously between
// processEvents() pumps — seconds of effectively frozen UI for a
// multi-page scan, and re-entrancy surface on every pump. The
// acquisition now runs on a QtConcurrent worker (the same place the
// OCR pool calls WindowsOcrEngine from); the watcher continuation
// lands on the UI thread and does what a window should do: the
// index writes, the preview update, the status line.
// The interpolated UPDATE the 2026-09 audit flagged is a prepared
// statement now.
// ============================================================
void MainWindow::onOcrThisFile(const QString& path) {
    if (!repo_ || !db_ || path.isEmpty()) return;
    if (!QFileInfo::exists(path)) {
        statusBar()->showMessage("File not found: " + path, 5000);
        return;
    }
    // Single flight: a second click while one file is being read
    // would double-charge the helper and race the preview updates.
    if (ocrThisFileInFlight_.load()) {
        statusBar()->showMessage("OCR is already running on a file...",
                                 3000);
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

    const qint64 fileId = selectedFileId_;
    const QString filePath = path;

    ocrThisFileInFlight_ = true;
    statusBar()->showMessage("Running OCR...", 0);

    struct OcrOutcome {
        bool helperMissing = false;
        bool openFailed = false;
        QString openError;
        QString text;
    };

    QFuture<OcrOutcome> future = QtConcurrent::run(
        [filePath, ext, isImage, isPdf]() {
        OcrOutcome out;

        // Use the singleton — this way the available_ flag persists
        // across calls (the status bar indicator and the OCR button
        // share the same engine state). WindowsOcrEngine is designed
        // to be driven from worker threads (the OCR pool does it).
        WindowsOcrEngine& ocrEngine = WindowsOcrEngine::instance();
        if (!ocrEngine.init()) {
            out.helperMissing = true;
            return out;
        }
        // Don't check isAvailable() — the cached flag may be stale.
        // Just try OCR silently. If it fails, empty text will be shown
        // as the result, and the helper's stderr updates the flag.

        if (isImage) {
            out.text = ocrEngine.ocrFile(filePath);
            return out;
        }
#ifdef DOCUSEARCH_HAS_PDFIUM
        // For PDFs: render each page to image via PDFium, save as
        // temp PNG, then OCR each page via the helper exe.
        try {
            PdfiumDocument doc;
            if (!doc.loadFromFile(filePath) || doc.pageCount() == 0) {
                out.openFailed = true;
                out.openError = doc.lastError();
                return out;
            }

            const int dpi = 96;  // lower DPI for OCR speed
            const int pageTotal = doc.pageCount();
            const int maxPages =
                (pageTotal < 10) ? pageTotal : 10;  // max 10 pages

            for (int i = 0; i < maxPages; ++i) {
                try {
                    const QImage qimg = doc.renderPage(i, dpi);
                    if (qimg.isNull()) continue;

                    // Save page as temp PNG and OCR it.
                    // Use native separators: the OCR helper exe calls
                    // WinRT StorageFile::GetFileFromPathAsync which
                    // rejects forward slashes with the misleading
                    // "The path contains one or more invalid
                    // characters" error. (Note: ocrFile() also
                    // normalizes defensively, but doing it here keeps
                    // the tempPath we log / remove consistent with
                    // what we pass to the helper.)
                    QString tempPath = QDir::toNativeSeparators(
                        QDir::tempPath() + "/docusearch_ocr_page_" +
                        QString::number(i) + ".png");
                    qimg.save(tempPath, "PNG");

                    QString pageText = ocrEngine.ocrFile(tempPath);
                    QFile::remove(tempPath);

                    if (!pageText.isEmpty()) {
                        out.text += pageText + "\n";
                    }
                } catch (...) {
                    // Skip this page
                }
            }
        } catch (...) {
            out.openFailed = true;
            out.openError = QStringLiteral("PDF rendering failed");
        }
#endif
        return out;
    });

    auto* watcher = new QFutureWatcher<OcrOutcome>(this);
    connect(watcher, &QFutureWatcher<OcrOutcome>::finished, this,
            [this, watcher, fileId, filePath, ext]() {
        const OcrOutcome out = watcher->result();
        watcher->deleteLater();
        ocrThisFileInFlight_ = false;

        if (out.helperMissing) {
            statusBar()->showMessage("OCR helper not found.", 5000);
            QMessageBox::information(this, "OCR",
                "OCR helper (docusearch_ocr_helper.exe) not found.\n"
                "Make sure it's in the same folder as DocuSearch.exe.");
            return;
        }
        if (out.openFailed) {
            statusBar()->showMessage(
                out.openError.isEmpty()
                    ? QStringLiteral("OCR: failed to open PDF.")
                    : QStringLiteral("OCR: %1.").arg(out.openError),
                5000);
            return;
        }
        const QString ocrText = out.text;

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

        // Save OCR text to database (UI thread, main connection).
        try {
            sqlite3* raw = db_->raw();
            if (raw) {
                QByteArray textBytes = ocrText.toUtf8();
                qint64 now = QDateTime::currentSecsSinceEpoch();

                sqlite3_stmt* upd = nullptr;
                sqlite3_prepare_v2(raw,
                    "INSERT INTO DocumentText (file_id, extracted_text, "
                    "  text_source, char_count, updated_at) "
                    "VALUES (?1, ?2, 'ocr', ?3, ?4) "
                    "ON CONFLICT(file_id) DO UPDATE SET "
                    "  extracted_text=excluded.extracted_text, "
                    "  text_source='ocr', "
                    "  char_count=excluded.char_count, "
                    "  updated_at=excluded.updated_at;",
                    -1, &upd, nullptr);
                if (upd) {
                    sqlite3_bind_int64(upd, 1, fileId);
                    sqlite3_bind_text(upd, 2, textBytes.constData(), -1,
                                      SQLITE_TRANSIENT);
                    sqlite3_bind_int64(upd, 3, ocrText.size());
                    sqlite3_bind_int64(upd, 4, now);
                    sqlite3_step(upd);
                    sqlite3_finalize(upd);
                }

                // v1.7.24: prepared (was QString::arg interpolation).
                sqlite3_stmt* st = nullptr;
                sqlite3_prepare_v2(raw,
                    "UPDATE Files SET indexing_status='content_done', "
                    "ocr_status='done' WHERE id=?1;",
                    -1, &st, nullptr);
                if (st) {
                    sqlite3_bind_int64(st, 1, fileId);
                    sqlite3_step(st);
                    sqlite3_finalize(st);
                }

                sqlite3_stmt* del = nullptr;
                sqlite3_prepare_v2(raw,
                    "DELETE FROM SearchIndex WHERE file_id=?1;",
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
                    "INSERT INTO SearchIndex (filename, content, path, "
                    "  extension, file_id) VALUES (?1, ?2, ?3, ?4, ?5);",
                    -1, &ins, nullptr);
                if (ins) {
                    sqlite3_bind_text(ins, 1, fn.constData(), -1,
                                      SQLITE_TRANSIENT);
                    sqlite3_bind_text(ins, 2, textBytes.constData(), -1,
                                      SQLITE_TRANSIENT);
                    sqlite3_bind_text(ins, 3, pth.constData(), -1,
                                      SQLITE_TRANSIENT);
                    sqlite3_bind_text(ins, 4, ext2.constData(), -1,
                                      SQLITE_TRANSIENT);
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
        // OCR results are text — surface them even when the selected
        // file is not a PDF (the extracted-text pane starts hidden for
        // those types).
        previewPane_->setVisible(true);
        updateIndexStats();
        statusBar()->showMessage(
            QString("OCR complete: %1 characters recognized.")
                .arg(ocrText.size()), 5000);

        // Refresh the OCR status indicator — if OCR just succeeded, the
        // Windows.Media.Ocr language packs are definitely installed. This
        // fixes the case where the indicator showed "Setup Required" because
        // the user installed language packs after launching DocuSearch.
        updateOcrStatusIndicator();
    });
    watcher->setFuture(future);
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
                // v1.7.23 B2 (audit): the old flow removed the live
                // database BEFORE expanding the zip, so a failed expand
                // left db_->open() to create a blank, schema-less file.
                // BackupManager::restore now expands into a temp dir,
                // validates the SQLite header and only then swaps - on
                // failure the original file is untouched. Pipelines are
                // parked the same way removeAndRebuildDatabase() parks
                // them, and Schema::initialize runs after reopen so a
                // backup taken by an older build migrates forward.
                statusBar()->showMessage("Restoring database...", 0);
                if (extractionController_) {
                    extractionController_->setDbResetting(true);
                    extractionController_->invalidateSession();
                }
                if (embeddingController_) embeddingController_->stopAll();
                dbResetting_ = true;
                db_->close();

                BackupManager bm;
                QString restoreErr;
                const bool ok = bm.restore(zipPath,
                                           Config::instance().dbPath(),
                                           &restoreErr);
                QString openErr;
                if (db_->open(Config::instance().dbPath(), &openErr)) {
                    Schema::initialize(*db_);
                } else if (!openErr.isEmpty()) {
                    DS_ERROR("Database",
                             "Reopen after restore failed: " + openErr);
                }
                dbResetting_ = false;
                if (extractionController_) {
                    extractionController_->setDatabase(db_.get());
                    extractionController_->setDbResetting(false);
                }
                if (embeddingController_)
                    embeddingController_->setDatabase(db_.get());

                if (ok) {
                    statusBar()->showMessage(
                        "Database restored. Please restart DocuSearch.",
                        8000);
                    updateIndexStats();
                    refreshSavedSearches();
                } else {
                    statusBar()->showMessage(
                        QStringLiteral("Restore failed%1. The previous "
                                       "database is unchanged.")
                            .arg(restoreErr.isEmpty()
                                     ? QStringLiteral(".")
                                     : QStringLiteral(": ") + restoreErr),
                        10000);
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
                const qint64 pending =
                    (embeddingController_
                         ? embeddingController_->countMissingEmbeddings() : 0)
                    + (embeddingController_
                         ? embeddingController_->countMissingChunkDocs() : 0);
                if (pending == 0) {
                    statusBar()->showMessage(
                        "All documents already have embeddings.", 5000);
                    return;
                }
                if (embeddingController_) embeddingController_->ensureBackfill();
                statusBar()->showMessage(
                    QString("AI indexing started — %1 document%2 in queue; "
                            "progress shows in the status bar.")
                        .arg(pending).arg(pending == 1 ? "" : "s"), 6000);
            });

        // "Rebuild All AI Embeddings" — purge every stored embedding in
        // small batches, then re-run the shared two-phase backfill so
        // the whole library is recomputed from full document text.
        // v1.7.21: the purge chain lives in EmbeddingController; a
        // blocked rebuild (AI not ready) comes back as a signal and the
        // ctor's forwarder shows the dialog.
        QObject::connect(&dlg, &SettingsDialog::rebuildEmbeddingsRequested,
            this, [this]() {
                if (embeddingController_) embeddingController_->startRebuild();
            });

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
            if (!settings_.indexedDrives.isEmpty() && liveIndexingEnabled_)
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
        if (watcher_) watcher_->removeWatch(drive);
        purgeFolderFromIndex(drive);
    }

    // ---- ADDED folders: watch live, scan now, auto-extract ----
    // v1.7.4 fix: a newly added folder was scanned but NEVER
    // watched (addWatches ran once at startup only), so live
    // changes in it went unnoticed until the next hourly scan.
    // v1.7.24: the walks queue on the ScanPipelineController worker
    // (FIFO) and each completion wakes extraction in
    // onFolderScanFinished — no UI-thread scan, no processEvents.
    for (const QString& drive : settings_.indexedDrives) {
        if (oldFolded.contains(FileUtils::toNative(drive).toLower()))
            continue;  // unchanged
        if (settings_.monitorFileChanges && liveIndexingEnabled_ &&
            watcher_ && !watcher_->isWatched(drive))
            watcher_->addWatch(drive);
        statusBar()->showMessage("Scanning " + drive + " ...");
        scanPipeline_->startFolderScan(drive, currentScanParams());
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
// ============================================================
// v1.7.24: DUPLICATES — the five passes (candidate pull, same-file
// collapse, size pre-grouping, fingerprinting with write-back,
// survivors-only grouping + re-verification + ordering) now run on
// DuplicateScanController's worker thread with its own sqlite
// connection. The window keeps what a window should own: the tier
// gate, the single-flight guard, the progress dialog (which now
// POLLS the worker's atomic hash counter instead of being kept
// alive by processEvents pumps), and rendering the result.
// pathIdentityKey() and moveFileKeepingName() live in the
// controller as statics — one definition, testable headless.
// ============================================================
void MainWindow::onDetectDuplicates() {
    if (!repo_ || !db_) return;
    // v1.7.23 (audit C2): the tier gate is real now. Every tier ships
    // with detection enabled; this keeps the config honest.
    if (!duplicateDetectionEnabled_) {
        statusBar()->showMessage(
            "Duplicate detection is disabled on this system tier.", 4000);
        return;
    }
    // Single flight: two concurrent scans would fight over the same
    // results pane and hash the same files twice (the old code guarded
    // a static bool across event-loop pumps; the controller's atomic
    // running flag is the honest version of the same guard).
    if (!duplicateScan_ || duplicateScan_->isRunning()) return;

    // Nothing from a PREVIOUS run may survive on screen.
    dupResults_.clear();
    dupKeys_.clear();
    resultsPane_->setResults({});
    resultsPane_->setAction(QString());
    resultsPane_->setAiSummary(QString());

    // Range must never be 0..0 — that is QProgressDialog's "busy"
    // mode, which shows a spinner forever for a job with nothing
    // to do. The real maximum arrives with the first poll.
    dupProgress_ = new QProgressDialog(
        QStringLiteral("Comparing file contents..."),
        QStringLiteral("Cancel"), 0, 1, this);
    dupProgress_->setWindowTitle(QStringLiteral("Duplicates"));
    dupProgress_->setWindowModality(Qt::WindowModal);
    dupProgress_->setMinimumDuration(400);   // no flash for instant jobs
    connect(dupProgress_, &QProgressDialog::canceled, this, [this]() {
        // Cancel stops BEFORE the next file: nothing is ever
        // half-compared, and the finished result reports the
        // partial run honestly.
        if (duplicateScan_) duplicateScan_->cancel();
    });
    dupProgressTimer_ = new QTimer(dupProgress_);
    connect(dupProgressTimer_, &QTimer::timeout, dupProgress_, [this]() {
        if (!duplicateScan_ || !dupProgress_) return;
        const int done = duplicateScan_->hashProgressDone();
        const int total = qMax(1, duplicateScan_->hashProgressTotal());
        dupProgress_->setMaximum(total);
        dupProgress_->setValue(qMin(done, total));
        if (done > 0) {
            dupProgress_->setLabelText(
                QStringLiteral("Comparing file contents... %1 / %2")
                    .arg(done).arg(total));
        }
    });
    dupProgressTimer_->start(250);

    duplicateScan_->start(Config::instance().dbPath());
}

void MainWindow::onDuplicateScanFinished(
        const DocuSearch::DuplicateScanResult& r) {
    if (dupProgressTimer_) {
        dupProgressTimer_->stop();
        dupProgressTimer_ = nullptr;
    }
    if (dupProgress_) {
        dupProgress_->setValue(dupProgress_->maximum());  // closes it
        dupProgress_->deleteLater();
        dupProgress_ = nullptr;
    }

    try {
        // v1.7.24: ghost-row purge happens HERE, on the UI thread,
        // through FileRepository::deleteByPath — the scan only
        // REPORTED the rows (purging mid-walk on a worker connection
        // was both racy against the main connection and pointless:
        // the candidates were excluded either way).
        if (!r.stalePaths.isEmpty())
            purgeStaleRows(r.stalePaths, QStringLiteral("duplicates"));

        // Honest helper for the summary: what the check covers.
        QString docTypeHelp;
        for (const QString& t : Constants::kIndexableExtensions)
            docTypeHelp += QStringLiteral(" .%1").arg(t.toLower());

        if (r.hits.isEmpty()) {
            // Empty the results list too, so the listing from a
            // PREVIOUS duplicate check can't stay on screen behind
            // the message box. (Cleared above — kept explicit here.)
            resultsPane_->setResults({});
            dupResults_.clear();
            dupKeys_.clear();
            resultsPane_->setAction(QString());
            resultsPane_->setAiSummary(QString());

            QString detail;
            if (r.cancelled) {
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
                    .arg(qint64(r.candidateCount))
                    .arg(r.candidateCount == 1 ? " was" : "s were")
                    .arg(r.hashedNow > 0
                        ? QString(" (%1 fingerprint%2 computed now)")
                            .arg(r.hashedNow)
                            .arg(r.hashedNow == 1 ? "" : "s")
                        : QString())
                    .arg(docTypeHelp);
            }
            QStringList notes;
            if (r.skippedMissing > 0)
                notes << QString("%1 index entr%2 pointed at files that "
                                 "no longer exist")
                             .arg(r.skippedMissing)
                             .arg(r.skippedMissing == 1 ? "y" : "ies");
            if (r.staleRows > 0)
                notes << QString("%1 duplicate index row%2 for the same "
                                 "physical file %3 collapsed")
                             .arg(r.staleRows)
                             .arg(r.staleRows == 1 ? "" : "s")
                             .arg(r.staleRows == 1 ? "was" : "were");
            if (r.droppedSingletons > 0)
                notes << QString("%1 file%2 lost its duplicate partner "
                                 "during the check")
                             .arg(r.droppedSingletons)
                             .arg(r.droppedSingletons == 1 ? "" : "s");
            if (r.unreadable > 0)
                notes << QString("%1 file%2 could not be read")
                             .arg(r.unreadable)
                             .arg(r.unreadable == 1 ? "" : "s");

            QMessageBox::information(this, "Duplicates",
                QString("No duplicate files found.\n\n%1%2")
                    .arg(detail)
                    .arg(notes.isEmpty()
                        ? QString()
                        : QStringLiteral("\n\n") + notes.join(", ")
                              + QStringLiteral(".")));
            statusBar()->showMessage(
                r.cancelled ? "Duplicate check cancelled."
                            : "No duplicate files found.", 5000);
            return;
        }

        resultsPane_->setResults(r.hits);
        // arm the cleanup action + remember what the pane is showing
        // (the delete slot re-validates against disk anyway).
        dupResults_ = r.hits;
        dupKeys_    = r.groupKeys;
        resultsPane_->setAction(
            QStringLiteral("Delete duplicates..."));
        // A cancelled check must not pass as complete: keep the note on
        // screen as long as the results are (not an 8 s status toast).
        resultsPane_->setAiSummary(r.cancelled
            ? QStringLiteral("Check cancelled early - the list is "
                             "partial; some files were never compared.")
            : QString());
        statusBar()->showMessage(
            QString("Found %1 duplicate group%2 (%3 files)%4%5%6")
                .arg(r.groupCount)
                .arg(r.groupCount == 1 ? "" : "s")
                .arg(r.hits.size())
                .arg(r.hashedNow > 0
                    ? QString("; %1 fingerprint%2 computed now")
                        .arg(r.hashedNow).arg(r.hashedNow == 1 ? "" : "s")
                    : QString())
                .arg(r.staleRows > 0
                    ? QString("; %1 duplicate index row%2 collapsed")
                        .arg(r.staleRows).arg(r.staleRows == 1 ? "" : "s")
                    : QString())
                .arg(r.cancelled ? QStringLiteral("; check cancelled early")
                                 : QString()),
            8000);
        if (r.hashedNow > 0) updateIndexStats();
    } catch (const std::exception& e) {
        DS_ERROR("Duplicates", QString("Failed: %1").arg(e.what()));
        statusBar()->showMessage("Duplicate detection failed.", 3000);
    } catch (...) {
        statusBar()->showMessage("Duplicate detection failed.", 3000);
    }
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
// v1.7.24: the newest-kept selection lives in
// DuplicateScanController::selectDoomedCopies, the collision-safe
// move in its moveFileKeepingName — both headless-testable.
void MainWindow::onDeleteDuplicateCopies() {
    if (dupResults_.isEmpty() || dupKeys_.size() != dupResults_.size())
        return;

    qint64 reclaimBytes = 0;
    int groupsActed = 0;
    const QList<int> doomed = DuplicateScanController::selectDoomedCopies(
        dupResults_, dupKeys_, &reclaimBytes, &groupsActed);
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

    // v1.7.15: visible progress while the copies are moved. Recycle-Bin
    // moves and cross-volume folder copies can take seconds PER FILE,
    // and a silent stretch reads as a hang. Cancelling stops BEFORE the
    // next file: already-moved copies stay moved, the rest keep their
    // originals — nothing is ever half-deleted.
    const int totalMoves = static_cast<int>(doomed.size());
    QProgressDialog prog(QStringLiteral("Preparing..."),
                         QStringLiteral("Cancel"), 0, totalMoves, this);
    prog.setWindowTitle(QStringLiteral("Delete duplicate copies"));
    prog.setWindowModality(Qt::WindowModal);
    prog.setMinimumDuration(400);  // show only if this actually takes time

    for (int pos = 0; pos < totalMoves; ++pos) {
        if (prog.wasCanceled()) break;
        const QString p = dupResults_[doomed[pos]].path;
        prog.setLabelText(
            QStringLiteral("Moving %1 of %2:\n%3")
                .arg(pos + 1).arg(totalMoves)
                .arg(QDir::toNativeSeparators(p)));
        prog.setValue(pos);
        QApplication::processEvents();  // paint the label before the blocking move

        // Re-check right before moving: the dialog pumped the event
        // loop, the user may have acted in the meantime.
        if (!QFileInfo::exists(p)) continue;
        const bool ok = toRecycleBin
            ? QFile::moveToTrash(p)
            : DuplicateScanController::moveFileKeepingName(p, destDir);
        if (ok) {
            ++deleted;
            movedPaths.append(p);
        } else {
            ++failed;
        }
    }
    const bool stoppedEarly = prog.wasCanceled();
    prog.setValue(totalMoves);  // ensure the dialog closes
    QApplication::processEvents();

    if (!movedPaths.isEmpty()) {
        purgeStaleRows(movedPaths, QStringLiteral("duplicates delete"));
        updateIndexStats();
    }

    dupResults_.clear();
    dupKeys_.clear();
    resultsPane_->setAction(QString());

    statusBar()->showMessage(
        QString("%1 of %2 cop%3 %4%5%6")
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
                : QString())
            .arg(stoppedEarly
                ? QStringLiteral("; stopped early - remaining copies were "
                                 "left in place")
                : QString()),
        8000);

    // Show the truth: re-run the check. Hashes were just written back,
    // so the reuse path makes this fast.
    onDetectDuplicates();
}

} // namespace DocuSearch
