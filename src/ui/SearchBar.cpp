// ============================================================
// SearchBar.cpp
// ============================================================

#include "SearchBar.h"

#include <QHBoxLayout>
#include <QCompleter>
#include <QStringListModel>

namespace DocuSearch {

SearchBar::SearchBar(QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    edit_      = new QLineEdit(this);
    edit_->setPlaceholderText("Search files and content...  (e.g. NOC type:pdf  or  \"Executive Lounge\")");
    edit_->setClearButtonEnabled(true);
    edit_->setMinimumHeight(30);

    searchBtn_ = new QPushButton("Search", this);
    searchBtn_->setDefault(true);
    searchBtn_->setMinimumHeight(30);
    searchBtn_->setStyleSheet(
        "QPushButton { background: #0078D4; color: white; border: none; "
        "border-radius: 4px; padding: 6px 18px; font-size: 13px; font-weight: bold; }"
        "QPushButton:hover { background: #0067C0; }"
        "QPushButton:pressed { background: #0054A6; }");

    savedBox_  = new QComboBox(this);
    savedBox_->setToolTip("Click to run a saved search");
    savedBox_->addItem("-- Saved Searches --");
    savedBox_->setMinimumHeight(30);
    savedBox_->setStyleSheet(
        "QComboBox { border: 1px solid #ccc; border-radius: 4px; padding: 4px 8px; }"
        "QComboBox:hover { border-color: #0078D4; }");

    // Filters button removed - all filters work via search syntax
    // (type:pdf, folder:Railway, date:2026, tag:Urgent, etc.)
    // See Help -> How to Search for the full syntax guide.
    filterBtn_ = nullptr;

    layout->addWidget(edit_, 1);
    layout->addWidget(searchBtn_);
    layout->addWidget(savedBox_);

    connect(searchBtn_, &QPushButton::clicked, this, &SearchBar::onReturnPressed);
    connect(edit_, &QLineEdit::returnPressed, this, &SearchBar::onReturnPressed);
    connect(edit_, &QLineEdit::textChanged, this, &SearchBar::onTextChanged);
    connect(savedBox_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int idx){
        if (idx > 0) {
            emit savedSearchSelected(savedBox_->itemText(idx));
            savedBox_->setCurrentIndex(0);
        }
    });

    // Auto-complete from saved searches
    auto* completer = new QCompleter(this);
    completer->setModel(new QStringListModel(this));
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    edit_->setCompleter(completer);
}

QString SearchBar::text() const { return edit_->text(); }
void SearchBar::setText(const QString& s) { edit_->setText(s); }
void SearchBar::setPlaceholder(const QString& s) { edit_->setPlaceholderText(s); }

void SearchBar::setSavedSearches(const QStringList& names) {
    savedBox_->blockSignals(true);
    savedBox_->clear();
    savedBox_->addItem("-- Saved Searches --");
    savedBox_->addItems(names);
    savedBox_->blockSignals(false);
    auto* m = qobject_cast<QStringListModel*>(edit_->completer()->model());
    if (m) m->setStringList(names);
}

void SearchBar::onReturnPressed() {
    emit searchRequested(edit_->text().trimmed());
}

void SearchBar::onTextChanged(const QString& s) {
    Q_UNUSED(s);
}

} // namespace DocuSearch
