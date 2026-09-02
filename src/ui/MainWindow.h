#pragma once

// ============================================================
// MainWindow.h - Top-level window with custom title bar
// ============================================================
//
// Layout (top to bottom):
//   ┌─────────────────────────────────────────────────────────┐
//   │ [Logo] DocuSearch 1.0.0 • Offline Document Search  ☀🌙─☐✕ │ title bar
//   ├──────┬──────────────────────────────────┬────────────────┤
//   │ Side │ [search bar with all buttons]    │ Metadata       │
//   │ bar  ├──────────┬───────────────────────┤                │
//   │      │ Results  │ Document viewer       │ Tags           │
//   │ nav  │ (340px)  │ (flex)                │                │
//   │      │          ├───────────────────────┤ Notes          │
//   │      │          │ Extracted text panel  │                │
//   │      │          │ (tabs + content)      │                │
//   │ st.  │          │                       │                │
//   ├──────┴──────────┴───────────────────────┴────────────────┤
//   │ ● Ready  Indexed: 2,451  Size: 3.42 GB  Last: ...  [📁] │ status bar
//   └─────────────────────────────────────────────────────────┘
// ============================================================

#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QFuture>
#include <memory>
#include <atomic>

#include "../core/Types.h"

class QSplitter;
class QMenu;
class QAction;
class QStatusBar;
class QTimer;
class QToolBar;
class QListWidget;
class QListWidgetItem;
class QLabel;
class QPushButton;
class QProgressBar;

namespace DocuSearch {

class Database;
class FileRepository;
class SearchEngine;
class OcrWorkerPool;
class FileWatcher;

class SearchBar;
class ResultsPane;
class PreviewPane;
class FilePreviewPane;
class MetadataPane;
class TagsNotesPane;
class IndexingProgressWidget;
class SwitchControl;

class BgeService;
class HybridSearchEngine;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* e) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
    bool eventFilter(QObject* obj, QEvent* e) override;
    void changeEvent(QEvent* e) override;
    void showEvent(QShowEvent* e) override;

private slots:
    void onSearch(const QString& query);
    void onFileSelected(qint64 fileId, const QString& path);
    void onFileActivated(qint64 fileId, const QString& path);
    void onOpenOriginal(const QString& path);
    void onOpenSettings();
    void onOcrThisFile(const QString& path);
    void onToggleTheme();
    void onSemanticToggled(bool checked);
    void onBgeReady();
    void onBgeFailed();
    void onBgeEmbeddingProgress(int current, int total);
    void onBgeEmbeddingFinished(int success, int fail);
    void onIndexingProgress(const DocuSearch::IndexingProgress& p);
    void onPhaseChanged(const QString& phase);
    void onIndexingStarted();
    void onIndexingFinished();
    void onFileAdded(const QString& path);
    void onFileModified(const QString& path);
    void onFileRenamed(const QString& oldPath, const QString& newPath);
    void onFileDeleted(const QString& path);

    // v1.7.5: shared single-file pipeline used by the watcher handlers.
    // Upserts the Files row, re-extracts the content, refreshes the FTS
    // row, clears now-stale AI embeddings (rebuilt by the backfill) and
    // wakes the embedding backfill. Returns false when the file is not
    // an indexable document (unsupported type, excluded folder, gone).
    bool extractAndIndexFile(const QString& path);
    void onSavedSearchSelected(const QString& name);
    void onTagAdded(qint64 fileId, const QString& tag);
    void onTagRemoved(qint64 fileId, const QString& tag);
    void onNoteChanged(qint64 fileId, const QString& note);
    void onLiveSearchTick();
    void onAbout();
    void onExportCsv();
    void onDetectDuplicates();
    void onAddFolder();
    void onExtract();
    void onRefresh();
    void onFilters();
    void onSidebarClicked(int row);
    void onOpenLocation();
    void autoScanIndexedFolders();
    void onOcrTaskCompleted(qint64 fileId, const QString& text, bool ok);
    void runStartupIntegrityPass();
    bool ocrWorkOutstanding() const;

    // v1.7.4: AUTO-wake of the extraction pipeline (startup timer, scan
    // completion, new folder). Unlike the Extract button's toggle
    // semantics, this NEVER cancels a run already in flight and yields
    // (with retries) while a scan is still walking the folders.
    void requestAutoExtract();
    // v1.7.4: remove every index row under `folder` (Settings -> Indexed
    // Folders removal). Cascades to SearchIndex, BgeEmbeddings and
    // EmbeddingChunks so no ghost of the removed folder can resurface
    // in keyword or AI search.
    void purgeFolderFromIndex(const QString& folder);
    // v1.7.4: self-heal — delete index rows whose file no longer exists
    // on a REACHABLE drive (an unplugged drive must never be purged;
    // those rows are only hidden from display instead). Returns the
    // number of rows actually removed.
    int purgeStaleRows(const QStringList& paths, const QString& context);
    // v1.7.7: one-time startup cleanup — delete every Files row (plus
    // SearchIndex/BgeEmbeddings/EmbeddingChunks) whose extension is not
    // in Constants::kIndexableExtensions. Older versions indexed every
    // file type they walked (md notes, installers, archives...); this
    // makes "documents and images only" true immediately on first launch
    // of 1.7.7. Returns the number of rows removed.
    int purgeNonIndexableRows();

    // v1.7.10: Settings → "Remove Database (Reset)". Cancels running
    // extraction/OCR, closes the database, deletes docusearch.db
    // (+ -wal/-shm), reopens a fresh one, re-initializes the schema and
    // kicks the auto-scan so the index rebuilds from the configured
    // folders without a restart.
    void removeAndRebuildDatabase();

