// ============================================================
// MetadataPane.cpp
// ============================================================

#include "MetadataPane.h"
#include "../core/StringUtils.h"

#include <QFormLayout>
#include <QGroupBox>

namespace DocuSearch {

MetadataPane::MetadataPane(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);

    auto* gb = new QGroupBox("Metadata", this);
    auto* form = new QFormLayout(gb);
    form->setLabelAlignment(Qt::AlignRight);

    filename_ = new QLabel("—", this);  filename_->setWordWrap(true);
    path_     = new QLabel("—", this);  path_->setWordWrap(true);
    ext_      = new QLabel("—", this);
    size_     = new QLabel("—", this);
    created_  = new QLabel("—", this);
    modified_ = new QLabel("—", this);
    hash_     = new QLabel("—", this);  hash_->setWordWrap(true);
    status_   = new QLabel("—", this);
    ocrStatus_= new QLabel("—", this);

    form->addRow("Name",       filename_);
    form->addRow("Path",       path_);
    form->addRow("Type",       ext_);
    form->addRow("Size",       size_);
    form->addRow("Created",    created_);
    form->addRow("Modified",   modified_);
    form->addRow("Hash",       hash_);
    form->addRow("Status",     status_);
    form->addRow("OCR",        ocrStatus_);

    outer->addWidget(gb);
    outer->addStretch();
}

void MetadataPane::setRecord(const FileRecord& r) {
    filename_->setText(r.filename);
    path_->setText(r.path);
    ext_->setText(r.extension.toUpper());
    size_->setText(Utils::formatFileSize(r.size));
    created_->setText(r.createdDate.toString("yyyy-MM-dd hh:mm"));
    modified_->setText(r.modifiedDate.toString("yyyy-MM-dd hh:mm"));
    hash_->setText(r.hash.isEmpty() ? "—" : r.hash);
    status_->setText(r.indexingStatus);
    ocrStatus_->setText(r.ocrStatus);
}

} // namespace DocuSearch
