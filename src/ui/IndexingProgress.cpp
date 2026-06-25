// ============================================================
// IndexingProgressWidget.cpp
// ============================================================

#include "IndexingProgress.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QFont>
#include <QFrame>

namespace DocuSearch {

namespace {
void applyLabelFont(QLabel* lbl) {
    QFont f = lbl->font();
    f.setFamily("Segoe UI");
    f.setPointSize(12);
    lbl->setFont(f);
}
}

IndexingProgressWidget::IndexingProgressWidget(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    // Wrap content in a QScrollArea so labels don't crush when small.
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* inner = new QWidget(scroll);
    auto* v = new QVBoxLayout(inner);
    v->setContentsMargins(8, 8, 8, 8);
    v->setSpacing(6);

    // Section header — blue.
    phaseLabel_ = new QLabel("Idle", inner);
    phaseLabel_->setObjectName("titleLabel");
    QFont headerFont("Segoe UI", 11);
    headerFont.setBold(true);
    phaseLabel_->setFont(headerFont);
    phaseLabel_->setStyleSheet(
        "QLabel {"
        "  color: #FFFFFF;"
        "  background: #0078D4;"
        "  padding: 6px 10px;"
        "  border-radius: 4px;"
        "}");
    v->addWidget(phaseLabel_);

    auto* gb = new QGroupBox("Indexing Status", inner);
    auto* form = new QFormLayout(gb);
    form->setLabelAlignment(Qt::AlignRight);

    scannedLabel_ = new QLabel("0", inner);
    indexedLabel_ = new QLabel("0", inner);
    ocrLabel_     = new QLabel("0", inner);
    queueLabel_   = new QLabel("0", inner);
    errorsLabel_  = new QLabel("0", inner);
    etaLabel_     = new QLabel("—", inner);

    applyLabelFont(scannedLabel_);
    applyLabelFont(indexedLabel_);
    applyLabelFont(ocrLabel_);
    applyLabelFont(queueLabel_);
    applyLabelFont(errorsLabel_);
    applyLabelFont(etaLabel_);

    form->addRow("Files scanned",     scannedLabel_);
    form->addRow("Documents indexed", indexedLabel_);
    form->addRow("OCR completed",     ocrLabel_);
    form->addRow("Queue remaining",   queueLabel_);
    form->addRow("Errors",            errorsLabel_);
    form->addRow("Est. time left",    etaLabel_);

    cpuBar_ = new QProgressBar(inner);
    cpuBar_->setRange(0, 100);
    cpuBar_->setFormat("CPU %p%");
    form->addRow("CPU usage", cpuBar_);

    overallBar_ = new QProgressBar(inner);
    overallBar_->setRange(0, 100);
    overallBar_->setFormat("%p%");
    form->addRow("Overall", overallBar_);

    v->addWidget(gb);

    // Pause/Stop buttons — hidden because the indexer subsystem is disabled.
    auto* h = new QHBoxLayout();
    h->addStretch();
    pauseBtn_ = new QPushButton("Pause", inner);
    stopBtn_  = new QPushButton("Stop",  inner);
    pauseBtn_->setVisible(false);
    stopBtn_->setVisible(false);
    h->addWidget(pauseBtn_);
    h->addWidget(stopBtn_);
    v->addLayout(h);

    // Still wire the buttons in case they're toggled visible later.
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

    v->addStretch();

    scroll->setWidget(inner);
    outer->addWidget(scroll);
}

void IndexingProgressWidget::setPhase(const QString& phase) {
    // phase may contain newlines (from updateIndexStats) — preserve them.
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
        etaLabel_->setText("estimating…");
    } else {
        etaLabel_->setText("—");
    }

    if (p.paused.load()) {
        phaseLabel_->setText(phaseLabel_->text() + "  (paused)");
    }
}

} // namespace DocuSearch