private:
    // UI builders
    void buildTitleBar();
    void buildCentral();
    void buildStatusBar();
    void applyTheme();
    // v1.7.11: ONE apply path for settings. Both the dialog's Apply button
    // and its OK button route here, so every setting takes effect the same
    // way regardless of which button the user pressed. Diffs indexedDrives
    // against the CURRENT settings_ (watch/unwatch/purge/scan), pushes CPU
    // throttle settings into the OCR pool, honors monitorFileChanges, and
    // persists + re-themes.
    void applyNewSettings(const AppSettings& s);
    void loadSettings();
    void saveSettings();
    void refreshSavedSearches();
    void openFile(const QString& path);

    // Fast metadata-only scan of a folder.
    void scanFolderFast(const QString& folder);

    // Refresh the preview pane with the currently-selected file's
    // extracted text.
    void refreshPreviewForSelectedFile();

    // Re-render all Lucide icons throughout the UI (call after theme toggle).
    void refreshAllIcons();

    // One-shot: restore WS_THICKFRAME so Windows honors the WM_NCHITTEST
    // resize borders on our frameless window. See MainWindow.cpp.
    void enableNativeResize();
    bool nativeResizeApplied_ = false;

    // Embedding backfill diagnostics + shared count helpers.
    qint64 countMissingEmbeddings();   // docs with text but no embedding
    qint64 countMissingChunkDocs();    // embedded docs without chunk rows
    bool   aiBackfillChunkMode_ = false;
    int    aiBackfillDeadlock_  = 0;   // consecutive zero-success batches

