// ============================================================
// PreviewPane.cpp
// ============================================================

#include "PreviewPane.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>

namespace DocuSearch {

PreviewPane::PreviewPane(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(8, 8, 8, 8);
    v->setSpacing(6);

    // Section header - uses palette so it adapts to dark/light mode.
    auto* section = new QLabel("PREVIEW", this);
    section->setObjectName("sectionLabel");
    v->addWidget(section);

    // File path label - no hardcoded color, uses palette.
    pathLabel_ = new QLabel("Select a file to preview", this);
    pathLabel_->setStyleSheet("font-size: 12px; padding: 2px; background: transparent; border: none;");
    pathLabel_->setWordWrap(true);
    v->addWidget(pathLabel_);

    // Extracted text - no hardcoded background, uses palette via Theme QSS.
    textEdit_ = new QTextEdit(this);
    textEdit_->setReadOnly(true);
    textEdit_->setPlaceholderText("Extracted text will appear here after content indexing.");
    v->addWidget(textEdit_, 1);

    // Buttons row: OCR + Open Original.
    auto* h = new QHBoxLayout();
    h->setSpacing(6);
    ocrBtn_ = new QPushButton("OCR This File", this);
    ocrBtn_->setCursor(Qt::PointingHandCursor);
    ocrBtn_->setToolTip("Run Windows OCR on this file (for scanned PDFs and images)");
    ocrBtn_->setDefault(true);
    openBtn_ = new QPushButton("Open Original", this);
    openBtn_->setCursor(Qt::PointingHandCursor);
    h->addStretch();
    h->addWidget(ocrBtn_);
    h->addWidget(openBtn_);
    v->addLayout(h);

    // thumbLabel_ still created (header declares it) but hidden
    thumbLabel_ = new QLabel(this);
    thumbLabel_->setVisible(false);

    connect(openBtn_, &QPushButton::clicked, this, &PreviewPane::onOpenClicked);
    connect(ocrBtn_,  &QPushButton::clicked, this, &PreviewPane::onOcrClicked);
}

void PreviewPane::setThumbnail(const QPixmap& pix) {
    Q_UNUSED(pix);
    // Thumbnails disabled - preview shows text only
}

void PreviewPane::setExtractedText(const QString& text) {
    textEdit_->setPlainText(text);
}

void PreviewPane::setFilePath(const QString& path) {
    pathLabel_->setText(path.isEmpty() ? "Select a file to preview" : path);
}

void PreviewPane::clear() {
    textEdit_->clear();
    pathLabel_->setText("Select a file to preview");
}

void PreviewPane::onOpenClicked() {
    const QString p = pathLabel_->text();
    if (!p.isEmpty() && p != "Select a file to preview")
        emit openRequested(p);
}

void PreviewPane::onOcrClicked() {
    const QString p = pathLabel_->text();
    if (!p.isEmpty() && p != "Select a file to preview")
        emit ocrRequested(p);
}

} // namespace DocuSearch
