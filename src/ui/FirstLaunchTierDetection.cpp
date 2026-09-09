// ============================================================
// FirstLaunchTierDetection.cpp
// ============================================================

#include "FirstLaunchTierDetection.h"
#include "../core/Logger.h"
#include "../core/TierConfig.h"
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFont>
#include <QString>

namespace DocuSearch {

FirstLaunchTierDetection::FirstLaunchTierDetection(const SystemProfile& profile, QWidget* parent)
    : QDialog(parent), profile_(profile)
{
    setWindowTitle("DocuSearch — First Launch Setup");
    setMinimumWidth(600);
    setMinimumHeight(500);
    buildUI();
    populateTierInfo();
}

FirstLaunchTierDetection::~FirstLaunchTierDetection() = default;

void FirstLaunchTierDetection::buildUI() {
    auto* layout = new QVBoxLayout(this);

    // Tier badge
    tierBadge_ = new QLabel(this);
    QFont boldFont = tierBadge_->font();
    boldFont.setBold(true);
    boldFont.setPointSize(16);
    tierBadge_->setFont(boldFont);
    layout->addWidget(tierBadge_);

    // Tier title
    tierTitle_ = new QLabel(this);
    QFont titleFont = tierTitle_->font();
    titleFont.setPointSize(14);
    tierTitle_->setFont(titleFont);
    layout->addWidget(tierTitle_);

    layout->addSpacing(10);

    // Tier description
    tierDescription_ = new QTextEdit(this);
    tierDescription_->setReadOnly(true);
    tierDescription_->setMaximumHeight(80);
    layout->addWidget(tierDescription_);

    layout->addSpacing(10);

    // Expected performance
    layout->addWidget(new QLabel("<b>Expected Performance:</b>", this));
    expectedPerformance_ = new QLabel(this);
    expectedPerformance_->setWordWrap(true);
    layout->addWidget(expectedPerformance_);

    layout->addSpacing(10);

    // Features available
    layout->addWidget(new QLabel("<b>Features Enabled:</b>", this));
    featuresAvailable_ = new QLabel(this);
    featuresAvailable_->setWordWrap(true);
    layout->addWidget(featuresAvailable_);

    layout->addSpacing(10);

    // Setup recommendations
    layout->addWidget(new QLabel("<b>Setup Recommendations:</b>", this));
    setupRecommendations_ = new QLabel(this);
    setupRecommendations_->setWordWrap(true);
    layout->addWidget(setupRecommendations_);

    layout->addStretch();

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    continueBtn_ = new QPushButton(QStringLiteral("Continue"), this);
    addFolderBtn_ = new QPushButton(QStringLiteral("Add my first folder"), this);
    addFolderBtn_->setDefault(true);
    connect(continueBtn_, &QPushButton::clicked, this, &QDialog::accept);
    connect(addFolderBtn_, &QPushButton::clicked, this, [this]() {
        addFolderChosen_ = true;
        emit addFolderRequested();
        accept();
    });
    btnLayout->addWidget(continueBtn_);
    btnLayout->addWidget(addFolderBtn_);
    layout->addLayout(btnLayout);
}

void FirstLaunchTierDetection::populateTierInfo() {
    QString tierStr = SystemProfiler::tierName(profile_.tier);
    tierBadge_->setText(tierStr);

    if (profile_.tier == SystemTier::LowEnd) {
        tierTitle_->setText("Low-End System Detected (2–4 GB RAM)");
        tierDescription_->setText(
            "Your system has " + QString::number(profile_.totalRAM / (1LL << 30)) + " GB RAM with " +
            QString::number(profile_.cpuCores) + " CPU cores. DocuSearch will optimize for efficiency.");
        expectedPerformance_->setText(
            "• Index 1,000 files: ~2 minutes\n"
            "• Keyword search: <500ms\n"
            "• Semantic search: <1s (after initial embedding)\n"
            "• Peak memory: <150 MB");
        featuresAvailable_->setText(
            "✓ Keyword search (instant)\n"
            "✓ AI semantic search (progressive, throttled)\n"
            "✓ OCR (single-threaded)\n"
            "✓ File monitoring (manual scan)");
        setupRecommendations_->setText(
            "1. Start with <5,000 documents for initial indexing\n"
            "2. Semantic search will enable progressively as embeddings build\n"
            "3. Run indexing during idle time\n"
            "4. All features work, just at a comfortable pace");
    } else if (profile_.tier == SystemTier::MidRange) {
        tierTitle_->setText("Mid-Range System Detected (8–16 GB RAM)");
        tierDescription_->setText(
            "Your system has " + QString::number(profile_.totalRAM / (1LL << 30)) + " GB RAM with " +
            QString::number(profile_.cpuCores) + " CPU cores. DocuSearch will operate at normal speed.");
        expectedPerformance_->setText(
            "• Index 1,000 files: ~30 seconds\n"
            "• Keyword search: <100ms\n"
            "• Semantic search: <1s (real-time)\n"
            "• Peak memory: <500 MB");
        featuresAvailable_->setText(
            "✓ Keyword search (instant)\n"
            "✓ AI semantic search (full-featured)\n"
            "✓ OCR (multi-threaded)\n"
            "✓ Live file monitoring\n"
            "✓ Thumbnail caching");
        setupRecommendations_->setText(
            "1. Optimal range: 50,000–500,000 documents\n"
            "2. Semantic search works smoothly in real-time\n"
            "3. All background features can run simultaneously\n"
            "4. Consider enabling auto-scan for live updates");
    } else {
        tierTitle_->setText("High-End System Detected (32GB+ RAM)");
        tierDescription_->setText(
            "Your system has " + QString::number(profile_.totalRAM / (1LL << 30)) + " GB RAM with " +
            QString::number(profile_.cpuCores) + " CPU cores. DocuSearch will operate at maximum speed.");
        expectedPerformance_->setText(
            "• Index 1,000 files: <10 seconds\n"
            "• Keyword search: <50ms\n"
            "• Semantic search: <200ms\n"
            "• Peak memory: <2 GB");
        featuresAvailable_->setText(
            "✓ Keyword search (instant)\n"
            "✓ AI semantic search (aggressive parallel)\n"
            "✓ OCR (4+ parallel workers)\n"
            "✓ Live file monitoring\n"
            "✓ Aggressive thumbnail & result caching\n"
            "✓ All features at maximum parallelism");
        setupRecommendations_->setText(
            "1. No limits: works smoothly with 1M+ documents\n"
            "2. Semantic search is instantaneous\n"
            "3. Enable all features — plenty of resources\n"
            "4. OCR and indexing can run without performance impact");
    }
}

} // namespace DocuSearch