public:
    Q_INVOKABLE void updateIndexStats();
    // Refresh the OCR availability indicator on the status bar.
    void updateOcrStatusIndicator();
    // Initialize the BGE semantic search subsystem (optional, async).
    void initializeSemanticSearch();
    // Returns a human-readable extraction status string for the status bar.
    QString getExtractionStatusString();

    // Owned subsystems
    std::unique_ptr<Database>       db_;
    std::unique_ptr<FileRepository> repo_;
    std::unique_ptr<SearchEngine>   search_;
    std::unique_ptr<OcrWorkerPool>  ocrPool_;
    std::unique_ptr<FileWatcher>    watcher_;
    // Phase 9: Debounce file watcher events (merge add+modify within 500ms).
    QHash<QString, qint64> fileEventDebounce_;
    QTimer* fileEventDebounceTimer_ = nullptr;

    // ---- UI widgets ----
    // Title bar
    QWidget*        titleBar_             = nullptr;
    QLabel*         appLogoLbl_           = nullptr;
    QLabel*         titleBarText_         = nullptr;
    QLabel*         titleBarSubtitle_     = nullptr;
    // No theme toggle button — light mode only
    QPushButton*    titleMinBtn_          = nullptr;
    QPushButton*    titleMaxBtn_          = nullptr;
    QPushButton*    titleCloseBtn_        = nullptr;

    // Sidebar
    QWidget*        sidebar_              = nullptr;
    QListWidget*    sidebarList_          = nullptr;
    QLabel*         indexedHeaderLbl_     = nullptr;
    QLabel*         indexedInfoLbl_       = nullptr;
    QProgressBar*   indexedBar_           = nullptr;

    // Center panel
    SearchBar*      searchBar_            = nullptr;
    QSplitter*      mainSplitter_         = nullptr;  // 3-way: results | viewer | metadata
    ResultsPane*    resultsPane_          = nullptr;
    PreviewPane*    previewPane_          = nullptr;
    // New top-pane native file preview (PDF/image/text/office).
    // Sits ABOVE previewPane_ in the same column.
    FilePreviewPane* filePreviewPane_     = nullptr;
    // Semantic search toggle button (in search bar).
    QPushButton*    semanticToggleBtn_    = nullptr;  // legacy; unused after switch port
    QWidget*        aiControlWidget_     = nullptr;  // container: [sparkles-ico] AI [switch] [state label]
    QLabel*         aiIconLbl_           = nullptr;
    SwitchControl*  aiSwitch_            = nullptr;
    QLabel*         aiStateLbl_          = nullptr;
    QPushButton*    themeToggleBtn_       = nullptr;

    // Semantic search subsystem (BGE + hybrid).
    std::unique_ptr<BgeService>        bgeService_;
    // v1.7.11: future of the background BGE initialize() (captures
    // `this`); joined in ~MainWindow before bgeService_ is destroyed.
    QFuture<void>                      bgeInitFuture_;
    std::unique_ptr<HybridSearchEngine> hybridSearch_;
    bool            semanticEnabled_     = false;
    bool            aiBackfillRunning_   = false;  // batch embed in flight
    bool            embeddingRebuildPurging_ = false;  // rebuild purge chain in flight
    int             embeddingRebuildRetries_ = 0;      // consecutive purge SQL failures

    // Scan SearchIndex for files with no BgeEmbeddings row and queue a
    // batch (capped) on the background BGE worker. Called when the BGE
    // service becomes ready AND whenever the user switches AI on, so the
    // embedding backlog drains without any manual user action. Follow-up
    // batches are chained from onBgeEmbeddingFinished until drained.
    void ensureEmbeddingsBackfill();

    // One-click "Rebuild All AI Embeddings" (Settings -> AI Search):
    // batch-deletes every BgeEmbeddings/EmbeddingChunks row, then hands
    // over to ensureEmbeddingsBackfill() so the whole library is
    // re-embedded from FULL document text. Embeddings built before the
    // v1.6.6 tokenizer fix were computed from text truncated at 128
    // tokens; this action recomputes them at exact length (up to 512).
    // The purge runs in small chained batches so the UI never freezes.
    void startEmbeddingRebuild();
    void purgeEmbeddingsTick();

    // Persistent status chip text for the AI control (state + counts).
    void setAiChip(const QString& text, bool active);
    void updateTitleBarState();

    // Right panel
    QSplitter*      rightSplitter_        = nullptr;  // metadata | tags/notes (vertical)
    MetadataPane*   metadataPane_         = nullptr;
    TagsNotesPane*  tagsNotesPane_        = nullptr;

    // Status bar
    QLabel*         statusDotLbl_         = nullptr;
    QLabel*         statusReadyLbl_       = nullptr;
    QLabel*         statusIndexedLbl_     = nullptr;
    QLabel*         statusSizeLbl_        = nullptr;
    QLabel*         statusLastLbl_        = nullptr;
    QPushButton*    openLocationBtn_      = nullptr;
    QProgressBar*   extractionProgressBar_ = nullptr;
    // OCR availability indicator (right side of status bar).
    QWidget*        ocrStatusWidget_      = nullptr;
    QLabel*         ocrDotLbl_            = nullptr;
    QLabel*         ocrStatusLbl_         = nullptr;

    // Hidden (kept for stats plumbing)
    IndexingProgressWidget* indexingWidget_ = nullptr;

    // Timers
    QTimer*         liveSearchTimer_      = nullptr;
    QTimer*         autoScanTimer_        = nullptr;
    QString         pendingQuery_;

    AppSettings     settings_;
    bool            darkMode_             = true;
    // Pastel theme cycling: 0=Lavender, 1=Mint, 2=Peach, 3=Midnight (dark)
    int             pastelTheme_          = 0;
    bool            contentExtractionRunning_ = false;
    std::atomic<bool> extractCancelFlag_{false};
    // v1.7.9: OCR pool session accounting — how many OCR tasks the
    // current extraction session enqueued / how many results arrived.
    int             ocrExpected_          = 0;
    int             ocrReceived_          = 0;
    // v1.7.10: first-run extract-all. True until the first full drain
    // finishes; raises the per-session cap (200 instead of 30) and the
    // re-arm delay (3 s instead of 60 s) so a new index extracts itself.
    bool            extractAllMode_       = false;
    // v1.7.10: true while removeAndRebuildDatabase() has detached the
    // old database — late OCR results / extraction-timer ticks must not
    // write into the freshly created (empty) database.
    bool            dbResetting_          = false;

    bool            autoScanRunning_      = false;
    qint64          autoScanStartedMs_    = 0;    // v1.7.3: watchdog clock
    int             autoExtractRetryLeft_ = 0;    // v1.7.4: startup auto-extract retries while a scan is busy
    qint64          lastWatcherRescanMs_  = 0;    // v1.7.4: throttle for overflow-triggered rescans
    bool            maximized_            = false;
    bool            ocrBtnEnabled_        = true;  // false while OCR is running
    qint64          selectedFileId_       = 0;
    QString         selectedPath_;
};

} // namespace DocuSearch
