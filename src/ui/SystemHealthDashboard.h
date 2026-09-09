#pragma once

// ============================================================
// SystemHealthDashboard.h - Real-time performance metrics
// ============================================================

#include <QWidget>
#include <QString>
#include <functional>
#include <QTimer>

// Forward-declare ::QLabel at GLOBAL scope, BEFORE the namespace opens.
// NEVER write `class QLabel* member;` (elaborated-type-specifier) inside
// namespace DocuSearch: with no visible declaration the compiler treats it
// as a forward declaration of a PHANTOM DocuSearch::QLabel, which then
// shadows the real ::QLabel inside every namespace-DocuSearch TU that
// includes this header - even after `#include <QLabel>` - and the build
// dies with C2027 "use of undefined type 'DocuSearch::QLabel'" in every
// file that merely mentions QLabel (v1.7.22 CI failure, 100+ errors).
class QLabel;

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
    QLabel* ramLabel_ = nullptr;
    QLabel* cpuLabel_ = nullptr;
    QLabel* indexSpeedLabel_ = nullptr;
    QLabel* searchLatencyLabel_ = nullptr;
    QLabel* semanticCacheLabel_ = nullptr;
    QLabel* degradationLabel_ = nullptr;
    QLabel* tierLabel_ = nullptr;
};

} // namespace DocuSearch
