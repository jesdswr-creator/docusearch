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

    thumbLabel_ = new QLabel(this);
    thumbLabel_->setAlignment(Qt::AlignCenter);
    thumbLabel_->setMinimumSize(300, 220);
    thumbLabel_->setFrameShape(QFrame::StyledPanel);
    thumbLabel_->setText("No preview");
    thumbLabel_->setStyleSheet("color: #888; background: rgba(0,0,0,0.05);");
    v->addWidget(thumbLabel_, 1);

    pathLabel_ = new QLabel(this);
    pathLabel_->setObjectName("subtitleLabel");
    pathLabel_->setWordWrap(true);
    v->addWidget(pathLabel_);

    textEdit_ = new QTextEdit(this);
    textEdit_->setReadOnly(true);
    textEdit_->setPlaceholderText("Extracted text & OCR will appear here.");
    v->addWidget(textEdit_, 2);

    auto* h = new QHBoxLayout();
    openBtn_ = new QPushButton("Open Original", this);
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
        thumbLabel_->setPixmap(pix.scaled(thumbLabel_->size(),
                                          Qt::KeepAspectRatio,
                                          Qt::SmoothTransformation));
    }
}

void PreviewPane::setExtractedText(const QString& text) {
    textEdit_->setPlainText(text);
}

void PreviewPane::setFilePath(const QString& path) {
    pathLabel_->setText(path);
}

void PreviewPane::clear() {
    thumbLabel_->setPixmap(QPixmap());
    thumbLabel_->setText("No preview");
    textEdit_->clear();
    pathLabel_->clear();
}

void PreviewPane::onOpenClicked() {
    if (!pathLabel_->text().isEmpty())
        emit openRequested(pathLabel_->text());
}

} // namespace DocuSearch
