// ============================================================
// IndexingProgressWidget.cpp
// ============================================================

#include "IndexingProgress.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>

namespace DocuSearch {

IndexingProgressWidget::IndexingProgressWidget(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);

    phaseLabel_ = new QLabel("Idle", this);
    phaseLabel_->setObjectName("titleLabel");
    outer->addWidget(phaseLabel_);

    auto* gb = new QGroupBox("Indexing Status", this);
    auto* form = new QFormLayout(gb);

    scannedLabel_ = new QLabel("0", this);
    indexedLabel_ = new QLabel("0", this);
    ocrLabel_     = new QLabel("0", this);
    queueLabel_   = new QLabel("0", this);
    errorsLabel_  = new QLabel("0", this);
    etaLabel_     = new QLabel("—", this);

    form->addRow("Files scanned",     scannedLabel_);
    form->addRow("Documents indexed", indexedLabel_);
    form->addRow("OCR completed",     ocrLabel_);
    form->addRow("Queue remaining",   queueLabel_);
    form->addRow("Errors",            errorsLabel_);
    form->addRow("Est. time left",    etaLabel_);

    cpuBar_ = new QProgressBar(this);
    cpuBar_->setRange(0, 100);
    cpuBar_->setFormat("CPU %p%");
    form->addRow("CPU usage", cpuBar_);

    overallBar_ = new QProgressBar(this);
    overallBar_->setRange(0, 100);
    overallBar_->setFormat("%p%");
    form->addRow("Overall", overallBar_);

    outer->addWidget(gb);

    auto* h = new QHBoxLayout();
    h->addStretch();
    pauseBtn_ = new QPushButton("Pause", this);
    stopBtn_  = new QPushButton("Stop",  this);
    h->addWidget(pauseBtn_);
    h->addWidget(stopBtn_);
    outer->addLayout(h);

    connect(pauseBtn_, &QPushButton::clicked, this, [this]{
        paused_ = !paused_;
        if (paused_) {
            pauseBtn_->setText("Resume");
            emit pauseRequested();
        } else {
            pauseBtn_->setText("Pause");
            emit resumeRequested();
        }
    });
    connect(stopBtn_, &QPushButton::clicked, this, &IndexingProgressWidget::stopRequested);
}

void IndexingProgressWidget::setPhase(const QString& phase) {
    phaseLabel_->setText(phase);
}

void IndexingProgressWidget::update(const DocuSearch::IndexingProgress& p) {
    scannedLabel_->setText(QString::number(p.filesScanned.load()));
    indexedLabel_->setText(QString::number(p.documentsIndexed.load()));
    ocrLabel_->    setText(QString::number(p.ocrCompleted.load()));
    queueLabel_->  setText(QString::number(p.queueRemaining.load()));
    errorsLabel_-> setText(QString::number(p.errorsCount.load()));
    cpuBar_->setValue(p.cpuUsagePct.load());

    // ETA estimation: very rough
    if (p.queueRemaining.load() > 0 && p.documentsIndexed.load() > 0) {
        // No throughput tracking here — leave ETA at "—"
        etaLabel_->setText("estimating…");
    } else {
        etaLabel_->setText("—");
    }

    if (p.paused.load()) {
        phaseLabel_->setText(phaseLabel_->text() + "  (paused)");
    }
}

} // namespace DocuSearch
