// ============================================================
// MemoryMonitor.cpp
// ============================================================

#include "MemoryMonitor.h"
#include "Logger.h"

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace DocuSearch {

MemoryMonitor* MemoryMonitor::s_instance = nullptr;

MemoryMonitor* MemoryMonitor::instance() {
    return s_instance;
}

MemoryMonitor::MemoryMonitor(QObject* parent)
    : QObject(parent)
{
    s_instance = this;
    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::CoarseTimer);
    connect(m_timer, &QTimer::timeout, this, &MemoryMonitor::checkMemory);
}

MemoryMonitor::~MemoryMonitor() {
    stopMonitoring();
    if (s_instance == this) s_instance = nullptr;
}

void MemoryMonitor::startMonitoring() {
    if (!m_timer->isActive())
        m_timer->start(5000);
    checkMemory();
}

void MemoryMonitor::stopMonitoring() {
    if (m_timer) m_timer->stop();
}

int MemoryMonitor::sampleCpuPercent() {
#ifdef Q_OS_WIN
    FILETIME idle, kernel, user;
    if (!GetSystemTimes(&idle, &kernel, &user)) return m_cpuPercent.load();
    auto toU64 = [](const FILETIME& ft) -> quint64 {
        return (static_cast<quint64>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    };
    const quint64 idleNow   = toU64(idle);
    const quint64 kernelNow = toU64(kernel);
    const quint64 userNow   = toU64(user);
    if (m_prevKernel == 0) {
        m_prevIdle = idleNow;
        m_prevKernel = kernelNow;
        m_prevUser = userNow;
        return 0;
    }
    const quint64 idleDelta   = idleNow - m_prevIdle;
    const quint64 kernelDelta = kernelNow - m_prevKernel;
    const quint64 userDelta   = userNow - m_prevUser;
    m_prevIdle = idleNow;
    m_prevKernel = kernelNow;
    m_prevUser = userNow;
    const quint64 total = kernelDelta + userDelta;
    if (total == 0) return m_cpuPercent.load();
    // Kernel time includes idle time on Windows.
    const int busy = static_cast<int>(((total - idleDelta) * 100) / total);
    return qBound(0, busy, 100);
#else
    return 0;
#endif
}

void MemoryMonitor::checkMemory() {
#ifdef Q_OS_WIN
    MEMORYSTATUSEX mem = {};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        m_freeRAM.store(static_cast<qint64>(mem.ullAvailPhys));
        m_totalRAM.store(static_cast<qint64>(mem.ullTotalPhys));
        if (mem.ullTotalPhys > 0) {
            m_percentFree.store(static_cast<int>(
                (mem.ullAvailPhys * 100) / mem.ullTotalPhys));
        }
    }
#else
    m_percentFree.store(100);
#endif

    m_cpuPercent.store(sampleCpuPercent());

    const int pct = m_percentFree.load();

    // Hysteresis: enter critical at <25, leave at >35;
    // enter warning at <50, leave at >60. Prevents flapping.
    if (pct < 25) {
        if (!m_criticalMode.exchange(true)) {
            m_warningMode.store(true);
            DS_WARN("MemoryMonitor",
                QString("CRITICAL: Free RAM %1% (< 25%). Throttling background work.")
                    .arg(pct));
            emit pressureCritical();
        }
    } else if (pct < 50) {
        if (m_criticalMode.load() && pct > 35) {
            m_criticalMode.store(false);
        }
        if (!m_warningMode.exchange(true) && !m_criticalMode.load()) {
            DS_WARN("MemoryMonitor",
                QString("WARNING: Free RAM %1% (< 50%). Slowing indexing.")
                    .arg(pct));
            emit pressureWarning();
        }
    } else if (pct > 60) {
        const bool wasHot = m_criticalMode.exchange(false) | m_warningMode.exchange(false);
        if (wasHot) {
            DS_INFO("MemoryMonitor",
                QString("RECOVERED: Free RAM %1%. Resuming normal operation.")
                    .arg(pct));
            emit pressureRecovered();
        }
    }
}

} // namespace DocuSearch
