// ============================================================
// ContentIndexer.cpp
// ============================================================

#include "ContentIndexer.h"
#include "../database/FileRepository.h"
#include "../documents/DocumentExtractorRegistry.h"
#include "../ocr/OcrWorkerPool.h"
#include "../ocr/OcrEngine.h"
#include "../core/Constants.h"
#include "../core/Logger.h"
#include "../core/FileUtils.h"
#include "../core/StringUtils.h"

#include <QElapsedTimer>
#include <QFileInfo>

namespace DocuSearch {

ContentIndexer::ContentIndexer(FileRepository& repo, OcrWorkerPool& ocrPool,
                               QObject* parent)
    : QObject(parent), repo_(repo), ocrPool_(ocrPool) {}

bool ContentIndexer::processOne() {
    QString path, ext;
    const qint64 id = repo_.nextPendingFile(path, ext);
    if (id == 0) return false;

    // Mark as "in progress" by setting a special status so the next call
    // doesn't pick the same row. We use 'metadata_only' as "in progress".
    // (A dedicated 'processing' status would be cleaner.)
    repo_.updateStatus(id, "processing", QString());

    QFileInfo fi(path);
    if (!fi.exists() || !fi.isReadable()) {
        repo_.updateStatus(id, Constants::IndexingStatus::kFailed,
                           Constants::OcrStatus::kSkipped);
        errors_.fetch_add(1);
        emit fileProcessed(id, path, false);
        return true;
    }

    ExtractionResult result;
    try {
        result = DocumentExtractorRegistry::instance().extractByExtension(path, ext);
    } catch (const std::exception& e) {
        result.errorMessage = QString("Extractor exception: %1").arg(e.what());
    }

    if (!result.errorMessage.isEmpty() && result.text.isEmpty()) {
        DS_WARN("Indexer", QString("Extract failed for %1: %2").arg(path, result.errorMessage));
    }

    // Decide OCR strategy.
    QString source = result.source;
    if (source.isEmpty()) source = "native";

    // If text was empty AND the file is an image, OCR is mandatory.
    // If needsOcr=true (PDF with empty pages), OCR is mandatory.
    bool needOcr = result.needsOcr ||
                   (result.text.trimmed().isEmpty() &&
                    FileUtils::hasExtension(path, Constants::kImageExtensions));

    QString indexingStatus = result.text.isEmpty() && !needOcr
        ? Constants::IndexingStatus::kFailed
        : Constants::IndexingStatus::kContentDone;

    QString ocrStatus = needOcr ? Constants::OcrStatus::kPending
                                : Constants::OcrStatus::kNotNeeded;

    repo_.updateContent(id, result.text, source, indexingStatus, ocrStatus);

    if (needOcr) {
        OcrTask task;
        task.fileId = id;
        task.path = path;
        task.extension = ext;
        ocrPool_.enqueue(task);
        ocrQueued_.fetch_add(1);
        emit ocrEnqueued(id);
    }

    docsIndexed_.fetch_add(1);
    emit fileProcessed(id, path, result.text.isEmpty() ? false : true);
    return true;
}

int ContentIndexer::processBatch(int max) {
    int processed = 0;
    for (int i = 0; i < max; ++i) {
        if (!processOne()) break;
        ++processed;
    }
    if (processed > 0) {
        emit progress(docsIndexed_.load(),
                      repo_.countByStatus(Constants::IndexingStatus::kPending));
    }
    return processed;
}

QString ContentIndexer::processNow(qint64 fileId) {
    FileRecord r;
    if (!repo_.getById(fileId, r)) return {};
    if (r.indexingStatus == Constants::IndexingStatus::kContentDone &&
        r.ocrStatus      == Constants::OcrStatus::kDone) {
        // Already indexed - return existing text.
        // Fetch from DocumentText via repo
        // (getById doesn't load text; we fetch explicitly via a small SQL.)
        // For simplicity, return empty here - UI will show snippet from FTS.
        return {};
    }

    ExtractionResult result;
    try {
        result = DocumentExtractorRegistry::instance().extractByExtension(
            r.path, r.extension);
    } catch (...) {
        result.errorMessage = "Extractor exception during lazy index";
    }

    const bool needOcr = result.needsOcr ||
                         (result.text.trimmed().isEmpty() &&
                          FileUtils::hasExtension(r.path, Constants::kImageExtensions));

    repo_.updateContent(fileId, result.text, result.source.isEmpty() ? "native" : result.source,
                        Constants::IndexingStatus::kContentDone,
                        needOcr ? Constants::OcrStatus::kPending
                                : Constants::OcrStatus::kNotNeeded);

    if (needOcr && settings_.lazyOcrEnabled) {
        // Run OCR synchronously on this thread (with timeout).
        OcrEngine engine(settings_.tessdataPath, settings_.ocrLanguage);
        if (engine.init()) {
            QString ocrText = engine.ocrFile(r.path);
            if (!ocrText.isEmpty()) {
                QString combined = result.text.isEmpty() ? ocrText
                                                         : result.text + "\n" + ocrText;
                repo_.updateContent(fileId, combined, "native+ocr",
                                    Constants::IndexingStatus::kOcrDone,
                                    Constants::OcrStatus::kDone);
                return combined;
            }
        }
    }
    return result.text;
}

} // namespace DocuSearch
