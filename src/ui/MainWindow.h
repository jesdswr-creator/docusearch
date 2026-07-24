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
class Indexer;
class OcrWorkerPool;
class ExtractionWorker;
class FileWatcher;

class SearchBar;
class ResultsPane;
class PreviewPane;
class FilePreviewPane;
class MetadataPane;
class TagsNotesPane;
class IndexingProgressWidget;

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
    void onBgeEmbeddingProgress(int current, int total);
    void onBgeEmbeddingFinished(int success, int fail);
    void onStartIndexing();
    void onStopIndexing();
    void onPauseIndexing();
    void onResumeIndexing();
    void onIndexingProgress(const DocuSearch::IndexingProgress& p);
    void onPhaseChanged(const QString& phase);
    void onIndexingStarted();
    void onIndexingFinished();
    void onFileAdded(const QString& path);
    void onFileModified(const QString& path);
    void onFileRenamed(const QString& oldPath, const QString& newPath);
    void onFileDeleted(const QString& path);
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
    // Extraction worker slots (called via queued connections from the worker thread).
    void onExtractionFileDone(const ExtractionResult& result);
    void onExtractionProgress(int done, int total);
    void onExtractionFinished(int succeeded, int failed, int total);
    void onRefresh();
    void onFilters();
    void onSidebarClicked(int row);
    void onOpenLocation();
    void autoScanIndexedFolders();

private:
    // UI builders
    void buildTitleBar();
    void buildCentral();
    void buildStatusBar();
    void applyTheme();
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
    std::unique_ptr<Indexer>        indexer_;
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
    QPushButton*    semanticToggleBtn_    = nullptr;
    QPushButton*    themeToggleBtn_       = nullptr;

    // Semantic search subsystem (BGE + hybrid).
    std::unique_ptr<BgeService>        bgeService_;
    std::unique_ptr<HybridSearchEngine> hybridSearch_;
    bool            semanticEnabled_     = false;

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
    bool            contentExtractionRunning_ = false;
    std::atomic<bool> extractCancelFlag_{false};

    // ── Extraction worker (off-main-thread) ──────────────
    // Prevents UI freeze during PDF/DOCX/XLSX text extraction.
    // Worker lives on a dedicated QThread; signals deliver progress
    // back to the main thread (queued connection).
    QThread*        extractionThread_     = nullptr;
    ExtractionWorker* extractionWorker_   = nullptr;
    bool            autoScanRunning_      = false;
    bool            maximized_            = false;
    bool            ocrBtnEnabled_        = true;  // false while OCR is running
    qint64          selectedFileId_       = 0;
    QString         selectedPath_;
};

} // namespace DocuSearch
