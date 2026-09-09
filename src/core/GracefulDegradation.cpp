// ============================================================
// GracefulDegradation.cpp
// ============================================================

#include "GracefulDegradation.h"
#include "Logger.h"
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
    int percentFree = 100;
#ifdef Q_OS_WIN
    MEMORYSTATUSEX mem = {};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem) && mem.ullTotalPhys > 0) {
        percentFree = static_cast<int>((mem.ullAvailPhys * 100) / mem.ullTotalPhys);
    }
#endif

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
            if (embeddingCtrl_) embeddingCtrl_->setPaused(false);
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
            // Semantic search stays on. Embedding backfill pauses so
            // ONNX doesn't fight the user for RAM.
            if (embeddingCtrl_) embeddingCtrl_->setPaused(true);
            DS_WARN("Degradation", "CRITICAL: OCR paused, extraction slowed, embeddings paused");
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
            if (embeddingCtrl_) embeddingCtrl_->setPaused(true);
            DS_WARN("Degradation", "EMERGENCY: extraction + OCR paused (search still live)");
            break;
    }
}

} // namespace DocuSearch
