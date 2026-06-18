// ============================================================
// ResultsPane.cpp
// ============================================================

#include "ResultsPane.h"
#include "../core/StringUtils.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>

namespace DocuSearch {

namespace {
// Each list item carries the SearchHit as Qt::UserRole + 1.
const int kRoleFileId = Qt::UserRole + 1;
const int kRolePath   = Qt::UserRole + 2;
}

ResultsPane::ResultsPane(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);

    countLabel_ = new QLabel("No results", this);
    countLabel_->setObjectName("subtitleLabel");
    v->addWidget(countLabel_);

    list_ = new QListWidget(this);
    list_->setAlternatingRowColors(true);
    list_->setSelectionBehavior(QAbstractItemView::SelectRows);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setUniformItemSizes(false);
    list_->setWordWrap(true);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);

    v->addWidget(list_);

    connect(list_, &QListWidget::itemClicked,     this, &ResultsPane::onItemClicked);
    connect(list_, &QListWidget::itemActivated,   this, &ResultsPane::onItemActivated);
}

void ResultsPane::setResults(const QList<SearchHit>& hits) {
    list_->clear();
    current_.clear();
    appendResults(hits);
    countLabel_->setText(QString("%1 result%2").arg(hits.size())
                         .arg(hits.size() == 1 ? "" : "s"));
}

void ResultsPane::appendResults(const QList<SearchHit>& hits) {
    for (const auto& h : hits) {
        auto* item = new QListWidgetItem(list_);

        QString display = QString("<b style='font-size:14px;'>%1</b>"
                                  " <span style='color:#888; font-size:11px;'>[%2]</span>"
                                  "<br><span style='color:#aaa; font-size:11px;'>%3</span>"
                                  "<br><span style='color:#666; font-size:11px;'>%4 · %5</span>")
            .arg(h.filename.toHtmlEscaped(),
                 h.extension.toUpper(),
                 h.path.toHtmlEscaped(),
                 Utils::formatFileSize(h.size),
                 h.modifiedDate.toString("yyyy-MM-dd"));

        if (!h.snippet.isEmpty()) {
            display += "<br><span style='color:#888; font-size:11px;'>" +
                       h.snippet.toHtmlEscaped() + "</span>";
        }

        item->setText(display);
        item->setData(kRoleFileId, h.fileId);
        item->setData(kRolePath,   h.path);
        item->setToolTip(h.path);
        QSize sz = item->sizeHint();
        sz.setHeight(h.snippet.isEmpty() ? 72 : 100);
        item->setSizeHint(sz);
        current_.append(h);
    }
}

void ResultsPane::clear() {
    list_->clear();
    current_.clear();
    countLabel_->setText("No results");
}

qint64 ResultsPane::selectedFileId() const {
    auto* cur = list_->currentItem();
    return cur ? cur->data(kRoleFileId).toLongLong() : 0;
}

QString ResultsPane::selectedPath() const {
    auto* cur = list_->currentItem();
    return cur ? cur->data(kRolePath).toString() : QString();
}

void ResultsPane::onItemClicked(QListWidgetItem* item) {
    const qint64 id = item->data(kRoleFileId).toLongLong();
    const QString path = item->data(kRolePath).toString();
    emit fileSelected(id, path);
}

void ResultsPane::onItemActivated(QListWidgetItem* item) {
    const qint64 id = item->data(kRoleFileId).toLongLong();
    const QString path = item->data(kRolePath).toString();
    emit fileActivated(id, path);
}

} // namespace DocuSearch
