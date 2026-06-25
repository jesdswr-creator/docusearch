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

    // Section header
    auto* section = new QLabel("PREVIEW", this);
    section->setStyleSheet("color: #0078D4; font-size: 11px; font-weight: bold; "
                           "text-transform: uppercase; letter-spacing: 1px;");
    v->addWidget(section);

    // File path label
    pathLabel_ = new QLabel("Select a file to preview", this);
    pathLabel_->setStyleSheet("color: #666; font-size: 12px; padding: 2px;");
    pathLabel_->setWordWrap(true);
    v->addWidget(pathLabel_);

    // Extracted text — main content area (no thumbnail box — saves space)
    textEdit_ = new QTextEdit(this);
    textEdit_->setReadOnly(true);
    textEdit_->setPlaceholderText("Extracted text will appear here after content indexing.");
    textEdit_->setStyleSheet("QTextEdit { background: white; border: 1px solid #d0d0d0; border-radius: 4px; font-size: 13px; }");
    v->addWidget(textEdit_, 1);

    // Open button
    auto* h = new QHBoxLayout();
    openBtn_ = new QPushButton("Open Original", this);
    openBtn_->setStyleSheet("QPushButton { padding: 6px 16px; border-radius: 4px; "
                            "background: #f0f0f0; border: 1px solid #ccc; font-size: 12px; }"
                            "QPushButton:hover { background: #e0e0e0; }");
    h->addStretch();
    h->addWidget(openBtn_);
    v->addLayout(h);

    // thumbLabel_ still created (header declares it) but hidden
    thumbLabel_ = new QLabel(this);
    thumbLabel_->setVisible(false);

    connect(openBtn_, &QPushButton::clicked, this, &PreviewPane::onOpenClicked);
}

void PreviewPane::setThumbnail(const QPixmap& pix) {
    Q_UNUSED(pix);
    // Thumbnails disabled — preview shows text only
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

} // namespace DocuSearch
