// ============================================================
// ResultsPane.cpp - Modern search results list
// ============================================================

#include "ResultsPane.h"
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
// Each item carries the fileId + path as Qt::UserRole + 1 / + 2.
const int kRoleFileId = Qt::UserRole + 1;
const int kRolePath   = Qt::UserRole + 2;

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
    header
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
    item->setToolTip(h.path);
    item->setSizeHint(QSize(280, 70));

    // Build the item widget: file icon badge + (title / snippet / meta) + dot
    auto* w = new QWidget(list_);
    w
    auto* hLay = new QHBoxLayout(w);
    hLay->setContentsMargins(12, 10, 12, 10);
    hLay->setSpacing(10);

    // File-type colored badge (36x36)
    auto* badge = new QLabel(w);
    badge->setObjectName("fileIconBadge");
    const QString color = colorForExtension(h.extension);
    const QString label = labelForExtension(h.extension);
    badge->setFixedSize(36, 36);
    badge->setStyleSheet(QString(
        "background-color: %1; color: #ffffff; border-radius: 8px; "
        "font-size: 10px; font-weight: 700;").arg(color));
    badge->setAlignment(Qt::AlignCenter);
    badge->setText(label);
    hLay->addWidget(badge);

    // Title + snippet + meta (vertical stack)
    auto* info = new QWidget(w);
    auto* vLay = new QVBoxLayout(info);
    vLay->setContentsMargins(0, 0, 0, 0);
    vLay->setSpacing(2);

    auto* title = new QLabel(info);
    title->setObjectName("resultTitle");
    title->setText(h.filename);
    // Title color changes to dark blue when selected — handled via QSS
    // using the list-item:selected selector on #resultTitle.
    title

    auto* snippet = new QLabel(info);
    snippet->setObjectName("resultSnippet");
    QString snip = stripBoldTags(h.snippet);
    if (snip.size() > 120) snip = snip.left(120) + "...";
    snippet->setText(snip.isEmpty() ? "..." : snip);
    snippet->setWordWrap(false);
    snippet

    auto* meta = new QLabel(info);
    meta->setObjectName("resultMeta");
    const QString dateStr = h.modifiedDate.toString("dd MMM yyyy");
    meta->setText(QString("%1 • %2").arg(humanizeSize(h.size)).arg(dateStr));
    meta

    vLay->addWidget(title);
    vLay->addWidget(snippet);
    vLay->addWidget(meta);
    hLay->addWidget(info, 1);

    // Active dot on the right (visible only for the selected row)
    auto* dot = new QLabel(w);
    dot->setFixedSize(10, 10);
    // dot styled by QSS

    hLay->addWidget(dot, 0, Qt::AlignTop);

    item->setSizeHint(QSize(280, w->sizeHint().height()));
    list_->setItemWidget(item, w);
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
