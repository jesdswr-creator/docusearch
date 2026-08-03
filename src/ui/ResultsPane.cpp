// ============================================================
// ResultsPane.cpp - Modern search results list
// ============================================================

#include "ResultsPane.h"
#include "ResultItemDelegate.h"
#include "IconUtils.h"
#include "../core/StringUtils.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QListWidgetItem>
#include <QApplication>
#include <QPalette>

namespace DocuSearch {

namespace {
// Data roles for QListWidgetItem — used by both ResultsPane and
// ResultItemDelegate to access item data without a widget tree.
const int kRoleFileId       = Qt::UserRole + 1;
const int kRolePath         = Qt::UserRole + 2;
const int kRoleFilename     = Qt::UserRole + 3;
const int kRoleSnippet      = Qt::UserRole + 4;  // matches delegate's ext lookup
const int kRoleExtension    = Qt::UserRole + 5;
const int kRoleSize         = Qt::UserRole + 6;
const int kRoleModifiedDate = Qt::UserRole + 7;
const int kRoleScore        = Qt::UserRole + 8;

// Extension -> brand color (matches the HTML design).
const QHash<QString, QString> kExtColor = {
    {"pdf",  "#ef4444"},   // red
    {"doc",  "#2563eb"},   // blue
    {"docx", "#2563eb"},
    {"xls",  "#16a34a"},   // green
    {"xlsx", "#16a34a"},
    {"xlsm", "#16a34a"},
    {"ppt",  "#ea580c"},   // orange
    {"pptx", "#ea580c"},
    {"txt",  "#6b7280"},   // gray
    {"csv",  "#6b7280"},
    {"md",   "#6b7280"},
    {"rtf",  "#6b7280"},
    {"jpg",  "#7c3aed"},   // purple (image)
    {"jpeg", "#7c3aed"},
    {"png",  "#7c3aed"},
    {"tif",  "#7c3aed"},
    {"tiff", "#7c3aed"},
    {"bmp",  "#7c3aed"},
    {"gif",  "#7c3aed"},
    {"webp", "#7c3aed"},
};

// Extension -> 1-3 character label shown on the file badge.
const QHash<QString, QString> kExtLabel = {
    {"pdf",  "PDF"},
    {"doc",  "W"},
    {"docx", "W"},
    {"xls",  "X"},
    {"xlsx", "X"},
    {"xlsm", "X"},
    {"ppt",  "P"},
    {"pptx", "P"},
    {"txt",  "TXT"},
    {"csv",  "CSV"},
    {"md",   "MD"},
    {"rtf",  "RTF"},
    {"jpg",  "IMG"},
    {"jpeg", "IMG"},
    {"png",  "IMG"},
    {"tif",  "IMG"},
    {"tiff", "IMG"},
    {"bmp",  "IMG"},
    {"gif",  "IMG"},
    {"webp", "IMG"},
};
} // namespace

ResultsPane::ResultsPane(QWidget* parent) : QWidget(parent) {
    setObjectName("resultsPanel");

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    // ---- Header: title + count + sort dropdown ----
    auto* header = new QWidget(this);
    auto* hLay = new QHBoxLayout(header);
    hLay->setContentsMargins(16, 12, 16, 12);
    hLay->setSpacing(8);

    titleLbl_ = new QLabel("Search Results", header);
    titleLbl_->setObjectName("resultsTitle");
    countLbl_ = new QLabel("(0)", header);
    countLbl_->setObjectName("resultsCount");
    hLay->addWidget(titleLbl_);
    hLay->addWidget(countLbl_);
    hLay->addStretch();

    sortBox_ = new QComboBox(header);
    sortBox_->setObjectName("sortSelect");
    sortBox_->addItem("Sort: Relevance");
    sortBox_->addItem("Sort: Date");
    sortBox_->addItem("Sort: Size");
    sortBox_->addItem("Sort: Name");
    hLay->addWidget(sortBox_);
    v->addWidget(header);

    // ---- Results list ----
    list_ = new QListWidget(this);
    list_->setObjectName("resultsList");
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setSpacing(0);
    list_->setMouseTracking(true);  // required for hover state in delegate
    // Custom delegate paints each item directly via QPainter — much faster
    // than setItemWidget (no QWidget tree per row) and gives smooth
    // hover/selection card styling.
    list_->setItemDelegate(new ResultItemDelegate(list_));
    v->addWidget(list_, 1);

    connect(list_, &QListWidget::currentRowChanged, this, &ResultsPane::onItemClicked);
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it){
        bool ok = false;
        const qint64 id = it ? it->data(kRoleFileId).toLongLong(&ok) : 0;
        const QString p = it ? it->data(kRolePath).toString() : QString();
        if (ok && id != 0) emit fileActivated(id, p);
    });
    connect(sortBox_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ResultsPane::onSortChanged);
}

