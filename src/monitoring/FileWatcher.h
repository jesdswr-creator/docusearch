#pragma once

// ============================================================
// FileWatcher.h - Windows ReadDirectoryChangesW-based recursive watcher
// ============================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QHash>
#include <QMutex>
#include <atomic>
#include <vector>
#include <memory>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace DocuSearch {

class FileWatcher : public QObject {
    Q_OBJECT
public:
    explicit FileWatcher(QObject* parent = nullptr);
    ~FileWatcher() override;

    // Add a root directory tree to monitor (recursive).
    bool addWatch(const QString& rootDir);

    // Add multiple at once.
    void addWatches(const QStringList& roots);

    // v1.7.4: stop watching ONE root (Settings -> remove indexed folder).
    // The old API could only stop everything or nothing, so a folder
    // removed from Settings stayed watched forever and its file events
    // kept landing on the index ("removed the folders ... nothing happens").
    // Returns true if a matching watch was found and stopped.
    bool removeWatch(const QString& rootDir);

    // True if a watch is currently active for the given root
    // (path comparison is case-insensitive, native separators).
    bool isWatched(const QString& rootDir) const;

    // Stop all watchers and free handles.
    void stop();

signals:
    void fileAdded(const QString& path);
    void fileModified(const QString& path);
    void fileRenamed(const QString& oldPath, const QString& newPath);
    void fileDeleted(const QString& path);
    void logMessage(const QString& msg);
    // v1.7.4: the kernel change buffer for a root overflowed (deep trees,
    // high-frequency edits). Events were LOST. The old code killed the
    // watch thread and never resumed — live tracking silently died for
    // that root and the index went stale until the next manual scan.
    // The thread now stays alive and asks for a reconciling scan instead.
    void rescanRequested(const QString& rootDir);

#ifdef Q_OS_WIN
public:
    // Public so the WatchThread helper class in FileWatcher.cpp can access it.
    // MUST be under 'public:' (not after 'signals:') or moc will try to parse
    // the struct fields as signal declarations and fail.
    struct WatchCtx {
        HANDLE      dirHandle = INVALID_HANDLE_VALUE;
        HANDLE      overlapped = nullptr;  // OVERLAPPED event handle
        QByteArray  buffer;
        QString     rootDir;
        QThread*    thread = nullptr;
        std::atomic<bool> stopping{false};
    };

    void workerLoop(WatchCtx* ctx);
private:
    std::vector<std::unique_ptr<WatchCtx>> contexts_;
#endif
};

} // namespace DocuSearch
