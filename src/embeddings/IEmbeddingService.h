#pragma once

// ============================================================
// IEmbeddingService.h - the extraction-side view of the AI service
// ============================================================
//
// EmbeddingController drives the embedding backfill through this
// interface instead of the concrete BgeService, so the whole backfill
// state machine (phase A/B selection, chaining, deadlock guard,
// rebuild purge chain) is testable headless: tst_Wiring injects a
// fake service and asserts the batches/chaining without ONNX.
//
// BgeService implements this — the two methods already existed there
// with identical signatures; adopting the base is the whole change.
// ============================================================

#include <QVector>
#include <QStringList>

namespace DocuSearch {

struct IEmbeddingService {
    virtual ~IEmbeddingService() = default;
    // True once the model is loaded and the embeddings DB is open.
    virtual bool isReady() const = 0;
    // Queue a batch on the worker pool. Progress and completion are
    // reported asynchronously via the service's signals (the window
    // forwards them to EmbeddingController::noteEmbeddingProgress /
    // noteEmbeddingFinished).
    virtual void embedDocumentsBatch(const QVector<int>& fileIds,
                                     const QStringList& texts) = 0;
};

} // namespace DocuSearch