void ResultsPane::setResults(const QList<SearchHit>& hits) {
    list_->clear();
    current_ = hits;
    for (int i = 0; i < hits.size(); ++i) {
        populateItem(i, hits[i]);
    }
    countLbl_->setText(QString("(%1)").arg(hits.size()));
}

void ResultsPane::appendResults(const QList<SearchHit>& hits) {
    for (const auto& h : hits) {
        const int row = list_->count();
        current_.append(h);
        populateItem(row, h);
    }
    countLbl_->setText(QString("(%1)").arg(current_.size()));
}

void ResultsPane::populateItem(int row, const SearchHit& h) {
    auto* item = new QListWidgetItem(list_);
    item->setData(kRoleFileId, h.fileId);
    item->setData(kRolePath, h.path);
    item->setData(kRoleFilename, h.filename);
    item->setData(kRoleSnippet, h.snippet);
    item->setData(kRoleExtension, h.extension);
    item->setData(kRoleSize, h.size);
    item->setData(kRoleModifiedDate, h.modifiedDate.toSecsSinceEpoch());
    item->setData(kRoleScore, h.score);
    item->setToolTip(h.path);
    // Size hint matches ResultItemDelegate::sizeHint (72px fixed height)
    item->setSizeHint(QSize(280, 72));
    // No setItemWidget — the delegate paints everything via QPainter.
}

void ResultsPane::clear() {
    list_->clear();
    current_.clear();
    countLbl_->setText("(0)");
}

qint64 ResultsPane::selectedFileId() const {
    auto* it = list_->currentItem();
    if (!it) return 0;
    bool ok = false;
    const qint64 id = it->data(kRoleFileId).toLongLong(&ok);
    return ok ? id : 0;
}

QString ResultsPane::selectedPath() const {
    auto* it = list_->currentItem();
    return it ? it->data(kRolePath).toString() : QString();
}

void ResultsPane::onItemClicked(int row) {
    if (row < 0 || row >= list_->count()) return;
    auto* it = list_->item(row);
    if (!it) return;
    bool ok = false;
    const qint64 id = it->data(kRoleFileId).toLongLong(&ok);
    const QString p = it->data(kRolePath).toString();
    if (ok && id != 0) emit fileSelected(id, p);
}

void ResultsPane::onItemDoubleClicked(int row) {
    if (row < 0 || row >= list_->count()) return;
    auto* it = list_->item(row);
    if (!it) return;
    bool ok = false;
    const qint64 id = it->data(kRoleFileId).toLongLong(&ok);
    const QString p = it->data(kRolePath).toString();
    if (ok && id != 0) emit fileActivated(id, p);
}

void ResultsPane::onSortChanged(int index) {
    if (current_.isEmpty()) return;
    QList<SearchHit> sorted = current_;
    switch (index) {
        case 1: // Date
            std::sort(sorted.begin(), sorted.end(),
                [](const SearchHit& a, const SearchHit& b){
                    return a.modifiedDate > b.modifiedDate;
                });
            break;
        case 2: // Size
            std::sort(sorted.begin(), sorted.end(),
                [](const SearchHit& a, const SearchHit& b){
                    return a.size > b.size;
                });
            break;
        case 3: // Name
            std::sort(sorted.begin(), sorted.end(),
                [](const SearchHit& a, const SearchHit& b){
                    return a.filename.toLower() < b.filename.toLower();
                });
            break;
        default: // Relevance — keep original order
            break;
    }
    setResults(sorted);
}

void ResultsPane::refreshIcons() {
    // No icons to refresh in this pane — file badges use text labels
    // colored by extension. The QSS handles dark/light backgrounds.
}

QString ResultsPane::colorForExtension(const QString& ext) const {
    auto it = kExtColor.constFind(ext.toLower());
    return it == kExtColor.constEnd() ? "#6b7280" : it.value();
}

QString ResultsPane::labelForExtension(const QString& ext) const {
    auto it = kExtLabel.constFind(ext.toLower());
    return it == kExtLabel.constEnd() ? ext.toUpper().left(3) : it.value();
}

QString ResultsPane::humanizeSize(qint64 bytes) const {
    return Utils::formatFileSize(bytes);
}

QString ResultsPane::stripBoldTags(const QString& s) const {
    QString out = s;
    out.remove("<b>");
    out.remove("</b>");
    out.remove("<B>");
    out.remove("</B>");
    return out;
}

} // namespace DocuSearch
