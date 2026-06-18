#pragma once

#include <QWidget>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include "../core/Types.h"

namespace DocuSearch {

class IndexingProgressWidget : public QWidget {
    Q_OBJECT
public:
    explicit IndexingProgressWidget(QWidget* parent = nullptr);

    void update(const DocuSearch::IndexingProgress& p);  // struct from core/Types.h
    void setPhase(const QString& phase);

signals:
    void pauseRequested();
    void resumeRequested();
    void stopRequested();

private:
    QLabel*       phaseLabel_;
    QLabel*       scannedLabel_;
    QLabel*       indexedLabel_;
    QLabel*       ocrLabel_;
    QLabel*       queueLabel_;
    QLabel*       errorsLabel_;
    QLabel*       etaLabel_;
    QProgressBar* cpuBar_;
    QProgressBar* overallBar_;
    QPushButton*  pauseBtn_;
    QPushButton*  stopBtn_;
    bool          paused_ = false;
};

} // namespace DocuSearch
