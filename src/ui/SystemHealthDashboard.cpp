// ============================================================
// SystemHealthDashboard.cpp
// ============================================================

#include "SystemHealthDashboard.h"
#include <QLabel>
#include <QVBoxLayout>
#include <QTimer>
#include <QColor>

namespace DocuSearch {

SystemHealthDashboard::SystemHealthDashboard(QWidget* parent)
    : QWidget(parent)
{
    buildUI();
    updateTimer_ = new QTimer(this);
    connect(updateTimer_, &QTimer::timeout, this, &SystemHealthDashboard::onUpdateTick);
}

SystemHealthDashboard::~SystemHealthDashboard() = default;

void SystemHealthDashboard::buildUI() {
    auto* layout = new QVBoxLayout(this);

    ramLabel_ = new QLabel("RAM: 0% ", this);
    cpuLabel_ = new QLabel("CPU: 0% ", this);
    indexSpeedLabel_ = new QLabel("Indexing: 0 files/min", this);
    searchLatencyLabel_ = new QLabel("Search latency: 0ms", this);
    semanticCacheLabel_ = new QLabel("Semantic cache: 0%", this);
    degradationLabel_ = new QLabel("Status: Healthy", this);

    layout->addWidget(ramLabel_);
    layout->addWidget(cpuLabel_);
    layout->addWidget(indexSpeedLabel_);
    layout->addWidget(searchLatencyLabel_);
    layout->addWidget(semanticCacheLabel_);
    layout->addWidget(degradationLabel_);
}

void SystemHealthDashboard::updateMetrics(const HealthMetrics& m) {
    ramLabel_->setText(QString("RAM: %1% ").arg(m.ramUsagePercent));
    cpuLabel_->setText(QString("CPU: %1% ").arg(m.cpuUsagePercent));
    indexSpeedLabel_->setText(QString("Indexing: %1 files/min").arg(m.indexingSpeedFilesPerMin));
    searchLatencyLabel_->setText(QString("Search latency: %1ms (P99)").arg(m.searchLatencyMs));
    semanticCacheLabel_->setText(QString("Semantic cache: %1%").arg(m.semanticCacheHitRate));

    // Color-code degradation level
    QString statusText = QString("Status: %1").arg(m.currentDegradationLevel);
    if (m.currentDegradationLevel == "Emergency") {
        degradationLabel_->setStyleSheet("color: red; font-weight: bold;");
    } else if (m.currentDegradationLevel == "Critical") {
        degradationLabel_->setStyleSheet("color: orange; font-weight: bold;");
    } else if (m.currentDegradationLevel == "Warning") {
        degradationLabel_->setStyleSheet("color: gold;");
    } else {
        degradationLabel_->setStyleSheet("color: green;");
    }
    degradationLabel_->setText(statusText);
}

void SystemHealthDashboard::startMonitoring() {
    updateTimer_->start(5000);  // Update every 5 seconds
}

void SystemHealthDashboard::stopMonitoring() {
    updateTimer_->stop();
}

void SystemHealthDashboard::onUpdateTick() {
    // Called by derived classes or external monitors
}

} // namespace DocuSearch
