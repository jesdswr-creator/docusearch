// ============================================================
// MemoryMonitor.cpp
// ============================================================

#include "MemoryMonitor.h"
#include "Logger.h"
#include <QThread>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace DocuSearch {

MemoryMonitor::MemoryMonitor(QObject* parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &MemoryMonitor::checkMemory);
}

MemoryMonitor::~MemoryMonitor() {
    stopMonitoring();
}

void MemoryMonitor::startMonitoring() {
    m_timer->start(5000);  // Check every 5 seconds
    checkMemory();  // Initial check
}

void MemoryMonitor::stopMonitoring() {
    m_timer->stop();
}

void MemoryMonitor::checkMemory() {
#ifdef Q_OS_WIN
    MEMORYSTATUSEX mem = {};
    mem.dwLength = sizeof(mem);
    GlobalMemoryStatusEx(&mem);
    
    m_freeRAM = mem.ullAvailPhys;
    qint64 total = mem.ullTotalPhys;
    m_percentFree = (int)((m_freeRAM * 100) / total);
#endif
    
    bool wasCritical = m_criticalMode.load();
    bool wasWarning = m_warningMode.load();
    
    // ── Threshold Logic ──
    if (m_percentFree < 25) {
        if (!m_criticalMode.load()) {
            m_criticalMode = true;
            DS_WARN("MemoryMonitor",
                QString("CRITICAL: Free RAM %1% (< 25%). Throttling OCR + search...").arg(m_percentFree));
            emit pressureCritical();
        }
    } else if (m_percentFree < 50) {
        if (!m_warningMode.load()) {
            m_warningMode = true;
            DS_WARN("MemoryMonitor",
                QString("WARNING: Free RAM %1% (< 50%). Slowing indexing...").arg(m_percentFree));
            emit pressureWarning();
        }
    } else if (m_percentFree > 75) {
        if (m_criticalMode.load() || m_warningMode.load()) {
            m_criticalMode = false;
            m_warningMode = false;
            DS_INFO("MemoryMonitor",
                QString("RECOVERED: Free RAM %1%. Resuming normal operation...").arg(m_percentFree));
            emit pressureRecovered();
        }
    }
}

} // namespace DocuSearch
