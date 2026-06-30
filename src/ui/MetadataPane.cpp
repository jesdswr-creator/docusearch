// ============================================================
// MetadataPane.cpp
// ============================================================

#include "MetadataPane.h"
#include "../core/StringUtils.h"
#include "../core/Constants.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QFont>
#include <QBrush>
#include <QColor>

namespace DocuSearch {

MetadataPane::MetadataPane(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    // Wrap content in a QScrollArea so labels don't get crushed.
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("QScrollArea { background: transparent; border: none; }");

    auto* inner = new QWidget(scroll);
    inner->setStyleSheet("background: transparent;");
    auto* innerLay = new QVBoxLayout(inner);
    innerLay->setContentsMargins(8, 8, 8, 8);
    innerLay->setSpacing(8);

    // Modern section header.
    auto* header = new QLabel("METADATA", inner);
    header->setStyleSheet(
        "QLabel { color: #0078D4; font-size: 11px; font-weight: bold; "
        "  text-transform: uppercase; letter-spacing: 1.5px; "
        "  background: transparent; border: none; padding: 2px; }");
    innerLay->addWidget(header);

    // Group box with modern card styling.
    auto* gb = new QGroupBox("Metadata", inner);
    gb->setStyleSheet(
        "QGroupBox { "
        "  border: 1px solid #e0e0e0; "
        "  border-radius: 8px; "
        "  margin-top: 14px; "
        "  padding: 16px 12px 12px 12px; "
        "  font-size: 12px; "
        "  font-weight: 600; "
        "  color: #1f1f1f; "
        "  background-color: #ffffff; "
        "} "
        "QGroupBox::title { "
        "  subcontrol-origin: margin; "
        "  subcontrol-position: top left; "
        "  left: 12px; "
        "  padding: 0 6px; "
        "  background-color: #ffffff; "
        "  color: #4a4a4a; "
        "}");
    auto* form = new QFormLayout(gb);
    form->setLabelAlignment(Qt::AlignRight);
    form->setSpacing(8);
    form->setContentsMargins(4, 4, 4, 4);

    filename_ = new QLabel("-", gb);  filename_->setWordWrap(true);
    path_     = new QLabel("-", gb);  path_->setWordWrap(true);
    ext_      = new QLabel("-", gb);
    size_     = new QLabel("-", gb);
    created_  = new QLabel("-", gb);
    modified_ = new QLabel("-", gb);
    hash_     = new QLabel("-", gb);  hash_->setWordWrap(true);
    status_   = new QLabel("-", gb);
    ocrStatus_= new QLabel("-", gb);

    styleLabel(filename_);
    styleLabel(path_);
    styleLabel(ext_);
    styleLabel(size_);
    styleLabel(created_);
    styleLabel(modified_);
    styleLabel(hash_);
    styleLabel(status_);
    styleLabel(ocrStatus_);

    form->addRow("Name",       filename_);
    form->addRow("Path",       path_);
    form->addRow("Type",       ext_);
    form->addRow("Size",       size_);
    form->addRow("Created",    created_);
    form->addRow("Modified",   modified_);
    form->addRow("Hash",       hash_);
    form->addRow("Index",      status_);
    form->addRow("Content",    ocrStatus_);

    innerLay->addWidget(gb);
    innerLay->addStretch();

    scroll->setWidget(inner);
    outer->addWidget(scroll);
}

void MetadataPane::setRecord(const FileRecord& r) {
    filename_->setText(r.filename);
    path_->setText(r.path);
    ext_->setText(r.extension.toUpper());
    size_->setText(Utils::formatFileSize(r.size));
    created_->setText(r.createdDate.toString("yyyy-MM-dd hh:mm"));
    modified_->setText(r.modifiedDate.toString("yyyy-MM-dd hh:mm"));
    hash_->setText(r.hash.isEmpty() ? "-" : r.hash);

    const QString idxText  = humanizeStatus(r.indexingStatus);
    const QString ocrText  = humanizeStatus(r.ocrStatus);
    status_->setText(idxText);
    ocrStatus_->setText(ocrText);
    status_->setStyleSheet(QString("color: %1;").arg(colorForStatus(r.indexingStatus)));
    ocrStatus_->setStyleSheet(QString("color: %1;").arg(colorForStatus(r.ocrStatus)));
}

void MetadataPane::styleLabel(QLabel* lbl) {
    QFont f = lbl->font();
    f.setFamily("Segoe UI");
    f.setPointSize(9);
    lbl->setFont(f);
}

QString MetadataPane::humanizeStatus(const QString& s) const {
    if (s == Constants::IndexingStatus::kPending)      return "Pending";
    if (s == Constants::IndexingStatus::kMetadataOnly) return "Metadata only";
    if (s == Constants::IndexingStatus::kContentDone)  return "Content indexed";
    if (s == Constants::IndexingStatus::kOcrDone)      return "OCR complete";
    if (s == Constants::IndexingStatus::kFailed)       return "Failed";
    if (s == Constants::OcrStatus::kNotNeeded)         return "Not needed";
    if (s == Constants::OcrStatus::kDone)              return "Done";
    if (s == Constants::OcrStatus::kPending)           return "Pending";
    if (s == Constants::OcrStatus::kFailed)            return "Failed";
    if (s == Constants::OcrStatus::kSkipped)           return "Skipped";
    if (s == Constants::OcrStatus::kRunning)           return "Running";
    if (s == Constants::IndexingStatus::kSkipped)      return "Skipped";
    if (s == "done")                                    return "Done";
    return s.isEmpty() ? "-" : s;
}

QString MetadataPane::colorForStatus(const QString& s) const {
    // green for done/content_done/ocr_done, orange for pending/metadata_only,
    // red for failed, gray for not_needed.
    if (s == Constants::IndexingStatus::kContentDone)  return "#2E7D32"; // green
    if (s == Constants::IndexingStatus::kOcrDone)      return "#2E7D32";
    if (s == Constants::OcrStatus::kDone)              return "#2E7D32";
    if (s == "done")                                    return "#2E7D32";
    if (s == Constants::IndexingStatus::kPending)      return "#E07B00"; // orange
    if (s == Constants::IndexingStatus::kMetadataOnly) return "#E07B00";
    if (s == Constants::OcrStatus::kPending)           return "#E07B00";
    if (s == Constants::IndexingStatus::kFailed)       return "#C62828"; // red
    if (s == Constants::OcrStatus::kFailed)            return "#C62828";
    if (s == Constants::OcrStatus::kNotNeeded)         return "#808080"; // gray
    return "#808080";
}

} // namespace DocuSearch
