// ============================================================
// SystemHealthDashboard.cpp
// ============================================================

#include "SystemHealthDashboard.h"
#include <QLabel>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QTimer>
#include <QFrame>
#include <utility>

namespace DocuSearch {

SystemHealthDashboard::SystemHealthDashboard(QWidget* parent)
    : QWidget(parent)
{
    buildUI();
    updateTimer_ = new QTimer(this);
    updateTimer_->setTimerType(Qt::CoarseTimer);
    connect(updateTimer_, &QTimer::timeout, this, &SystemHealthDashboard::onUpdateTick);
}

SystemHealthDashboard::~SystemHealthDashboard() = default;

void SystemHealthDashboard::setMetricsProvider(MetricsProvider fn) {
    provider_ = std::move(fn);
}

void SystemHealthDashboard::buildUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(10);

    auto* box = new QGroupBox(QStringLiteral("Live system health"), this);
    auto* grid = new QGridLayout(box);
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(6);

    auto make = [box](const QString& caption) {
        auto* cap = new QLabel(caption, box);
        cap->setStyleSheet(QStringLiteral("color:#667188; font-size:11px;"));
        auto* val = new QLabel(QStringLiteral("—"), box);
        val->setStyleSheet(QStringLiteral("font-weight:700; font-size:13px;"));
        return std::pair<QLabel*, QLabel*>{cap, val};
    };

    int row = 0;
    auto add = [&](QLabel*& store, const QString& caption) {
        auto [cap, val] = make(caption);
        grid->addWidget(cap, row, 0);
        grid->addWidget(val, row, 1);
        store = val;
        ++row;
    };

    add(ramLabel_, QStringLiteral("Free RAM"));
    add(cpuLabel_, QStringLiteral("CPU"));
    add(indexSpeedLabel_, QStringLiteral("Extraction"));
    add(searchLatencyLabel_, QStringLiteral("Last search"));
    add(semanticCacheLabel_, QStringLiteral("AI coverage"));
    add(degradationLabel_, QStringLiteral("Pressure"));
    add(tierLabel_, QStringLiteral("System tier"));

    layout->addWidget(box);
}

void SystemHealthDashboard::updateMetrics(const HealthMetrics& m) {
    ramLabel_->setText(QStringLiteral("%1% free").arg(m.ramFreePercent));
    cpuLabel_->setText(QStringLiteral("%1%").arg(m.cpuUsagePercent));
    indexSpeedLabel_->setText(QStringLiteral("%1 files/min").arg(m.indexingSpeedFilesPerMin));
    searchLatencyLabel_->setText(QStringLiteral("%1 ms").arg(m.searchLatencyMs));
    semanticCacheLabel_->setText(QStringLiteral("%1% embedded").arg(m.semanticCoveragePercent));

    const QString status = m.currentDegradationLevel.isEmpty()
        ? QStringLiteral("Healthy") : m.currentDegradationLevel;
    QString color = QStringLiteral("#059669");
    if (status == QLatin1String("Emergency"))      color = QStringLiteral("#dc2626");
    else if (status == QLatin1String("Critical"))  color = QStringLiteral("#ea580c");
    else if (status == QLatin1String("Warning"))   color = QStringLiteral("#ca8a04");
    degradationLabel_->setStyleSheet(
        QStringLiteral("font-weight:700; font-size:13px; color:%1;").arg(color));
    degradationLabel_->setText(status);

    if (tierLabel_)
        tierLabel_->setText(m.tierName.isEmpty() ? QStringLiteral("—") : m.tierName);
}

void SystemHealthDashboard::startMonitoring() {
    updateTimer_->start(2000);
    onUpdateTick();
}

void SystemHealthDashboard::stopMonitoring() {
    updateTimer_->stop();
}

void SystemHealthDashboard::onUpdateTick() {
    if (provider_) updateMetrics(provider_());
}

} // namespace DocuSearch
