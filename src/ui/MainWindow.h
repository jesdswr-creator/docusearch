#pragma once

// ============================================================
// MainWindow.h - Top-level window with custom title bar
// ============================================================

#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QFuture>
#include <QFutureWatcher>
#include <QMutex>
#include <vector>
#include <memory>
#include <atomic>

#include "../core/Types.h"
#include "../search/HybridSearchEngine.h"
#include "SystemHealthDashboard.h"

class QSplitter;
class QMenu;
class QAction;
class QThreadPool;
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
struct ExtractionResult;
class FileWatcher;
class MemoryMonitor;
class GracefulDegradation;

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
class ExtractionController;
class EmbeddingController;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(std::unique_ptr<Database> preopened,
                        QWidget* parent = nullptr);
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
    void onIndexingProgress(const DocuSearch::IndexingProgress& p);
    void onPhaseChanged(const QString& phase);
    void onIndexingStarted();
    void onIndexingFinished();
    void onFileAdded(const QString& path);
    void onFileModified(const QString& path);
    void onFileRenamed(const QString& oldPath, const QString& newPath);
    void onFileDeleted(const QString& path);
    
    // PHASE 1: Memory pressure handlers
    void onMemoryPressureWarning();
    void onMemoryPressureCritical();
    void onMemoryPressureRecovered();

    bool extractAndIndexFile(const QString& path);
    void onSavedSearchSelected(const QString& name);
    void onTagAdded(qint64 fileId, const QString& tag);
    void onTagRemoved(qint64 fileId, const QString& tag);
    void onNoteChanged(qint64 fileId, const QString& note);
    void onLiveSearchTick();
    void onAbout();
    void onExportCsv();
    void onDetectDuplicates();
    void onDeleteDuplicateCopies();
    void onAddFolder();
    void onExtract();
    void onRefresh();
    void onFilters();
    void onSidebarClicked(int row);
    void onOpenLocation();
    void autoScanIndexedFolders();
    void maybeRunAutomaticBackup();
    void onOcrTaskCompleted(qint64 fileId, const QString& text, bool ok);
    void runStartupIntegrityPass();
    bool ocrWorkOutstanding() const;

    void requestAutoExtract();
    void purgeFolderFromIndex(const QString& folder);
    int purgeStaleRows(const QStringList& paths, const QString& context);
    int purgeNonIndexableRows();
    void removeAndRebuildDatabase();
    void showWelcomeDialog();
    void showStatsAndHealth();
    HealthMetrics collectHealthMetrics() const;

private:
    void buildTitleBar();
    void buildCentral();
    void buildStatusBar();
    void applyTheme();
    void applyNewSettings(const AppSettings& s);
    void loadSettings();
    void saveSettings();
    void refreshSavedSearches();
    void openFile(const QString& path);
    void applySemanticResults(const QString& query,
                              const QList<SearchHit>& keywordHits,
                              std::vector<HybridResult> results,
                              qint64 totalMs);
    void scanFolderFast(const QString& folder);
    void refreshPreviewForSelectedFile();
    void refreshAllIcons();
    void enableNativeResize();
    bool nativeResizeApplied_ = false;

