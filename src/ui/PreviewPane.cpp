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
    v->setContentsMargins(12, 12, 12, 12);
    v->setSpacing(8);

    // Section header - modern uppercase label.
    auto* section = new QLabel("PREVIEW", this);
    section->setObjectName("sectionLabel");
    section->setStyleSheet(
        "QLabel { color: #0078D4; font-size: 11px; font-weight: bold; "
        "  text-transform: uppercase; letter-spacing: 1.5px; "
        "  background: transparent; border: none; }");
    v->addWidget(section);

    // File path label.
    pathLabel_ = new QLabel("Select a file to preview", this);
    pathLabel_->setStyleSheet(
        "QLabel { font-size: 12px; padding: 4px 2px; "
        "  color: #606060; background: transparent; border: none; }");
    pathLabel_->setWordWrap(true);
    v->addWidget(pathLabel_);

    // Extracted text - main content area with comfortable margins.
    textEdit_ = new QTextEdit(this);
    textEdit_->setReadOnly(true);
    textEdit_->setPlaceholderText("Extracted text will appear here after content indexing.");
    textEdit_->setStyleSheet(
        "QTextEdit { "
        "  background-color: #ffffff; "
        "  border: 1px solid #e0e0e0; "
        "  border-radius: 8px; "
        "  font-size: 13px; "
        "  padding: 12px; "
        "  line-height: 1.5; "
        "} ");
    v->addWidget(textEdit_, 1);

    // Open button - modern accent button.
    auto* h = new QHBoxLayout();
    h->setContentsMargins(0, 4, 0, 0);
    openBtn_ = new QPushButton("Open Original", this);
    openBtn_->setCursor(Qt::PointingHandCursor);
    openBtn_->setStyleSheet(
        "QPushButton { "
        "  padding: 8px 20px; "
        "  border-radius: 8px; "
        "  background-color: #0078D4; "
        "  color: #ffffff; "
        "  border: none; "
        "  font-size: 12px; "
        "  font-weight: 500; "
        "} "
        "QPushButton:hover { background-color: #0067C0; } "
        "QPushButton:pressed { background-color: #0054A6; }");
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
