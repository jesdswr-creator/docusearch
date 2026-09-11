// ============================================================
// GracefulDegradation.cpp
// ============================================================

#include "GracefulDegradation.h"
#include "Logger.h"
#include "MemoryMonitor.h"
#include "ExtractionController.h"
#include "ocr/OcrWorkerPool.h"
#include "embeddings/EmbeddingController.h"

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace DocuSearch {

GracefulDegradation::GracefulDegradation(QObject* parent)
    : QObject(parent)
{
    checkTimer_ = new QTimer(this);
    checkTimer_->setTimerType(Qt::CoarseTimer);
    connect(checkTimer_, &QTimer::timeout, this, &GracefulDegradation::onMemoryCheck);
}

GracefulDegradation::~GracefulDegradation() {
    stopMonitoring();
}

QString GracefulDegradation::levelName(DegradationLevel level) {
    switch (level) {
        case DegradationLevel::Healthy:   return QStringLiteral("Healthy");
        case DegradationLevel::Warning:   return QStringLiteral("Warning");
        case DegradationLevel::Critical:  return QStringLiteral("Critical");
        case DegradationLevel::Emergency: return QStringLiteral("Emergency");
    }
    return QStringLiteral("Unknown");
}

void GracefulDegradation::startMonitoring() {
    checkTimer_->start(10000);
    onMemoryCheck();
}

void GracefulDegradation::stopMonitoring() {
    checkTimer_->stop();
}

void GracefulDegradation::onMemoryCheck() {
    // C5 (audit 2026-09-09): this 10 s poll used to call
    // GlobalMemoryStatusEx itself, overlapping MemoryMonitor's 5 s
    // sampler. MemoryMonitor already publishes everything needed as
    // atomics - read its (at most 5 s old) sample instead, keeping a
    // direct-sampling fallback only for the window before the monitor
    // exists (construction order).
    int percentFree = 100;
    if (MemoryMonitor* mm = MemoryMonitor::instance()) {
        percentFree = mm->percentageFree();
    } else {
#ifdef Q_OS_WIN
        MEMORYSTATUSEX mem = {};
        mem.dwLength = sizeof(mem);
        if (GlobalMemoryStatusEx(&mem) && mem.ullTotalPhys > 0) {
            percentFree = static_cast<int>((mem.ullAvailPhys * 100) / mem.ullTotalPhys);
        }
#endif
    }

    DegradationLevel newLevel = DegradationLevel::Healthy;
    if (percentFree < 10) {
        newLevel = DegradationLevel::Emergency;
    } else if (percentFree < 25) {
        newLevel = DegradationLevel::Critical;
    } else if (percentFree < 50) {
        newLevel = DegradationLevel::Warning;
    }

    // Leave emergency only above 15%, critical only above 35%, so we
    // don't flap on a noisy 24/26 reading.
    if (currentLevel_ == DegradationLevel::Emergency && percentFree < 15)
        newLevel = DegradationLevel::Emergency;
    else if (currentLevel_ == DegradationLevel::Critical && percentFree < 35
             && newLevel == DegradationLevel::Healthy)
        newLevel = DegradationLevel::Warning;

    if (newLevel == currentLevel_) return;

    currentLevel_ = newLevel;
    DS_WARN("Degradation",
        QString("Memory pressure: %1% free → %2")
            .arg(percentFree)
            .arg(levelName(newLevel)));
    applyDegradation(newLevel);
    emit degradationLevelChanged(newLevel);
}

void GracefulDegradation::applyDegradation(DegradationLevel level) {
    switch (level) {
        case DegradationLevel::Healthy:
            if (extractionCtrl_) {
                extractionCtrl_->setPaused(false);
                extractionCtrl_->setTickIntervalMs(healthyTickMs_);
            }
            if (ocrPool_ && ocrPausedByPressure_) {
                ocrPool_->resume();
            }
            // v1.7.26: leave pressure mode only at full recovery —
            // Warning keeps the throttle so a machine hovering around
            // 30-50% free does not flap between batch sizes.
            if (embeddingCtrl_) {
                embeddingCtrl_->setPaused(false);
                embeddingCtrl_->setPressureMode(false);
            }
            if (indexingPausedByPressure_) {
                indexingPausedByPressure_ = false;
                emit indexingResumed();
            }
            if (ocrPausedByPressure_) {
                ocrPausedByPressure_ = false;
                emit ocrResumed();
            }
            DS_INFO("Degradation", "Resumed background work — memory recovered");
            break;

        case DegradationLevel::Warning:
            // Slow the extraction tick; keep OCR + semantic search running.
            if (extractionCtrl_) extractionCtrl_->setTickIntervalMs(500);
            if (embeddingCtrl_) embeddingCtrl_->setPaused(false);
            DS_WARN("Degradation", "Warning: slowing extraction (500 ms/file)");
            break;

        case DegradationLevel::Critical:
            if (extractionCtrl_) {
                extractionCtrl_->setPaused(false);
                extractionCtrl_->setTickIntervalMs(1000);
            }
            if (ocrPool_ && !ocrPausedByPressure_) {
                ocrPool_->pause();
                ocrPausedByPressure_ = true;
                emit ocrPaused();
            }
            // v1.7.26 THE MOTO FIX: embeddings THROTTLE instead of
            // stopping. <25% free RAM is the steady state on a low-
            // memory PC, so the old setPaused(true) here kept the AI
            // index permanently dark ("RAM critical and all stop").
            // Pressure mode halves the batch text budget and stretches
            // the chain delay so ONNX stops fighting the user for RAM
            // while the index KEEPS BUILDING. Search stays instant
            // (dedicated pool) and the chunk scan budget halves itself
            // in HybridSearchEngine under the same condition.
            if (embeddingCtrl_) {
                embeddingCtrl_->setPaused(false);
                embeddingCtrl_->setPressureMode(true);
            }
            DS_WARN("Degradation", "CRITICAL: OCR paused, extraction slowed, embeddings throttled");
            break;

        case DegradationLevel::Emergency:
            if (extractionCtrl_ && !indexingPausedByPressure_) {
                extractionCtrl_->setPaused(true);
                indexingPausedByPressure_ = true;
                emit indexingPaused();
            }
            if (ocrPool_ && !ocrPausedByPressure_) {
                ocrPool_->pause();
                ocrPausedByPressure_ = true;
                emit ocrPaused();
            }
            // EMERGENCY (<10% free) is the only level that still stops
            // the embedding pipeline — below this line the OS itself is
            // about to thrash, and protecting the machine protects the
            // user experience more than a few more embedded documents.
            // Search stays live (throttled by HybridSearchEngine).
            if (embeddingCtrl_) embeddingCtrl_->setPaused(true);
            DS_WARN("Degradation", "EMERGENCY: extraction + OCR + embeddings paused (search still live)");
            break;
    }
}

} // namespace DocuSearch
