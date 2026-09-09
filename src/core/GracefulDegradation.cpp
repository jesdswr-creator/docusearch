// ============================================================
// GracefulDegradation.cpp
// ============================================================

#include "GracefulDegradation.h"
#include "Logger.h"
#include <QTimer>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace DocuSearch {

GracefulDegradation::GracefulDegradation(QObject* parent)
    : QObject(parent)
{
    checkTimer_ = new QTimer(this);
    connect(checkTimer_, &QTimer::timeout, this, &GracefulDegradation::onMemoryCheck);
}

GracefulDegradation::~GracefulDegradation() {
    stopMonitoring();
}

void GracefulDegradation::startMonitoring() {
    checkTimer_->start(10000);  // Check every 10 seconds
    onMemoryCheck();  // Initial check
}

void GracefulDegradation::stopMonitoring() {
    checkTimer_->stop();
}

void GracefulDegradation::onMemoryCheck() {
#ifdef Q_OS_WIN
    MEMORYSTATUSEX mem = {};
    mem.dwLength = sizeof(mem);
    GlobalMemoryStatusEx(&mem);

    qint64 free = mem.ullAvailPhys;
    qint64 total = mem.ullTotalPhys;
    int percentFree = (free * 100) / total;

    DegradationLevel newLevel = DegradationLevel::Healthy;

    if (percentFree < 10) {
        newLevel = DegradationLevel::Emergency;
    } else if (percentFree < 25) {
        newLevel = DegradationLevel::Critical;
    } else if (percentFree < 50) {
        newLevel = DegradationLevel::Warning;
    }

    if (newLevel != currentLevel_) {
        previousLevel_ = currentLevel_;
        currentLevel_ = newLevel;
        DS_WARN("Degradation",
            QString("Memory pressure: %1% free → %2").arg(percentFree)
                .arg(newLevel == DegradationLevel::Healthy ? "Healthy" :
                     newLevel == DegradationLevel::Warning ? "Warning" :
                     newLevel == DegradationLevel::Critical ? "Critical" : "Emergency"));
        applyDegradation(newLevel);
        emit degradationLevelChanged(newLevel);
    }
#endif
}

void GracefulDegradation::applyDegradation(DegradationLevel level) {
    switch (level) {
        case DegradationLevel::Healthy:
            if (indexingPausedByPressure_) {
                indexingPausedByPressure_ = false;
                emit indexingResumed();
                DS_INFO("Degradation", "Resuming indexing — memory recovered");
            }
            if (ocrPausedByPressure_) {
                ocrPausedByPressure_ = false;
                emit ocrResumed();
                DS_INFO("Degradation", "Resuming OCR — memory recovered");
            }
            break;

        case DegradationLevel::Warning:
            // Slow down indexing (increase pause between batches)
            // But keep semantic search and OCR running
            DS_WARN("Degradation", "Warning: Slowing down indexing");
            break;

        case DegradationLevel::Critical:
            // Pause OCR, reduce thread count, but keep semantic search
            if (!ocrPausedByPressure_) {
                ocrPausedByPressure_ = true;
                emit ocrPaused();
                DS_WARN("Degradation", "CRITICAL: Pausing OCR to free memory");
            }
            break;

        case DegradationLevel::Emergency:
            // Pause everything except semantic search
            if (!indexingPausedByPressure_) {
                indexingPausedByPressure_ = true;
                emit indexingPaused();
                DS_WARN("Degradation", "EMERGENCY: Pausing indexing");
            }
            if (!ocrPausedByPressure_) {
                ocrPausedByPressure_ = true;
                emit ocrPaused();
                DS_WARN("Degradation", "EMERGENCY: Pausing OCR");
            }
            break;
    }
}

} // namespace DocuSearch
