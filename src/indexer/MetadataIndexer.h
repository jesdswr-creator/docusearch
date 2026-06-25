#pragma once

// ============================================================
// MetadataIndexer.h - Phase 1: fast recursive file metadata scan
// ============================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include <atomic>
#include <functional>

namespace DocuSearch {

class FileRepository;

class MetadataIndexer : public QObject {
    Q_OBJECT
public:
    using CancelFlag = std::shared_ptr<std::atomic<bool>>;

    MetadataIndexer(FileRepository& repo, QObject* parent = nullptr);

    // Scan `roots` recursively, skipping `excludes`. Stops early if cancel flag is set.
    void scan(const QStringList& roots, const QStringList& excludes,
              CancelFlag cancel = {});

    qint64 filesScanned() const { return filesScanned_.load(); }
    qint64 errorsCount()  const { return errors_.load(); }

signals:
    void progress(qint64 filesScanned);
    void finished(qint64 totalScanned, qint64 errors);

private:
    FileRepository& repo_;
    std::atomic<qint64> filesScanned_{0};
    std::atomic<qint64> errors_{0};
};

} // namespace DocuSearch
