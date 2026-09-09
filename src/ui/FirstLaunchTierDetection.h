#pragma once

// ============================================================
// FirstLaunchTierDetection.h - Welcome dialog with tier info
// ============================================================
// Shows on first launch:
// - Detected system tier (Low/Mid/High-End)
// - Expected performance metrics
// - Feature availability
// - Setup recommendations
// ============================================================

#include <QDialog>
#include <QString>
#include <memory>

#include "../core/SystemProfile.h"

class QLabel;
class QPushButton;
class QTextEdit;

namespace DocuSearch {

class FirstLaunchTierDetection : public QDialog {
    Q_OBJECT

public:
    explicit FirstLaunchTierDetection(const SystemProfile& profile, QWidget* parent = nullptr);
    ~FirstLaunchTierDetection() override;

    bool addFolderChosen() const { return addFolderChosen_; }

signals:
    void addFolderRequested();

private:
    void buildUI();
    void populateTierInfo();

    SystemProfile profile_;
    QLabel* tierBadge_ = nullptr;
    QLabel* tierTitle_ = nullptr;
    QTextEdit* tierDescription_ = nullptr;
    QLabel* expectedPerformance_ = nullptr;
    QLabel* featuresAvailable_ = nullptr;
    QLabel* setupRecommendations_ = nullptr;
    QPushButton* continueBtn_ = nullptr;
    QPushButton* addFolderBtn_ = nullptr;
    bool addFolderChosen_ = false;
};

} // namespace DocuSearch
