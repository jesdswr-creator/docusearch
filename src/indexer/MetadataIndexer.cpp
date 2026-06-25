// ============================================================
// MetadataIndexer.cpp
// ============================================================

#include "MetadataIndexer.h"
#include "../database/FileRepository.h"
#include "../database/Database.h"
#include "../core/FileUtils.h"
#include "../core/Constants.h"
#include "../core/Logger.h"
#include "../core/Types.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QDateTime>
#include <QElapsedTimer>
#include <QMutex>

namespace DocuSearch {

MetadataIndexer::MetadataIndexer(FileRepository& repo, QObject* parent)
    : QObject(parent), repo_(repo) {}

void MetadataIndexer::scan(const QStringList& roots, const QStringList& excludes,
                           CancelFlag cancel) {
    filesScanned_.store(0);
    errors_.store(0);

    QElapsedTimer t; t.start();
    qint64 lastProgressReport = 0;

    // Track deleted files: compare DB rows against scanned set.
    // For simplicity we don't track here; FileWatcher handles live deletes.
    // For initial scan we just upsert. A prune step can be invoked separately.

    for (const QString& root : roots) {
        if (cancel && cancel->load()) break;

        QDirIterator it(root, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (cancel && cancel->load()) break;
            it.next();
            const QFileInfo info = it.fileInfo();
            const QString absPath = info.absoluteFilePath();

            // Skip excluded subtrees
            if (FileUtils::isUnderAny(absPath, excludes)) {
                if (info.isDir()) {
                    // QDirIterator doesn't expose skip - we just continue and rely on
                    // the prefix check to skip files inside.
                    continue;
                }
                continue;
            }
            if (info.isHidden()) continue;
            if (info.isDir()) continue;

            FileRecord r;
            r.path         = FileUtils::toNative(absPath);
            r.filename     = info.fileName();
            r.extension    = FileUtils::extensionOf(absPath);
            r.size         = info.size();
            r.createdDate  = info.birthTime();
            r.modifiedDate = info.lastModified();
            r.indexingStatus = Constants::IndexingStatus::kMetadataOnly;
            r.ocrStatus      = Constants::OcrStatus::kPending;

            // Optional hash for large files only
            // (We skip hashing by default here; Phase 2 can do it if needed.)

            if (repo_.upsertFile(r) == 0) {
                errors_.fetch_add(1);
            } else {
                filesScanned_.fetch_add(1);
            }

            // Throttle progress emission
            if (filesScanned_.load() - lastProgressReport > 250) {
                lastProgressReport = filesScanned_.load();
                emit progress(lastProgressReport);
            }
        }
    }

    emit progress(filesScanned_.load());
    emit finished(filesScanned_.load(), errors_.load());
    DS_INFO("Indexer", QString("Phase 1 scan complete: %1 files in %2 ms (%3 errors)")
            .arg(filesScanned_.load()).arg(t.elapsed()).arg(errors_.load()));
}

} // namespace DocuSearch
