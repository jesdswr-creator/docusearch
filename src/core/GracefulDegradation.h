#pragma once

// ============================================================
// GracefulDegradation.h - Slow down under memory pressure
// ============================================================
// Semantic search is NEVER disabled — only throttled (smaller
// batches, serial queries). OCR / extraction pause only at
// Critical / Emergency so the UI and keyword search stay alive.
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
    ~GracefulDegradation() override;

    void setOcrPool(OcrWorkerPool* pool) { ocrPool_ = pool; }
    void setExtractionController(ExtractionController* ctrl) { extractionCtrl_ = ctrl; }
    void setEmbeddingController(EmbeddingController* ctrl) { embeddingCtrl_ = ctrl; }
    // v1.7.23: the tick interval restored at Healthy. MainWindow seeds
    // it with the tier's base pacing (200 ms + indexingPauseMs) so a
    // LowEnd machine keeps its slower indexing after pressure clears
    // instead of snapping back to a hardcoded 200 ms (audit C2).
    void setHealthyTickMs(int ms) { healthyTickMs_ = qMax(20, ms); }

    DegradationLevel level() const { return currentLevel_; }
    bool isPaused() const { return currentLevel_ == DegradationLevel::Emergency; }
    bool isWarning() const {
        // enum class has no operator>= on MSVC; compare the underlying values.
        return static_cast<int>(currentLevel_)
            >= static_cast<int>(DegradationLevel::Warning);
    }

    static QString levelName(DegradationLevel level);

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

private:
    void applyDegradation(DegradationLevel level);

    QTimer* checkTimer_ = nullptr;
    DegradationLevel currentLevel_ = DegradationLevel::Healthy;
    int healthyTickMs_ = 200;

    std::atomic<bool> indexingPausedByPressure_{false};
    std::atomic<bool> ocrPausedByPressure_{false};

    OcrWorkerPool* ocrPool_ = nullptr;
    ExtractionController* extractionCtrl_ = nullptr;
    EmbeddingController* embeddingCtrl_ = nullptr;
};

} // namespace DocuSearch
