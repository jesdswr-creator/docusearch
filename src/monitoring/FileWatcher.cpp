// ============================================================
// FileWatcher.cpp — Windows ReadDirectoryChangesW implementation
// ============================================================

#include "FileWatcher.h"
#include "../core/Logger.h"
#include "../core/FileUtils.h"
#include "../core/Constants.h"

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <shlwapi.h>
#endif

#include <QFileInfo>
#include <QDir>

namespace DocuSearch {

#ifdef Q_OS_WIN

class WatchThread : public QThread {
public:
    WatchThread(FileWatcher* owner, FileWatcher::WatchCtx* ctx)
        : QThread(owner), owner_(owner), ctx_(ctx) {
        setObjectName(QString("FileWatcher-%1").arg(ctx_->rootDir));
    }
protected:
    void run() override { owner_->workerLoop(ctx_); }
private:
    FileWatcher* owner_;
    FileWatcher::WatchCtx* ctx_;
};

// Helper to convert wide string to QString
static inline QString fromWchar(const wchar_t* w, int len) {
    return QString::fromWCharArray(w, len);
}

#endif // Q_OS_WIN

FileWatcher::FileWatcher(QObject* parent) : QObject(parent) {}

FileWatcher::~FileWatcher() { stop(); }

bool FileWatcher::addWatch(const QString& rootDir) {
#ifdef Q_OS_WIN
    const QString native = QDir::toNativeSeparators(rootDir);
    const std::wstring w = native.toStdWString();

    HANDLE h = CreateFileW(
        w.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        DS_ERROR("FileWatcher", QString("CreateFileW failed for %1: err=%2")
                 .arg(native).arg(GetLastError()));
        return false;
    }

    auto ctx = std::make_unique<WatchCtx>();
    ctx->dirHandle = h;
    ctx->overlapped = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx->buffer.resize(Constants::kFileWatcherBufferBytes);
    ctx->rootDir = native;
    ctx->stopping.store(false);

    auto* thread = new WatchThread(this, ctx.get());
    ctx->thread = thread;
    WatchCtx* raw = ctx.get();
    contexts_.push_back(std::move(ctx));
    thread->start();

    DS_INFO("FileWatcher", QString("Watching: %1").arg(native));
    return true;
#else
    Q_UNUSED(rootDir);
    emit logMessage("FileWatcher not implemented on this platform");
    return false;
#endif
}

void FileWatcher::addWatches(const QStringList& roots) {
    for (const auto& r : roots) addWatch(r);
}

void FileWatcher::stop() {
#ifdef Q_OS_WIN
    for (auto& ctx : contexts_) {
        ctx->stopping.store(true);
        if (ctx->overlapped) SetEvent(ctx->overlapped);
        CancelIoEx(ctx->dirHandle, nullptr);
    }
    for (auto& ctx : contexts_) {
        if (ctx->thread && ctx->thread->isRunning()) {
            ctx->thread->quit();
            ctx->thread->wait(2000);
        }
    }
    for (auto& ctx : contexts_) {
        if (ctx->overlapped) CloseHandle(ctx->overlapped);
        if (ctx->dirHandle != INVALID_HANDLE_VALUE) CloseHandle(ctx->dirHandle);
    }
    contexts_.clear();
#endif
}

void FileWatcher::workerLoop(WatchCtx* ctx) {
#ifdef Q_OS_WIN
    const DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME |
                         FILE_NOTIFY_CHANGE_DIR_NAME |
                         FILE_NOTIFY_CHANGE_ATTRIBUTES |
                         FILE_NOTIFY_CHANGE_SIZE |
                         FILE_NOTIFY_CHANGE_LAST_WRITE |
                         FILE_NOTIFY_CHANGE_CREATION |
                         FILE_NOTIFY_CHANGE_SECURITY;

    OVERLAPPED ov{};
    ov.hEvent = ctx->overlapped;

    while (!ctx->stopping.load()) {
        DWORD bytesReturned = 0;
        ZeroMemory(ctx->buffer.data(), ctx->buffer.size());

        if (!ReadDirectoryChangesW(
                ctx->dirHandle,
                ctx->buffer.data(),
                static_cast<DWORD>(ctx->buffer.size()),
                TRUE,                       // recursive
                filter,
                &bytesReturned,
                &ov,
                nullptr)) {
            DS_ERROR("FileWatcher", "ReadDirectoryChangesW failed");
            break;
        }

        // Wait for completion
        DWORD waitRc = WaitForSingleObject(ov.hEvent, INFINITE);
        if (ctx->stopping.load()) break;
        if (waitRc != WAIT_OBJECT_0) continue;

        if (!GetOverlappedResult(ctx->dirHandle, &ov, &bytesReturned, FALSE)) {
            DS_WARN("FileWatcher", "GetOverlappedResult failed");
            continue;
        }
        if (bytesReturned == 0) continue;

        // Iterate FILE_NOTIFY_INFORMATION records. Each record has:
        //   NextEntryOffset (DWORD) — byte offset to the next record, 0 = last
        //   Action              (DWORD)
        //   FileNameLength      (DWORD) — in bytes
        //   FileName[]          (WCHAR[]) — NOT null-terminated
        // For renames, Windows emits an OLD_NAME record immediately followed by a
        // NEW_NAME record. We pair them up so callers see a single renamed() signal.
        BYTE* p = reinterpret_cast<BYTE*>(ctx->buffer.data());
        for (;;) {
            auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(p);
            const QString name = QString::fromWCharArray(
                info->FileName, info->FileNameLength / sizeof(wchar_t));
            const QString full = ctx->rootDir +
                (ctx->rootDir.endsWith('\\') ? "" : "\\") + name;

            DWORD nextOffset = info->NextEntryOffset;

            switch (info->Action) {
            case FILE_ACTION_ADDED:
                emit fileAdded(full);
                break;
            case FILE_ACTION_MODIFIED:
                emit fileModified(full);
                break;
            case FILE_ACTION_RENAMED_OLD_NAME: {
                // The next record in this buffer should be the matching NEW_NAME.
                // If there is no next record (buffer ended on OLD_NAME — rare),
                // treat as a delete of the old path.
                if (nextOffset == 0) {
                    emit fileDeleted(full);
                } else {
                    BYTE* pNext = p + nextOffset;
                    auto* next = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(pNext);
                    const QString newName = QString::fromWCharArray(
                        next->FileName, next->FileNameLength / sizeof(wchar_t));
                    const QString newFull = ctx->rootDir +
                        (ctx->rootDir.endsWith('\\') ? "" : "\\") + newName;
                    emit fileRenamed(full, newFull);
                    // Consume BOTH records: advance to the entry after NEW_NAME.
                    nextOffset = nextOffset + next->NextEntryOffset;
                }
                break;
            }
            case FILE_ACTION_RENAMED_NEW_NAME:
                // Already consumed by the matching OLD_NAME record above.
                // A standalone NEW_NAME (without a preceding OLD_NAME) should not
                // happen in practice; ignore it defensively.
                break;
            case FILE_ACTION_REMOVED:
                emit fileDeleted(full);
                break;
            default:
                break;
            }

            if (nextOffset == 0) break;
            p += nextOffset;
        }
    }
#else
    Q_UNUSED(ctx);
#endif
}

} // namespace DocuSearch
