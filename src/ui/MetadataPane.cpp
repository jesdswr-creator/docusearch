// ============================================================
// MetadataPane.cpp - File metadata display matching reference design
// ============================================================

#include "MetadataPane.h"
#include "IconUtils.h"
#include "../core/StringUtils.h"
#include "../core/Constants.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QApplication>
#include <QPalette>

namespace DocuSearch {

namespace {

// A single metadata row: [icon] [label] [value].
// The row widget is returned; the value label pointer is written to *valueOut.
QWidget* makeMetaRow(QWidget* parent, const QString& labelText, QLabel** valueOut) {
    auto* row = new QWidget(parent);
    row->setObjectName("metadataSection");
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(16, 5, 16, 5);
    h->setSpacing(10);

    auto* iconLbl = new QLabel(row);
    iconLbl->setObjectName("metaIconLabel");
    iconLbl->setFixedSize(18, 18);

    auto* labelLbl = new QLabel(labelText, row);
    labelLbl->setObjectName("metaLabel");

    auto* valueLbl = new QLabel("-", row);
    valueLbl->setObjectName("metaValue");
    valueLbl->setWordWrap(true);
    valueLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);

    h->addWidget(iconLbl);
    h->addWidget(labelLbl);
    h->addWidget(valueLbl, 1);

    *valueOut = valueLbl;
    return row;
}

} // namespace

MetadataPane::MetadataPane(QWidget* parent) : QWidget(parent) {
    setObjectName("metadataPanel");

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ---- Header ----
    auto* header = new QWidget(this);
    auto* hLay = new QHBoxLayout(header);
    hLay->setContentsMargins(16, 12, 16, 12);
    hLay->setSpacing(6);

    titleLbl_ = new QLabel("Metadata", header);
    titleLbl_->setObjectName("metadataTitle");
    hLay->addWidget(titleLbl_);

    infoIconLbl_ = new QLabel("i", header);
    infoIconLbl_->setObjectName("infoIcon");
    infoIconLbl_->setToolTip("File metadata extracted from the filesystem and indexed content.");
    hLay->addWidget(infoIconLbl_);

    hLay->addStretch();

    editBtn_ = new QPushButton(header);
    editBtn_->setObjectName("editBtn");
    editBtn_->setCursor(Qt::PointingHandCursor);
    editBtn_->setToolTip("Edit metadata");
    hLay->addWidget(editBtn_);
    outer->addWidget(header);

    // ---- Scrollable metadata rows ----
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* inner = new QWidget(scroll);
    auto* innerLay = new QVBoxLayout(inner);
    innerLay->setContentsMargins(0, 0, 0, 0);
    innerLay->setSpacing(0);

    innerLay->addWidget(makeMetaRow(inner, "File Name",     &filename_));
    innerLay->addWidget(makeMetaRow(inner, "Path",          &path_));
    innerLay->addWidget(makeMetaRow(inner, "Type",          &type_));
    innerLay->addWidget(makeMetaRow(inner, "Size",          &size_));
    innerLay->addWidget(makeMetaRow(inner, "Created",       &created_));
    innerLay->addWidget(makeMetaRow(inner, "Modified",      &modified_));
    innerLay->addWidget(makeMetaRow(inner, "Hash (SHA-256)", &hash_));
    innerLay->addWidget(makeMetaRow(inner, "Index",         &indexLbl_));
    innerLay->addWidget(makeMetaRow(inner, "Content",       &content_));
    innerLay->addStretch();

    scroll->setWidget(inner);
    outer->addWidget(scroll, 1);

    refreshIcons();
}

void MetadataPane::setRecord(const FileRecord& r) {
    filename_->setText(r.filename);
    path_->setText(r.path);
    type_->setText(r.extension.isEmpty() ? "-" : r.extension.toUpper() + " Document");
    size_->setText(Utils::formatFileSize(r.size));
    created_->setText(r.createdDate.toString("dd MMM yyyy hh:mm AP"));
    modified_->setText(r.modifiedDate.toString("dd MMM yyyy hh:mm AP"));
    hash_->setText(r.hash.isEmpty() ? "-" : r.hash.left(32) + "...");

    // Index status: combine indexing + OCR status into a short label.
    QString idxText;
    if (r.ocrStatus == Constants::OcrStatus::kDone ||
        r.indexingStatus == Constants::IndexingStatus::kOcrDone) {
        idxText = "Text + OCR";
    } else if (r.indexingStatus == Constants::IndexingStatus::kContentDone) {
        idxText = "Text";
    } else if (r.indexingStatus == Constants::IndexingStatus::kMetadataOnly) {
        idxText = "Metadata only";
    } else if (r.indexingStatus == Constants::IndexingStatus::kFailed) {
        idxText = "Failed";
    } else {
        idxText = humanizeStatus(r.indexingStatus);
    }
    indexLbl_->setText(idxText);

    // Content: best guess based on extension.
    const QString ext = r.extension.toLower();
    if (ext == "pdf" || ext == "docx" || ext == "pptx") {
        content_->setText("Text + Image");
    } else if (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "bmp" ||
               ext == "tiff" || ext == "tif" || ext == "webp" || ext == "gif") {
        content_->setText("Image only");
    } else {
        content_->setText("Text only");
    }
}

void MetadataPane::refreshIcons() {
    // Use the muted secondary color from the design (#6b7280).
    // We use a hardcoded color rather than palette so the icons stay
    // the same shade of gray in both light and dark mode (the design
    // uses #6b7280 for metadata icons in both modes).
    QColor iconColor("#6b7280");

    // Walk the scroll area's inner layout to find each row's icon label.
    auto* scroll = findChild<QScrollArea*>();
    if (!scroll) return;
    auto* inner = scroll->widget();
    if (!inner) return;
    auto* v = inner->layout();
    if (!v) return;

    const QStringList icons = {
        "file", "folder", "file", "upload", "calendar",
        "calendar", "lock", "search", "image"
    };
    for (int i = 0; i < 9 && i < v->count(); ++i) {
        auto* item = v->itemAt(i);
        if (!item) continue;
        auto* row = qobject_cast<QWidget*>(item->widget());
        if (!row) continue;
        auto* h = row->layout();
        if (!h || h->count() < 1) continue;
        auto* iconLbl = qobject_cast<QLabel*>(h->itemAt(0)->widget());
        if (!iconLbl) continue;
        iconLbl->setPixmap(loadLucidePixmap(icons[i], iconColor, 16, row->devicePixelRatio()));
    }

    // Edit button icon
    editBtn_->setIcon(loadLucideIcon("pencil", iconColor, 16));
    editBtn_->setIconSize(QSize(16, 16));
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
    if (s == Constants::IndexingStatus::kContentDone)  return "#2E7D32";
    if (s == Constants::IndexingStatus::kOcrDone)      return "#2E7D32";
    if (s == Constants::OcrStatus::kDone)              return "#2E7D32";
    if (s == "done")                                    return "#2E7D32";
    if (s == Constants::IndexingStatus::kPending)      return "#E07B00";
    if (s == Constants::IndexingStatus::kMetadataOnly) return "#E07B00";
    if (s == Constants::OcrStatus::kPending)           return "#E07B00";
    if (s == Constants::IndexingStatus::kFailed)       return "#C62828";
    if (s == Constants::OcrStatus::kFailed)            return "#C62828";
    if (s == Constants::OcrStatus::kNotNeeded)         return "#808080";
    return "#808080";
}

} // namespace DocuSearch
