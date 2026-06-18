#pragma once

// ============================================================
// FileWatcher.h — Windows ReadDirectoryChangesW-based recursive watcher
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

    // Stop all watchers and free handles.
    void stop();

signals:
    void fileAdded(const QString& path);
    void fileModified(const QString& path);
    void fileRenamed(const QString& oldPath, const QString& newPath);
    void fileDeleted(const QString& path);
    void logMessage(const QString& msg);

#ifdef Q_OS_WIN
    // Public so the WatchThread helper class in FileWatcher.cpp can access it.
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
