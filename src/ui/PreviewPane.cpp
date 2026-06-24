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
    v->setSpacing(8);

    auto* section = new QLabel("PREVIEW", this);
    section->setObjectName("sectionLabel");
    v->addWidget(section);

    // Thumbnail — smaller minimum (200x150), capped at 200 tall.
    thumbLabel_ = new QLabel(this);
    thumbLabel_->setAlignment(Qt::AlignCenter);
    thumbLabel_->setMinimumSize(200, 150);
    thumbLabel_->setMaximumHeight(200);
    thumbLabel_->setFrameShape(QFrame::StyledPanel);
    thumbLabel_->setText("No preview");
    thumbLabel_->setStyleSheet(
        "QLabel {"
        "  color: #888;"
        "  background: rgba(0,0,0,0.05);"
        "  border: 1px solid #D0D0D0;"
        "  border-radius: 8px;"
        "}");
    v->addWidget(thumbLabel_);

    // Path label — default placeholder text.
    pathLabel_ = new QLabel("Select a file to preview", this);
    pathLabel_->setObjectName("subtitleLabel");
    pathLabel_->setWordWrap(true);
    pathLabel_->setStyleSheet(
        "QLabel {"
        "  color: #606060;"
        "  padding: 4px;"
        "  border-radius: 4px;"
        "}");
    v->addWidget(pathLabel_);

    // Text edit — gets the most space (stretch=1).
    textEdit_ = new QTextEdit(this);
    textEdit_->setReadOnly(true);
    textEdit_->setPlaceholderText("Extracted text & OCR will appear here.");
    textEdit_->setStyleSheet(
        "QTextEdit {"
        "  border: 1px solid #D0D0D0;"
        "  border-radius: 6px;"
        "  padding: 4px;"
        "  background: #FFFFFF;"
        "}");
    v->addWidget(textEdit_, 1);

    auto* h = new QHBoxLayout();
    openBtn_ = new QPushButton("Open Original", this);
    openBtn_->setStyleSheet(
        "QPushButton {"
        "  padding: 6px 14px;"
        "  border: 1px solid #B0B0B0;"
        "  border-radius: 6px;"
        "  background: #F5F5F5;"
        "}"
        "QPushButton:hover { background: #E5E5E5; }"
        "QPushButton:pressed { background: #D5D5D5; }");
    h->addStretch();
    h->addWidget(openBtn_);
    v->addLayout(h);

    connect(openBtn_, &QPushButton::clicked, this, &PreviewPane::onOpenClicked);
}

void PreviewPane::setThumbnail(const QPixmap& pix) {
    if (pix.isNull()) {
        thumbLabel_->setText("No preview");
        thumbLabel_->setPixmap(QPixmap());
    } else {
        thumbLabel_->setText("");
        // Scale to fit the label's current size, capped at max height.
        QSize sz = thumbLabel_->size();
        if (sz.height() > 200) sz.setHeight(200);
        thumbLabel_->setPixmap(pix.scaled(sz,
                                          Qt::KeepAspectRatio,
                                          Qt::SmoothTransformation));
    }
}

void PreviewPane::setExtractedText(const QString& text) {
    textEdit_->setPlainText(text);
}

void PreviewPane::setFilePath(const QString& path) {
    pathLabel_->setText(path.isEmpty() ? "Select a file to preview" : path);
}

void PreviewPane::clear() {
    thumbLabel_->setPixmap(QPixmap());
    thumbLabel_->setText("No preview");
    textEdit_->clear();
    pathLabel_->setText("Select a file to preview");
}

void PreviewPane::onOpenClicked() {
    const QString p = pathLabel_->text();
    if (!p.isEmpty() && p != "Select a file to preview")
        emit openRequested(p);
}

} // namespace DocuSearch