public:
    Q_INVOKABLE void updateIndexStats();
    void updateOcrStatusIndicator();
    void initializeSemanticSearch();
    QString getExtractionStatusString();

    std::unique_ptr<Database>       db_;
    std::unique_ptr<FileRepository> repo_;
    std::unique_ptr<SearchEngine>   search_;
    std::unique_ptr<OcrWorkerPool>  ocrPool_;
    std::unique_ptr<FileWatcher>    watcher_;
    std::unique_ptr<ExtractionController> extractionController_;
    std::unique_ptr<EmbeddingController>  embeddingController_;
    std::unique_ptr<MemoryMonitor>  memoryMonitor_;
    std::unique_ptr<GracefulDegradation> degradation_;
    qint64 lastSearchLatencyMs_ = 0;
    
    QHash<QString, qint64> fileEventDebounce_;
    QTimer* fileEventDebounceTimer_ = nullptr;

    QWidget*        titleBar_             = nullptr;
    QLabel*         appLogoLbl_           = nullptr;
    QLabel*         titleBarText_         = nullptr;
    QLabel*         titleBarSubtitle_     = nullptr;
    QPushButton*    titleMinBtn_          = nullptr;
    QPushButton*    titleMaxBtn_          = nullptr;
    QPushButton*    titleCloseBtn_        = nullptr;

    QWidget*        sidebar_              = nullptr;
    QListWidget*    sidebarList_          = nullptr;
    QLabel*         indexedHeaderLbl_     = nullptr;
    QLabel*         indexedInfoLbl_       = nullptr;
    QLabel*         extractedInfoLbl_     = nullptr;
    QLabel*         embeddedInfoLbl_      = nullptr;
    QProgressBar*   indexedBar_           = nullptr;

    SearchBar*      searchBar_            = nullptr;
    QSplitter*      mainSplitter_         = nullptr;
    ResultsPane*    resultsPane_          = nullptr;
    PreviewPane*    previewPane_          = nullptr;
    FilePreviewPane* filePreviewPane_     = nullptr;
    QPushButton*    semanticToggleBtn_    = nullptr;
    QWidget*        aiControlWidget_     = nullptr;
    QLabel*         aiIconLbl_           = nullptr;
    class SwitchControl* aiSwitch_            = nullptr;
    QLabel*         aiStateLbl_          = nullptr;
    QPushButton*    themeToggleBtn_       = nullptr;

    std::unique_ptr<BgeService>        bgeService_;
    QFuture<void>                      bgeInitFuture_;
    std::unique_ptr<HybridSearchEngine> hybridSearch_;
    bool            semanticEnabled_     = false;

    quint64         searchGen_           = 0;
    QFuture<std::vector<HybridResult>> semanticSearchFuture_;
    QMutex          semanticSearchMutex_;
    std::atomic<bool> searchWorkersStop_{false};
    std::shared_ptr<std::atomic<bool>> activeSemanticCancel_;
    QThreadPool*    searchPool_          = nullptr;

    QList<SearchHit> dupResults_;
    QStringList      dupKeys_;

    void setAiChip(const QString& text, bool active);
    void updateTitleBarState();

    QSplitter*      rightSplitter_        = nullptr;
    MetadataPane*   metadataPane_         = nullptr;
    TagsNotesPane*  tagsNotesPane_        = nullptr;

    QLabel*         statusDotLbl_         = nullptr;
    QLabel*         statusReadyLbl_       = nullptr;
    QLabel*         statusIndexedLbl_     = nullptr;
    QLabel*         statusSizeLbl_        = nullptr;
    QLabel*         statusLastLbl_        = nullptr;
    QPushButton*    openLocationBtn_      = nullptr;
    QProgressBar*   extractionProgressBar_ = nullptr;
    QWidget*        ocrStatusWidget_      = nullptr;
    QLabel*         ocrDotLbl_            = nullptr;
    QLabel*         ocrStatusLbl_         = nullptr;
    QLabel*         memoryStatusLbl_      = nullptr;  // PHASE 1: Memory pressure indicator

    IndexingProgressWidget* indexingWidget_ = nullptr;

    QTimer*         liveSearchTimer_      = nullptr;
    QTimer*         autoScanTimer_        = nullptr;
    QString         pendingQuery_;

    AppSettings     settings_;
    bool            darkMode_             = true;
    int             pastelTheme_          = 0;
    bool            dbResetting_          = false;

    bool            autoScanRunning_      = false;
    qint64          autoScanStartedMs_    = 0;
    // v1.7.23 tier gates (audit C2/B3): read once from TierConfig at
    // construction and actually consumed by the matching code paths.
    bool            liveIndexingEnabled_       = true;
    bool            autoScanEnabled_           = true;
    bool            duplicateDetectionEnabled_ = true;
    bool            autoBackupEnabled_         = false;
    bool            autoBackupInFlight_        = false;
    QTimer*         statsCoalesceTimer_        = nullptr;
    QFutureWatcher<bool> autoBackupWatcher_;
    int             autoExtractRetryLeft_ = 0;
    qint64          lastWatcherRescanMs_  = 0;
    bool            maximized_            = false;
    bool            ocrBtnEnabled_        = true;
    qint64          selectedFileId_       = 0;
    QString         selectedPath_;
};

} // namespace DocuSearch
