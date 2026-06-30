// ============================================================
// ResultsPane.cpp - QTableWidget-based results list
// ============================================================

#include "ResultsPane.h"
#include "../core/StringUtils.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QHeaderView>
#include <QFont>
#include <QBrush>
#include <QColor>

namespace DocuSearch {

namespace {
// Each Name cell carries the fileId + path as Qt::UserRole + 1 / + 2.
const int kRoleFileId = Qt::UserRole + 1;
const int kRolePath   = Qt::UserRole + 2;

// Extension -> brand color (Office / common colors).
const QHash<QString, QString> kExtColor = {
    {"pdf",  "#EB4034"},   // red
    {"doc",  "#2B579A"},   // blue
    {"docx", "#2B579A"},
    {"xls",  "#216746"},   // green
    {"xlsx", "#216746"},
    {"xlsm", "#216746"},
    {"ppt",  "#E36C0A"},   // orange
    {"pptx", "#E36C0A"},
    {"txt",  "#808080"},   // gray
    {"csv",  "#808080"},
    {"md",   "#808080"},
    {"rtf",  "#808080"},
    {"jpg",  "#7030A0"},   // purple (image)
    {"jpeg", "#7030A0"},
    {"png",  "#7030A0"},
    {"tif",  "#7030A0"},
    {"tiff", "#7030A0"},
    {"bmp",  "#7030A0"},
    {"gif",  "#7030A0"},
    {"webp", "#7030A0"},
};

QString humanSize(qint64 bytes) {
    return Utils::formatFileSize(bytes);
}
}

ResultsPane::ResultsPane(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(6);

    // Modern result count label with larger, bolder text.
    countLabel_ = new QLabel("No results", this);
    countLabel_->setObjectName("subtitleLabel");
    countLabel_->setStyleSheet(
        "QLabel { font-size: 12px; color: #606060; padding: 2px 4px; }");
    v->addWidget(countLabel_);

    table_ = new QTableWidget(this);
    table_->setColumnCount(ColCount);
    table_->setAlternatingRowColors(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setHorizontalHeaderLabels({"Name", "Type", "Size", "Date", "Snippet"});
    table_->verticalHeader()->setVisible(false);
    table_->setWordWrap(true);
    // No grid lines for a cleaner modern look (Windows 11 style).
    table_->setShowGrid(false);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);

    // Modern table styling: no grid, rounded selection, better padding.
    table_->setStyleSheet(
        "QTableWidget { "
        "  border: 1px solid #e0e0e0; "
        "  border-radius: 8px; "
        "  background-color: #ffffff; "
        "  gridline-color: transparent; "
        "} "
        "QTableWidget::item { "
        "  padding: 8px 10px; "
        "  border-bottom: 1px solid #f0f0f0; "
        "} "
        "QTableWidget::item:hover { "
        "  background-color: #f5f5f5; "
        "} "
        "QTableWidget::item:selected { "
        "  background-color: #cce4f7; "
        "  color: #000000; "
        "} "
        "QHeaderView::section { "
        "  background-color: #fafafa; "
        "  color: #4a4a4a; "
        "  padding: 10px 10px; "
        "  border: none; "
        "  border-bottom: 1px solid #e0e0e0; "
        "  font-weight: 600; "
        "  font-size: 12px; "
        "}");

    // Header sizing: Name & Snippet stretch, others size to contents.
    auto* hh = table_->horizontalHeader();
    hh->setStretchLastSection(false);
    hh->setSectionResizeMode(ColName,    QHeaderView::Stretch);
    hh->setSectionResizeMode(ColType,    QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(ColSize,    QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(ColDate,    QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(ColSnippet, QHeaderView::Stretch);
    hh->setHighlightSections(false);

    v->addWidget(table_);

    connect(table_, &QTableWidget::cellClicked,    this, &ResultsPane::onCellClicked);
    connect(table_, &QTableWidget::cellDoubleClicked,
            this, &ResultsPane::onCellActivated);
}

void ResultsPane::setResults(const QList<SearchHit>& hits) {
    table_->setRowCount(0);
    current_.clear();
    appendResults(hits);
    countLabel_->setText(QString("%1 result%2").arg(hits.size())
                         .arg(hits.size() == 1 ? "" : "s"));
}

void ResultsPane::appendResults(const QList<SearchHit>& hits) {
    for (const auto& h : hits) {
        const int row = table_->rowCount();
        table_->insertRow(row);
        populateRow(row, h);
        current_.append(h);
    }
}

void ResultsPane::populateRow(int row, const SearchHit& h) {
    // --- Name (col 0) - bold Segoe UI, carries fileId+path in UserRole ---
    auto* nameItem = new QTableWidgetItem(h.filename);
    QFont nameFont("Segoe UI", 10);
    nameFont.setBold(true);
    nameItem->setFont(nameFont);
    nameItem->setData(kRoleFileId, h.fileId);
    nameItem->setData(kRolePath,   h.path);
    nameItem->setToolTip(h.path);
    table_->setItem(row, ColName, nameItem);

    // --- Type (col 1) - color-coded by extension ---
    auto* typeItem = new QTableWidgetItem(h.extension.toUpper());
    QFont typeFont("Segoe UI", 9);
    typeFont.setBold(true);
    typeItem->setFont(typeFont);
    typeItem->setForeground(QBrush(QColor(colorForExtension(h.extension))));
    typeItem->setTextAlignment(Qt::AlignCenter);
    table_->setItem(row, ColType, typeItem);

    // --- Size (col 2) - right-aligned, gray ---
    auto* sizeItem = new QTableWidgetItem(humanSize(h.size));
    sizeItem->setForeground(QBrush(QColor("#808080")));
    sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QFont sizeFont("Segoe UI", 9);
    sizeItem->setFont(sizeFont);
    table_->setItem(row, ColSize, sizeItem);

    // --- Date (col 3) - centered, gray, yyyy-MM-dd ---
    auto* dateItem = new QTableWidgetItem(h.modifiedDate.toString("yyyy-MM-dd"));
    dateItem->setForeground(QBrush(QColor("#808080")));
    dateItem->setTextAlignment(Qt::AlignCenter);
    dateItem->setFont(sizeFont);
    table_->setItem(row, ColDate, dateItem);

    // --- Snippet (col 4) - strip <b></b>, truncate 150, gray ---
    QString snip = stripBoldTags(h.snippet);
    if (snip.size() > 150) snip = snip.left(150) + "...";
    auto* snipItem = new QTableWidgetItem(snip);
    snipItem->setForeground(QBrush(QColor("#808080")));
    QFont snipFont("Segoe UI", 9);
    snipItem->setFont(snipFont);
    table_->setItem(row, ColSnippet, snipItem);

    // Row height: 56 if there's a snippet, else 40. Modern Windows 11
    // apps use generous row heights for better readability.
    table_->setRowHeight(row, h.snippet.isEmpty() ? 40 : 56);
}

void ResultsPane::clear() {
    table_->setRowCount(0);
    current_.clear();
    countLabel_->setText("No results");
}

qint64 ResultsPane::selectedFileId() const {
    const int row = table_->currentRow();
    if (row < 0) return 0;
    auto* it = table_->item(row, ColName);
    return it ? it->data(kRoleFileId).toLongLong() : 0;
}

QString ResultsPane::selectedPath() const {
    const int row = table_->currentRow();
    if (row < 0) return QString();
    auto* it = table_->item(row, ColName);
    return it ? it->data(kRolePath).toString() : QString();
}

void ResultsPane::onCellClicked(int row, int /*col*/) {
    auto* it = table_->item(row, ColName);
    if (!it) return;
    emit fileSelected(it->data(kRoleFileId).toLongLong(),
                      it->data(kRolePath).toString());
}

void ResultsPane::onCellActivated(int row, int /*col*/) {
    auto* it = table_->item(row, ColName);
    if (!it) return;
    emit fileActivated(it->data(kRoleFileId).toLongLong(),
                       it->data(kRolePath).toString());
}

QString ResultsPane::colorForExtension(const QString& ext) const {
    auto it = kExtColor.constFind(ext.toLower());
    return it == kExtColor.constEnd() ? "#808080" : it.value();
}

QString ResultsPane::humanizeSize(qint64 bytes) const {
    return humanSize(bytes);
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
