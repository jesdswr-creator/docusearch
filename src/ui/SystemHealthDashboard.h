#pragma once

// ============================================================
// SystemHealthDashboard.h - Real-time performance metrics
// ============================================================

#include <QWidget>
#include <QString>
#include <functional>
#include <QTimer>

namespace DocuSearch {

struct HealthMetrics {
    int ramFreePercent = 100;
    int cpuUsagePercent = 0;
    int indexingSpeedFilesPerMin = 0;
    int searchLatencyMs = 0;
    int semanticCoveragePercent = 0;
    QString currentDegradationLevel;
    QString tierName;
};

class SystemHealthDashboard : public QWidget {
    Q_OBJECT

public:
    using MetricsProvider = std::function<HealthMetrics()>;

    explicit SystemHealthDashboard(QWidget* parent = nullptr);
    ~SystemHealthDashboard() override;

    void setMetricsProvider(MetricsProvider fn);
    void updateMetrics(const HealthMetrics& m);
    void startMonitoring();
    void stopMonitoring();

private slots:
    void onUpdateTick();

private:
    void buildUI();

    MetricsProvider provider_;
    QTimer* updateTimer_ = nullptr;
    class QLabel* ramLabel_ = nullptr;
    class QLabel* cpuLabel_ = nullptr;
    class QLabel* indexSpeedLabel_ = nullptr;
    class QLabel* searchLatencyLabel_ = nullptr;
    class QLabel* semanticCacheLabel_ = nullptr;
    class QLabel* degradationLabel_ = nullptr;
    class QLabel* tierLabel_ = nullptr;
};

} // namespace DocuSearch
