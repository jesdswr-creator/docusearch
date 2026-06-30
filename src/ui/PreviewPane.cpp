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

    // Section header - use objectName so Theme QSS can style it.
    auto* section = new QLabel("PREVIEW", this);
    section->setObjectName("sectionLabel");
    v->addWidget(section);

    // File path label - no hardcoded color so Theme QSS can style it
    // for dark mode. Previously had 'color: #666' which was unreadable
    // on the dark background.
    pathLabel_ = new QLabel("Select a file to preview", this);
    pathLabel_->setStyleSheet("font-size: 12px; padding: 2px;");
    pathLabel_->setWordWrap(true);
    v->addWidget(pathLabel_);

    // Extracted text - main content area (no thumbnail box - saves space).
    // NO inline stylesheet - let the Theme QSS handle colors so dark mode
    // works correctly. Previously had hardcoded 'background: white' which
    // left the preview pane white in dark mode.
    textEdit_ = new QTextEdit(this);
    textEdit_->setReadOnly(true);
    textEdit_->setPlaceholderText("Extracted text will appear here after content indexing.");
    v->addWidget(textEdit_, 1);

    // Open button - no inline stylesheet, let Theme QSS style it.
    // Previously had hardcoded 'background: #f0f0f0' which left the button
    // white in dark mode.
    auto* h = new QHBoxLayout();
    openBtn_ = new QPushButton("Open Original", this);
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

} // namespace DocuSearch
