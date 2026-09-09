#pragma once

// ============================================================
// SystemHealthDashboard.h - Real-time performance metrics
// ============================================================
// Shown in status bar or settings:
// - RAM usage (live graph)
// - CPU usage (per-tier baseline)
// - Indexing speed (files/min)
// - Search latency (P99)
// - Semantic search quality (result count, cache hit rate)
// - System tier & current degradation level
// ============================================================

#include <QWidget>
#include <QString>
#include <memory>
#include <QTimer>

namespace DocuSearch {

struct HealthMetrics {
    int ramUsagePercent;
    int cpuUsagePercent;
    int indexingSpeedFilesPerMin;
    int searchLatencyMs;
    int semanticCacheHitRate;
    QString currentDegradationLevel;
};

class SystemHealthDashboard : public QWidget {
    Q_OBJECT

public:
    explicit SystemHealthDashboard(QWidget* parent = nullptr);
    ~SystemHealthDashboard() override;

    void updateMetrics(const HealthMetrics& m);
    void startMonitoring();
    void stopMonitoring();

private slots:
    void onUpdateTick();

private:
    void buildUI();

    QTimer* updateTimer_ = nullptr;
    class QLabel* ramLabel_ = nullptr;
    class QLabel* cpuLabel_ = nullptr;
    class QLabel* indexSpeedLabel_ = nullptr;
    class QLabel* searchLatencyLabel_ = nullptr;
    class QLabel* semanticCacheLabel_ = nullptr;
    class QLabel* degradationLabel_ = nullptr;
};

} // namespace DocuSearch
