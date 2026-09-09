#pragma once

// ============================================================
// GracefulDegradation.h - Smooth degradation under memory pressure
// ============================================================
// When RAM < 50%: Slow indexing, reduce thread count
// When RAM < 25%: Pause OCR, disable thumbnails, clear caches
// When RAM < 10%: Pause all background work
// Always: Semantic search continues (throttled, not disabled)
// ============================================================

#include <QObject>
#include <QTimer>
#include <atomic>

namespace DocuSearch {

class OcrWorkerPool;
class ExtractionController;
class EmbeddingController;

enum class DegradationLevel {
    Healthy,    // > 50% free RAM
    Warning,    // 25–50% free RAM
    Critical,   // 10–25% free RAM
    Emergency   // < 10% free RAM
};

class GracefulDegradation : public QObject {
    Q_OBJECT

public:
    explicit GracefulDegradation(QObject* parent = nullptr);
    ~GracefulDegradation();

    // Set the subsystems to control
    void setOcrPool(OcrWorkerPool* pool) { ocrPool_ = pool; }
    void setExtractionController(ExtractionController* ctrl) { extractionCtrl_ = ctrl; }
    void setEmbeddingController(EmbeddingController* ctrl) { embeddingCtrl_ = ctrl; }

    // Get current degradation level
    DegradationLevel level() const { return currentLevel_; }
    bool isPaused() const { return currentLevel_ == DegradationLevel::Emergency; }
    bool isWarning() const { return currentLevel_ >= DegradationLevel::Warning; }

    // Start/stop monitoring
    void startMonitoring();
    void stopMonitoring();

signals:
    void degradationLevelChanged(DegradationLevel newLevel);
    void indexingPaused();
    void indexingResumed();
    void ocrPaused();
    void ocrResumed();

private slots:
    void onMemoryCheck();
    void applyDegradation(DegradationLevel level);

private:
    QTimer* checkTimer_ = nullptr;
    DegradationLevel currentLevel_ = DegradationLevel::Healthy;
    DegradationLevel previousLevel_ = DegradationLevel::Healthy;
    std::atomic<bool> indexingPausedByPressure_{false};
    std::atomic<bool> ocrPausedByPressure_{false};

    OcrWorkerPool* ocrPool_ = nullptr;
    ExtractionController* extractionCtrl_ = nullptr;
    EmbeddingController* embeddingCtrl_ = nullptr;
};

} // namespace DocuSearch
