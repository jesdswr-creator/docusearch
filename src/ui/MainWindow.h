#pragma once

// ============================================================
// MainWindow.h - Top-level window tying all modules together
// ============================================================

#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <memory>

#include "../core/Types.h"

class QSplitter;
class QMenu;
class QAction;
class QStatusBar;
class QTimer;

namespace DocuSearch {

class Database;
class FileRepository;
class SearchEngine;
class Indexer;
class OcrWorkerPool;
class FileWatcher;
class ThumbnailGenerator;

class SearchBar;
class ResultsPane;
class PreviewPane;
class MetadataPane;
class TagsNotesPane;
class IndexingProgressWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* e) override;

private slots:
    void onSearch(const QString& query);
    void onFileSelected(qint64 fileId, const QString& path);
    void onFileActivated(qint64 fileId, const QString& path);
    void onOpenOriginal(const QString& path);
    void onOpenSettings();
    void onToggleTheme();
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
    void autoScanIndexedFolders();

private:
    void buildMenus();
    void buildToolbar();
    void buildCentral();
    void applyTheme();
    void loadSettings();
    void saveSettings();
    void refreshSavedSearches();
    void openFile(const QString& path);

    // Fast metadata-only scan of a folder. Inserts file metadata via
    // raw SQL (no transactions, no hashing, no content extraction).
    // Used by onAddFolder and by Settings when new drives are added.
    void scanFolderFast(const QString& folder);

    // Refresh the preview pane with the currently-selected file's
    // extracted text (re-reads from the DB so newly-extracted content
    // appears immediately after an Extract run finishes).
    void refreshPreviewForSelectedFile();

public:
    // Update indexing widget with file counts. Q_INVOKABLE so it can be
    // called safely via QMetaObject::invokeMethod from worker threads.
    Q_INVOKABLE void updateIndexStats();

    // Owned subsystems
    std::unique_ptr<Database>       db_;
    std::unique_ptr<FileRepository> repo_;
    std::unique_ptr<SearchEngine>   search_;
    std::unique_ptr<OcrWorkerPool>  ocrPool_;
    std::unique_ptr<Indexer>        indexer_;
    std::unique_ptr<FileWatcher>    watcher_;
    std::unique_ptr<ThumbnailGenerator> thumbs_;

    // UI
    SearchBar*        searchBar_       = nullptr;
    ResultsPane*      resultsPane_     = nullptr;
    PreviewPane*      previewPane_     = nullptr;
    MetadataPane*     metadataPane_    = nullptr;
    TagsNotesPane*    tagsNotesPane_   = nullptr;
    IndexingProgressWidget* indexingWidget_  = nullptr;
    QSplitter*        mainSplitter_    = nullptr;
    QSplitter*        rightSplitter_   = nullptr;
    QTimer*           liveSearchTimer_ = nullptr;
    QTimer*           autoScanTimer_   = nullptr;
    QString           pendingQuery_;

    AppSettings       settings_;
    bool              darkMode_ = true;
    bool              contentExtractionRunning_ = false;
    bool              autoScanRunning_ = false;
    // Track the currently-selected file so we can refresh its preview
    // after a background extraction completes.
    qint64            selectedFileId_ = 0;
    QString           selectedPath_;
};

} // namespace DocuSearch
