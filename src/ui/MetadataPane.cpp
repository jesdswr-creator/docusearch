// ============================================================
// MetadataPane.cpp — Compact, scrollable, small font
// ============================================================

#include "MetadataPane.h"
#include "../core/StringUtils.h"
#include "../core/Constants.h"

#include <QFormLayout>
#include <QScrollArea>
#include <QGroupBox>
#include <QFont>

namespace DocuSearch {

MetadataPane::MetadataPane(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* content = new QWidget();
    auto* form = new QFormLayout(content);
    form->setLabelAlignment(Qt::AlignRight);
    form->setContentsMargins(6, 6, 6, 6);
    form->setSpacing(3);

    // Small font for compact display
    QFont smallFont("Segoe UI", 9);
    QString labelStyle = "QLabel { font-size: 9px; }";

    filename_ = new QLabel("-", content);  filename_->setWordWrap(true); filename_->setStyleSheet(labelStyle);
    path_     = new QLabel("-", content);  path_->setWordWrap(true);     path_->setStyleSheet(labelStyle);
    ext_      = new QLabel("-", content);  ext_->setStyleSheet(labelStyle);
    size_     = new QLabel("-", content);  size_->setStyleSheet(labelStyle);
    created_  = new QLabel("-", content);  created_->setStyleSheet(labelStyle);
    modified_ = new QLabel("-", content);  modified_->setStyleSheet(labelStyle);
    hash_     = new QLabel("-", content);  hash_->setWordWrap(true);     hash_->setStyleSheet(labelStyle);
    status_   = new QLabel("-", content);  status_->setStyleSheet(labelStyle);
    ocrStatus_= new QLabel("-", content);  ocrStatus_->setStyleSheet(labelStyle);

    form->addRow("Name",       filename_);
    form->addRow("Path",       path_);
    form->addRow("Type",       ext_);
    form->addRow("Size",       size_);
    form->addRow("Created",    created_);
    form->addRow("Modified",   modified_);
    form->addRow("Hash",       hash_);
    form->addRow("Index",      status_);
    form->addRow("Content",    ocrStatus_);

    scroll->setWidget(content);
    outer->addWidget(scroll);
}

static QString humanizeStatus(const QString& s) {
    if (s == "pending")         return "Pending";
    if (s == "metadata_only")   return "Metadata only";
    if (s == "content_done")    return "Content indexed";
    if (s == "ocr_done")        return "OCR complete";
    if (s == "failed")          return "Failed";
    if (s == "skipped")         return "Skipped";
    if (s == "not_needed")      return "Not needed";
    if (s == "done")            return "Done";
    if (s == "running")         return "Running";
    return s.isEmpty() ? "-" : s;
}

void MetadataPane::setRecord(const FileRecord& r) {
    filename_->setText(r.filename);
    path_->setText(r.path);
    ext_->setText(r.extension.toUpper());
    size_->setText(Utils::formatFileSize(r.size));
    created_->setText(r.createdDate.toString("yyyy-MM-dd hh:mm"));
    modified_->setText(r.modifiedDate.toString("yyyy-MM-dd hh:mm"));
    hash_->setText(r.hash.isEmpty() ? "-" : r.hash);
    status_->setText(humanizeStatus(r.indexingStatus));
    ocrStatus_->setText(humanizeStatus(r.ocrStatus));

    if (r.indexingStatus == "content_done" || r.indexingStatus == "ocr_done") {
        status_->setStyleSheet("QLabel { font-size: 9px; color: green; }");
    } else if (r.indexingStatus == "pending" || r.indexingStatus == "metadata_only") {
        status_->setStyleSheet("QLabel { font-size: 9px; color: orange; }");
    } else if (r.indexingStatus == "failed") {
        status_->setStyleSheet("QLabel { font-size: 9px; color: red; }");
    } else {
        status_->setStyleSheet("QLabel { font-size: 9px; }");
    }

    if (r.ocrStatus == "not_needed") {
        ocrStatus_->setStyleSheet("QLabel { font-size: 9px; color: gray; }");
    } else if (r.ocrStatus == "done") {
        ocrStatus_->setStyleSheet("QLabel { font-size: 9px; color: green; }");
    } else if (r.ocrStatus == "pending") {
        ocrStatus_->setStyleSheet("QLabel { font-size: 9px; color: orange; }");
    } else {
        ocrStatus_->setStyleSheet("QLabel { font-size: 9px; }");
    }
}

} // namespace DocuSearch
