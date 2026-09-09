#pragma once

// ============================================================
// MemoryMonitor.h - Real-time RAM + CPU pressure monitoring
// ============================================================

#include <QObject>
#include <QTimer>
#include <atomic>
#include <cstdint>

namespace DocuSearch {

class MemoryMonitor : public QObject {
    Q_OBJECT

public:
    explicit MemoryMonitor(QObject* parent = nullptr);
    ~MemoryMonitor() override;

    // The live instance MainWindow owns. Null before construction / after dtor.
    static MemoryMonitor* instance();

    qint64 freeRAM() const { return m_freeRAM.load(); }
    qint64 totalRAM() const { return m_totalRAM.load(); }
    int percentageFree() const { return m_percentFree.load(); }
    int cpuPercent() const { return m_cpuPercent.load(); }

    bool isCritical() const { return m_criticalMode.load(); }
    bool isWarning() const { return m_warningMode.load(); }
    bool isHealthy() const { return !m_warningMode.load() && !m_criticalMode.load(); }

    void startMonitoring();
    void stopMonitoring();

signals:
    void pressureWarning();    // Free RAM < 50%
    void pressureCritical();   // Free RAM < 25%
    void pressureRecovered();  // Free RAM recovered above hysteresis

private slots:
    void checkMemory();

private:
    int sampleCpuPercent();

    static MemoryMonitor* s_instance;

    QTimer* m_timer = nullptr;
    std::atomic<qint64> m_freeRAM{0};
    std::atomic<qint64> m_totalRAM{0};
    std::atomic<int> m_percentFree{100};
    std::atomic<int> m_cpuPercent{0};
    std::atomic<bool> m_criticalMode{false};
    std::atomic<bool> m_warningMode{false};

#ifdef Q_OS_WIN
    quint64 m_prevIdle = 0;
    quint64 m_prevKernel = 0;
    quint64 m_prevUser = 0;
#endif
};

} // namespace DocuSearch
