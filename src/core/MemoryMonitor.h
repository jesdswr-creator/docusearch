#pragma once

// ============================================================
// MemoryMonitor.h - Real-time RAM pressure monitoring
// ============================================================
// Watches free RAM and throttles indexing/search when < 25%
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
    ~MemoryMonitor();
    
    // Get current RAM status
    qint64 freeRAM() const { return m_freeRAM; }
    int percentageFree() const { return m_percentFree; }
    
    // Throttling hints
    bool isCritical() const { return m_criticalMode; }
    bool isWarning() const { return m_warningMode; }
    bool isHealthy() const { return !m_warningMode; }
    
    // Pause/resume operations under pressure
    void startMonitoring();
    void stopMonitoring();
    
signals:
    void pressureWarning();    // Free RAM < 50%
    void pressureCritical();   // Free RAM < 25%
    void pressureRecovered();  // Free RAM > 75%
    
private slots:
    void checkMemory();
    
private:
    QTimer* m_timer = nullptr;
    qint64 m_freeRAM = 0;
    int m_percentFree = 100;
    std::atomic<bool> m_criticalMode{false};
    std::atomic<bool> m_warningMode{false};
};

} // namespace DocuSearch
